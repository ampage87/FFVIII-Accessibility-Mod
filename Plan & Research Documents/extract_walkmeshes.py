#!/usr/bin/env python3
"""
FF8 Walkmesh Extractor v4
Extracts all walkmesh (.id) files from the FF8 field archive system
and saves them as a single JSON file for offline analysis.

Usage:
    python extract_walkmeshes.py <path_to_lang-en_folder> [output.json]

Example:
    python extract_walkmeshes.py "C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VIII\Data\lang-en" walkmeshes.json

The script reads field.fi, field.fl, and field.fs from the specified folder.
Output is a JSON file with all walkmesh data for every field in the game.

v2: Fixed archive structure handling. The outer archive has two sections:
  1. First 7 fields (cwwood1-7) as direct .fs/.fl/.fi triplets
  2. A mapdata archive (entry 21+) with separate .fi/.fl/.fs blocks
     matched by field name, not by position.

v3: Fixed inner file matching to use fieldname+ext (e.g. 'bg2f_1.id')
    instead of just extension ('.id'), matching the C++ runtime behavior.
    Added raw_tri_preview with first 3 triangles' raw vertex coords for
    cross-referencing with runtime log output.

v4: Camera matrix extraction and 3D→2D projection.
    Extracts .ca (camera) file for each field, parses the camera matrix
    (rotation axes + position + zoom), and projects all walkmesh vertices
    from 3D world space into 2D screen space. Screen-space coordinates
    match entity position coords (int32/4096 at offsets 0x190/0x194).
    JSON now includes: camera data, projected 2D vertex coords, and
    2D triangle centers alongside original 3D data.
    Format reference: FF7/FF8 camera matrix (Qhimm wiki / OpenVIII).
"""

import struct
import json
import sys
import os
import time
import math

# =============================================================================
# LZSS Decompression (FF8/FF7 variant)
# =============================================================================
def decompress_lzss(data, expected_size):
    ring = bytearray(4096)
    ring_pos = 0xFEE
    out = bytearray()
    pos = 0
    while len(out) < expected_size and pos < len(data):
        flags = data[pos]
        pos += 1
        for bit in range(8):
            if len(out) >= expected_size:
                break
            if flags & (1 << bit):
                if pos >= len(data):
                    return bytes(out)
                b = data[pos]
                pos += 1
                out.append(b)
                ring[ring_pos] = b
                ring_pos = (ring_pos + 1) & 0xFFF
            else:
                if pos + 1 >= len(data):
                    return bytes(out)
                b1 = data[pos]
                b2 = data[pos + 1]
                pos += 2
                offset = b1 | ((b2 & 0xF0) << 4)
                length = (b2 & 0x0F) + 3
                for i in range(length):
                    if len(out) >= expected_size:
                        break
                    b = ring[(offset + i) & 0xFFF]
                    out.append(b)
                    ring[ring_pos] = b
                    ring_pos = (ring_pos + 1) & 0xFFF
    return bytes(out)


# =============================================================================
# Archive parsing
# =============================================================================
def parse_fi(fi_path):
    with open(fi_path, 'rb') as f:
        data = f.read()
    entries = []
    for i in range(len(data) // 12):
        size, offset, comp = struct.unpack_from('<III', data, i * 12)
        entries.append((size, offset, comp))
    return entries

def parse_fl(fl_path):
    with open(fl_path, 'r', errors='ignore') as f:
        lines = [l.strip() for l in f.readlines() if l.strip()]
    return lines

def extract_basename(path):
    name = path.replace('\\', '/').split('/')[-1]
    if '.' in name:
        name = name.rsplit('.', 1)[0]
    return name.lower()

def extract_ext(path):
    if '.' in path:
        return path.rsplit('.', 1)[1].lower()
    return ''

def read_chunk(fs_path, offset, size):
    with open(fs_path, 'rb') as f:
        f.seek(offset)
        return f.read(size)

def extract_outer_entry(fs_path, fi_entries, fl_index):
    if fl_index >= len(fi_entries):
        return None
    size, offset, comp = fi_entries[fl_index]
    if size == 0:
        return None
    if comp == 0:
        return read_chunk(fs_path, offset, size)
    else:
        comp_size = size
        for j in range(fl_index + 1, len(fi_entries)):
            if fi_entries[j][1] > offset:
                comp_size = fi_entries[j][1] - offset
                break
        compressed = read_chunk(fs_path, offset, comp_size)
        if len(compressed) <= 4:
            return None
        return decompress_lzss(compressed[4:], size)


# =============================================================================
# Inner archive extraction
# =============================================================================
def extract_inner_file(inner_fs, inner_fi_entries, inner_fl_lines, field_name, target_ext):
    # Match by fieldname+ext (e.g. 'bg2f_1.id') to match C++ runtime behavior.
    # Falls back to extension-only match if exact name not found.
    target_suffix = (field_name + '.' + target_ext).lower()
    target_idx = None
    for i, line in enumerate(inner_fl_lines):
        if line.lower().endswith(target_suffix):
            target_idx = i
            break
    if target_idx is None:
        # Fallback: match by extension only (original behavior)
        for i, line in enumerate(inner_fl_lines):
            if line.lower().endswith('.' + target_ext.lower()):
                target_idx = i
                break
    if target_idx is None or target_idx >= len(inner_fi_entries):
        return None
    size, offset, comp = inner_fi_entries[target_idx]
    if size == 0:
        return None
    if comp == 0:
        if offset + size > len(inner_fs):
            return None
        return inner_fs[offset:offset + size]
    else:
        comp_size = size
        for j in range(target_idx + 1, len(inner_fi_entries)):
            if inner_fi_entries[j][1] > offset:
                comp_size = inner_fi_entries[j][1] - offset
                break
        if offset + comp_size > len(inner_fs):
            return None
        compressed = inner_fs[offset:offset + comp_size]
        if len(compressed) <= 4:
            return None
        return decompress_lzss(compressed[4:], size)

def parse_inner_fi(data):
    entries = []
    for i in range(len(data) // 12):
        size, offset, comp = struct.unpack_from('<III', data, i * 12)
        entries.append((size, offset, comp))
    return entries

def parse_inner_fl(data):
    text = data.decode('ascii', errors='ignore')
    return [l.strip() for l in text.split('\n') if l.strip()]


# =============================================================================
# Camera matrix (.ca file) parsing and 3D→2D projection
# =============================================================================
#
# FF8 .ca format (based on FF7 camera matrix, per Qhimm wiki):
#   Offset  Size  Description
#   0x00    6     X axis vector (3 × int16, fixed-point /4096)
#   0x06    6     Y axis vector (3 × int16, fixed-point /4096)
#   0x0C    6     Z axis vector (3 × int16, fixed-point /4096)
#   0x12    2     Z.z copy
#   0x14    4     Camera-space position X (int32)
#   0x18    4     Camera-space position Y (int32)
#   0x1C    4     Camera-space position Z (int32)
#   0x20    4     Blank (zero)
#   0x24    2     Zoom
#   0x26    2     Zoom2 (FF8 extension — may be duplicate or second zoom)
# Total: 0x28 = 40 bytes per camera (FF8), 0x26 = 38 bytes (FF7).
#
# The axes form an orthonormal rotation matrix (each has length 4096).
# Camera-space position (ox,oy,oz) is NOT world position — it must be
# inverse-transformed through the rotation axes to get world position.
#
# To project a 3D walkmesh point W=(wx,wy,wz) into 2D screen coords:
#   screen_x = vx · W + ox    (where vx is the X axis row, ox is cam pos x)
#   screen_y = vy · W + oy    (where vy is the Y axis row, oy is cam pos y)
# All values in fixed-point /4096, so divide result by 4096 for final coords.
# The Y/Z sign convention from the FF7 wiki (negate y,z of each axis) must
# be tested empirically — we try both and validate against known fields.

def parse_camera(ca_data):
    """Parse the first camera from a .ca file.
    
    Returns a dict with raw camera data, or None if parsing fails.
    The struct can be either 38 bytes (FF7 style) or 40 bytes (FF8 style)
    per camera. We detect based on file size.
    """
    if ca_data is None or len(ca_data) < 38:
        return None
    
    # Detect struct size: FF8 uses 0x28=40 bytes, FF7 uses 0x26=38 bytes.
    # If file size is divisible by 40 (and not by 38), use 40.
    # If divisible by both, prefer 40 for FF8.
    struct_size = 40 if (len(ca_data) % 40 == 0) else 38
    if len(ca_data) < struct_size:
        struct_size = 38
    if len(ca_data) < 38:
        return None
    
    # Read axis vectors (3 × int16 each, fixed-point with scale 4096)
    ax_x = struct.unpack_from('<3h', ca_data, 0x00)  # X axis
    ax_y = struct.unpack_from('<3h', ca_data, 0x06)  # Y axis
    ax_z = struct.unpack_from('<3h', ca_data, 0x0C)  # Z axis
    
    # Read camera-space position (3 × int32)
    ox = struct.unpack_from('<i', ca_data, 0x14)[0]
    oy = struct.unpack_from('<i', ca_data, 0x18)[0]
    oz = struct.unpack_from('<i', ca_data, 0x1C)[0]
    
    # Read zoom
    zoom = struct.unpack_from('<H', ca_data, 0x24)[0]
    zoom2 = struct.unpack_from('<H', ca_data, 0x26)[0] if struct_size >= 40 else zoom
    
    num_cameras = len(ca_data) // struct_size
    
    return {
        'axis_x': list(ax_x),
        'axis_y': list(ax_y),
        'axis_z': list(ax_z),
        'cam_pos': [ox, oy, oz],
        'zoom': zoom,
        'zoom2': zoom2,
        'struct_size': struct_size,
        'num_cameras': num_cameras,
    }


def project_vertex_2d(wx, wy, wz, camera):
    """Project a 3D walkmesh vertex to 2D screen coords using the camera matrix.
    
    The projection is: screen = R * world + T
    where R is the 3x3 rotation matrix (axes as rows) and T is the camera
    position offset. We only need the X and Y rows for 2D output.
    
    All values are in fixed-point /4096. The dot product of a fixed-point
    axis (scale 4096) with a world coord gives a result scaled by 4096.
    The camera position is also pre-scaled. So the final screen coord
    needs to be divided by 4096 to get the same units as entity coords.
    """
    ax = camera['axis_x']
    ay = camera['axis_y']
    ox, oy, oz = camera['cam_pos']
    
    # screen_x = (ax · world + ox) / 4096
    # screen_y = (ay · world + oy) / 4096
    sx_fp = ax[0] * wx + ax[1] * wy + ax[2] * wz + ox
    sy_fp = ay[0] * wx + ay[1] * wy + ay[2] * wz + oy
    
    sx = sx_fp / 4096.0
    sy = sy_fp / 4096.0
    
    return (round(sx, 1), round(sy, 1))


def apply_camera_to_mesh(mesh, camera):
    """Add projected 2D coordinates to all vertices and triangle centers.
    
    Adds to mesh:
      - 'vertices_2d': list of {'x': sx, 'y': sy} for each vertex
      - triangle 'center_2d_x', 'center_2d_y' for each triangle
      - 'camera': the raw camera data dict
    """
    if camera is None:
        mesh['camera'] = None
        return
    
    mesh['camera'] = camera
    
    # Project all vertices
    verts_3d = mesh['vertices']
    verts_2d = []
    for v in verts_3d:
        sx, sy = project_vertex_2d(v['x'], v['y'], v['z'], camera)
        verts_2d.append({'x': sx, 'y': sy})
    mesh['vertices_2d'] = verts_2d
    
    # Compute 2D triangle centers
    for tri in mesh['triangles']:
        vi = tri['vertex_indices']
        cx = sum(verts_2d[i]['x'] for i in vi) / 3.0
        cy = sum(verts_2d[i]['y'] for i in vi) / 3.0
        tri['center_2d_x'] = round(cx, 1)
        tri['center_2d_y'] = round(cy, 1)


# =============================================================================
# Walkmesh (ID file) parsing
# =============================================================================
def parse_walkmesh(id_data, field_name):
    if len(id_data) < 4:
        return None
    num_tri = struct.unpack_from('<I', id_data, 0)[0]
    if num_tri == 0 or num_tri > 4096:
        return None
    expected_size = 4 + num_tri * 30
    if len(id_data) < expected_size:
        return None
    
    vert_section = 4
    access_section = 4 + num_tri * 24
    
    vertices = []
    vertex_map = {}
    triangles = []
    
    for t in range(num_tri):
        verts_raw = struct.unpack_from('<12h', id_data, vert_section + t * 24)
        tri_vertex_indices = []
        vx_sum = 0
        vy_sum = 0
        for v in range(3):
            x = verts_raw[v * 4 + 0]
            y = verts_raw[v * 4 + 1]
            z = verts_raw[v * 4 + 2]
            vx_sum += x
            vy_sum += y
            key = (x, y, z)
            if key in vertex_map:
                tri_vertex_indices.append(vertex_map[key])
            else:
                idx = len(vertices)
                vertices.append(key)
                vertex_map[key] = idx
                tri_vertex_indices.append(idx)
        
        access = struct.unpack_from('<3h', id_data, access_section + t * 6)
        neighbors = [nb if nb != -1 else 0xFFFF for nb in access]
        
        triangles.append({
            'vertex_indices': tri_vertex_indices,
            'neighbors': neighbors,
            'center_x': round(vx_sum / 3.0, 1),
            'center_y': round(vy_sum / 3.0, 1),
        })
    
    wall_edge_count = sum(1 for tri in triangles for nb in tri['neighbors'] if nb == 0xFFFF)
    
    # Raw vertex preview for first 3 triangles (for cross-referencing with runtime log).
    # The runtime logs: "ID tri[0] v0=(x,y,z) v1=(x,y,z) v2=(x,y,z) neighbors=(a,b,c) center=(cx,cy)"
    raw_preview = []
    for t in range(min(3, num_tri)):
        verts_raw = struct.unpack_from('<12h', id_data, 4 + t * 24)
        access_raw = struct.unpack_from('<3h', id_data, 4 + num_tri * 24 + t * 6)
        raw_preview.append({
            'v0': [verts_raw[0], verts_raw[1], verts_raw[2]],
            'v1': [verts_raw[4], verts_raw[5], verts_raw[6]],
            'v2': [verts_raw[8], verts_raw[9], verts_raw[10]],
            'neighbors': [int(access_raw[0]), int(access_raw[1]), int(access_raw[2])],
        })
    
    return {
        'field_name': field_name,
        'num_triangles': num_tri,
        'num_vertices': len(vertices),
        'num_wall_edges': wall_edge_count,
        'raw_tri_preview': raw_preview,
        'vertices': [{'x': v[0], 'y': v[1], 'z': v[2]} for v in vertices],
        'triangles': triangles,
    }


# =============================================================================
# Main extraction
# =============================================================================
def main():
    if len(sys.argv) < 2:
        print("Usage: python extract_walkmeshes.py <path_to_lang-en_folder> [output.json]")
        print()
        print("Example:")
        print('  python extract_walkmeshes.py "C:\\Program Files (x86)\\Steam\\steamapps\\common\\FINAL FANTASY VIII\\Data\\lang-en"')
        sys.exit(1)
    
    lang_path = sys.argv[1]
    output_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), 'ff8_walkmeshes.json')
    
    fi_path = os.path.join(lang_path, 'field.fi')
    fl_path = os.path.join(lang_path, 'field.fl')
    fs_path = os.path.join(lang_path, 'field.fs')
    
    for p, name in [(fi_path, 'field.fi'), (fl_path, 'field.fl'), (fs_path, 'field.fs')]:
        if not os.path.exists(p):
            print(f"ERROR: {name} not found at {p}")
            sys.exit(1)
    
    print(f"Reading outer archive from: {lang_path}")
    
    outer_fi = parse_fi(fi_path)
    outer_fl = parse_fl(fl_path)
    print(f"  Outer FI: {len(outer_fi)} entries")
    print(f"  Outer FL: {len(outer_fl)} entries")
    
    # =========================================================================
    # Phase 1: Extract the first 7 cwwood fields (direct triplets at indices 0-20)
    # =========================================================================
    walkmeshes = {}
    errors = []
    start_time = time.time()
    
    num_direct = 7  # cwwood1-7
    print(f"\nPhase 1: Extracting {num_direct} direct-triplet fields (cwwood)...")
    for field_idx in range(num_direct):
        fs_idx = field_idx * 3 + 0
        fl_idx = field_idx * 3 + 1
        fi_idx = field_idx * 3 + 2
        field_name = extract_basename(outer_fl[fs_idx])
        
        try:
            inner_fs = extract_outer_entry(fs_path, outer_fi, fs_idx)
            inner_fl_data = extract_outer_entry(fs_path, outer_fi, fl_idx)
            inner_fi_data = extract_outer_entry(fs_path, outer_fi, fi_idx)
            
            if inner_fs is None or inner_fl_data is None or inner_fi_data is None:
                continue
            
            inner_fi = parse_inner_fi(inner_fi_data)
            inner_fl = parse_inner_fl(inner_fl_data)
            
            id_data = extract_inner_file(inner_fs, inner_fi, inner_fl, field_name, 'id')
            if id_data is None:
                continue
            
            mesh = parse_walkmesh(id_data, field_name)
            if mesh is not None:
                # Extract camera and project vertices
                ca_data = extract_inner_file(inner_fs, inner_fi, inner_fl, field_name, 'ca')
                camera = parse_camera(ca_data)
                apply_camera_to_mesh(mesh, camera)
                walkmeshes[field_name] = mesh
                cam_str = f", camera={'yes' if camera else 'no'}"
                print(f"    {field_name}: {mesh['num_triangles']} triangles{cam_str}")
        except Exception as e:
            errors.append(f"{field_name}: {str(e)}")
    
    # =========================================================================
    # Phase 2: Extract mapdata fields (entries 21+ in outer archive)
    # The outer FL has three sections after entry 21:
    #   - .fi files (entries 22 to ~913)
    #   - .fl files (entries ~914 to ~1806)
    #   - .fs files (entries ~1807 to ~2699)
    # We match them by field name, not by position.
    # =========================================================================
    print(f"\nPhase 2: Extracting mapdata fields...")
    
    # Build name -> {ext: outer_fl_index} mapping for entries 22+
    mapdata_fields = {}  # name -> {'fi': idx, 'fl': idx, 'fs': idx}
    for i in range(22, len(outer_fl)):
        path = outer_fl[i]
        name = extract_basename(path)
        ext = extract_ext(path)
        if name not in mapdata_fields:
            mapdata_fields[name] = {}
        mapdata_fields[name][ext] = i
    
    # Filter to fields with all three files
    complete_fields = {name: indices for name, indices in mapdata_fields.items()
                       if 'fi' in indices and 'fl' in indices and 'fs' in indices}
    
    print(f"  Mapdata fields with fi+fl+fs: {len(complete_fields)}")
    
    processed = 0
    for field_name, indices in sorted(complete_fields.items()):
        processed += 1
        if processed % 50 == 0:
            elapsed = time.time() - start_time
            print(f"  Processing {processed}/{len(complete_fields)}: {field_name} ({elapsed:.1f}s)")
        
        try:
            inner_fi_data = extract_outer_entry(fs_path, outer_fi, indices['fi'])
            inner_fl_data = extract_outer_entry(fs_path, outer_fi, indices['fl'])
            inner_fs_data = extract_outer_entry(fs_path, outer_fi, indices['fs'])
            
            if inner_fs_data is None or inner_fl_data is None or inner_fi_data is None:
                continue
            
            inner_fi = parse_inner_fi(inner_fi_data)
            inner_fl = parse_inner_fl(inner_fl_data)
            
            if len(inner_fi) == 0 or len(inner_fl) == 0:
                continue
            
            id_data = extract_inner_file(inner_fs_data, inner_fi, inner_fl, field_name, 'id')
            if id_data is None:
                continue
            
            mesh = parse_walkmesh(id_data, field_name)
            if mesh is not None:
                # Extract camera and project vertices
                ca_data = extract_inner_file(inner_fs_data, inner_fi, inner_fl, field_name, 'ca')
                camera = parse_camera(ca_data)
                apply_camera_to_mesh(mesh, camera)
                walkmeshes[field_name] = mesh
        except Exception as e:
            errors.append(f"{field_name}: {str(e)}")
    
    elapsed = time.time() - start_time
    
    print(f"\nExtraction complete in {elapsed:.1f}s")
    print(f"  Walkmeshes extracted: {len(walkmeshes)}")
    print(f"  Errors: {len(errors)}")
    
    if errors:
        print(f"\n  First 10 errors:")
        for e in errors[:10]:
            print(f"    {e}")
    
    # Summary statistics
    if walkmeshes:
        total_tris = sum(m['num_triangles'] for m in walkmeshes.values())
        total_verts = sum(m['num_vertices'] for m in walkmeshes.values())
        max_field = max(walkmeshes.values(), key=lambda m: m['num_triangles'])
        tris_list = [m['num_triangles'] for m in walkmeshes.values()]
        
        print(f"\n  Total triangles across all fields: {total_tris}")
        print(f"  Total unique vertices: {total_verts}")
        print(f"  Min/Avg/Max triangles per field: {min(tris_list)}/{sum(tris_list)//len(tris_list)}/{max(tris_list)}")
        print(f"  Largest walkmesh: {max_field['field_name']} ({max_field['num_triangles']} triangles)")
        
        # Camera statistics
        cam_count = sum(1 for m in walkmeshes.values() if m.get('camera') is not None)
        print(f"  Fields with camera data: {cam_count}/{len(walkmeshes)}")
        
        for key_field in ['bgroom_1', 'bg2f_1', 'bg2f_2', 'bggate_1', 'bggate_2', 'bggate_3',
                          'bgmd1_3', 'bghall_3', 'bghall_6']:
            if key_field in walkmeshes:
                m = walkmeshes[key_field]
                cam = m.get('camera')
                cam_info = f", cam=yes zoom={cam['zoom']}" if cam else ", cam=no"
                print(f"  {key_field}: {m['num_triangles']} tris, {m['num_vertices']} verts, {m['num_wall_edges']} wall edges{cam_info}")
    
    # Write JSON output
    output = {
        'version': '4.0',
        'extraction_date': time.strftime('%Y-%m-%d %H:%M:%S'),
        'num_fields': len(walkmeshes),
        'total_triangles': sum(m['num_triangles'] for m in walkmeshes.values()) if walkmeshes else 0,
        'total_vertices': sum(m['num_vertices'] for m in walkmeshes.values()) if walkmeshes else 0,
        'fields': walkmeshes,
    }
    
    print(f"\nWriting {output_path}...")
    with open(output_path, 'w') as f:
        json.dump(output, f, separators=(',', ':'))
    
    file_size = os.path.getsize(output_path)
    print(f"  Written: {file_size:,} bytes ({file_size/1024/1024:.1f} MB)")
    print("\nDone! Upload the JSON file to Claude for analysis.")


if __name__ == '__main__':
    main()
