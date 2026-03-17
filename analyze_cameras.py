#!/usr/bin/env python3
"""
Analyze extracted .ca camera files and test projection formula.

Usage:
    python analyze_cameras.py

Reads .ca files from CameraFiles/ directory and prints camera axis data
for key fields. Tests the projection formula against known coordinate
mappings from COORD data.
"""

import struct
import os
import sys
import math

CAMERA_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "CameraFiles")

def parse_ca_file(filepath):
    """Parse a .ca camera file. Returns list of camera entries."""
    with open(filepath, 'rb') as f:
        data = f.read()
    
    entries = []
    size = len(data)
    
    # FF8 .ca files come in two sizes commonly:
    # 38 bytes = 1 camera (no animation, fields with single fixed camera)
    # 80 bytes = 2 cameras (two keyframes for interpolation)
    # Larger = animated camera sequences
    #
    # Each camera frame appears to be either 38 or 40 bytes.
    # Try both and see which produces valid magnitude-4096 vectors.
    
    for frame_size in [40, 38]:
        test_entries = []
        valid = True
        for offset in range(0, size, frame_size):
            if offset + frame_size > size:
                break
            # Parse: 3 axis vectors as int16[3], then other data
            ax = struct.unpack_from('<3h', data, offset + 0)
            ay = struct.unpack_from('<3h', data, offset + 6)
            az = struct.unpack_from('<3h', data, offset + 12)
            
            mag_x = (ax[0]**2 + ax[1]**2 + ax[2]**2)**0.5
            mag_y = (ay[0]**2 + ay[1]**2 + ay[2]**2)**0.5
            mag_z = (az[0]**2 + az[1]**2 + az[2]**2)**0.5
            
            if not (4080 < mag_x < 4120 and 4080 < mag_y < 4120 and 4080 < mag_z < 4120):
                valid = False
                break
            
            # Remaining bytes depend on frame size
            if frame_size == 40:
                extra = struct.unpack_from('<2h', data, offset + 18)
                unk = struct.unpack_from('<h', data, offset + 22)
                pos = struct.unpack_from('<3i', data, offset + 24)
                zoom = struct.unpack_from('<2h', data, offset + 36)
                entry = {
                    'x_axis': ax, 'y_axis': ay, 'z_axis': az,
                    'mag_x': mag_x, 'mag_y': mag_y, 'mag_z': mag_z,
                    'extra': extra, 'unknown': unk[0],
                    'pos': pos, 'zoom': zoom,
                    'frame_size': 40
                }
            elif frame_size == 38:
                # 38 bytes: 18 for axes, then 20 remaining
                # Try: 2 bytes padding + 12 bytes pos + 4 bytes zoom + 2 bytes extra
                # Or: skip padding, read pos as int32[3] at offset 18, zoom at 30
                remaining = data[offset+18:offset+38]
                # Try pos at offset 18 (no padding)
                pos = struct.unpack_from('<3i', data, offset + 18)
                zoom = struct.unpack_from('<2h', data, offset + 30)
                extra2 = struct.unpack_from('<2h', data, offset + 34)
                entry = {
                    'x_axis': ax, 'y_axis': ay, 'z_axis': az,
                    'mag_x': mag_x, 'mag_y': mag_y, 'mag_z': mag_z,
                    'pos': pos, 'zoom': zoom,
                    'extra': extra2, 'unknown': 0,
                    'frame_size': 38,
                    'remaining_hex': remaining.hex()
                }
            
            test_entries.append(entry)
        
        if valid and len(test_entries) > 0:
            entries = test_entries
            break
    
    return entries

def print_camera_entry(name, entry, idx=0):
    """Print a single camera entry."""
    ax = entry['x_axis']
    ay = entry['y_axis']
    az = entry['z_axis']
    pos = entry['pos']
    zoom = entry['zoom']
    
    print(f"  Camera {idx} (frame_size={entry['frame_size']}):")
    print(f"    X-axis (right):   ({ax[0]:6d}, {ax[1]:6d}, {ax[2]:6d})  mag={entry['mag_x']:.1f}")
    print(f"    Y-axis (up):      ({ay[0]:6d}, {ay[1]:6d}, {ay[2]:6d})  mag={entry['mag_y']:.1f}")
    print(f"    Z-axis (forward): ({az[0]:6d}, {az[1]:6d}, {az[2]:6d})  mag={entry['mag_z']:.1f}")
    print(f"    Position:         ({pos[0]:6d}, {pos[1]:6d}, {pos[2]:6d})")
    print(f"    Zoom:             ({zoom[0]:6d}, {zoom[1]:6d})")
    
    # Check orthogonality
    dot_xy = ax[0]*ay[0] + ax[1]*ay[1] + ax[2]*ay[2]
    dot_xz = ax[0]*az[0] + ax[1]*az[1] + ax[2]*az[2]
    dot_yz = ay[0]*az[0] + ay[1]*az[1] + ay[2]*az[2]
    print(f"    Orthogonality: XY={dot_xy} XZ={dot_xz} YZ={dot_yz} (should be ~0)")

def test_projection_formulas(name, entry, test_points):
    """Test various projection formulas against known 3D->2D mappings."""
    ax = [float(v) for v in entry['x_axis']]
    ay = [float(v) for v in entry['y_axis']]
    az = [float(v) for v in entry['z_axis']]
    pos = [float(v) for v in entry['pos']]
    
    print(f"\n  Testing projection formulas for {name}:")
    
    # Build all possible formulas
    axes = {'X': ax, 'Y': ay, 'Z': az}
    
    # For each pair of axes as (screenX_axis, screenY_axis):
    best_formula = None
    best_err = 1e30
    
    for ax_name_1 in ['X', 'Y', 'Z']:
        for ax_name_2 in ['X', 'Y', 'Z']:
            if ax_name_1 == ax_name_2:
                continue
            for negate_1 in [False, True]:
                for negate_2 in [False, True]:
                    for use_pos in [False, True]:
                        a1 = axes[ax_name_1]
                        a2 = axes[ax_name_2]
                        
                        total_err = 0
                        for w3d, expected2d in test_points:
                            p = [w3d[0], w3d[1], w3d[2]]
                            if use_pos:
                                p = [p[0] - pos[0], p[1] - pos[1], p[2] - pos[2]]
                            
                            px = (p[0]*a1[0] + p[1]*a1[1] + p[2]*a1[2]) / 4096.0
                            py = (p[0]*a2[0] + p[1]*a2[1] + p[2]*a2[2]) / 4096.0
                            
                            if negate_1:
                                px = -px
                            if negate_2:
                                py = -py
                            
                            dx = px - expected2d[0]
                            dy = py - expected2d[1]
                            total_err += (dx**2 + dy**2)**0.5
                        
                        avg_err = total_err / len(test_points)
                        label = f"{'!' if negate_1 else ''}{ax_name_1},{'!' if negate_2 else ''}{ax_name_2}"
                        label += " +pos" if use_pos else " nopos"
                        
                        if avg_err < best_err:
                            best_err = avg_err
                            best_formula = label
    
    print(f"    Best formula: {best_formula}  avg_err={best_err:.1f}")
    
    # Show details for the best formula
    # Parse best formula back
    parts = best_formula.split(',')
    neg1 = parts[0].startswith('!')
    neg2 = parts[1].split(' ')[0].startswith('!')
    ax_name_1 = parts[0].lstrip('!')
    ax_name_2_and_pos = parts[1].split(' ')
    ax_name_2 = ax_name_2_and_pos[0].lstrip('!')
    use_pos = '+pos' in best_formula
    
    a1 = axes[ax_name_1]
    a2 = axes[ax_name_2]
    
    print(f"    Details:")
    for w3d, expected2d in test_points:
        p = [w3d[0], w3d[1], w3d[2]]
        if use_pos:
            p = [p[0] - pos[0], p[1] - pos[1], p[2] - pos[2]]
        
        px = (p[0]*a1[0] + p[1]*a1[1] + p[2]*a1[2]) / 4096.0
        py = (p[0]*a2[0] + p[1]*a2[1] + p[2]*a2[2]) / 4096.0
        
        if neg1: px = -px
        if neg2: py = -py
        
        err = ((px - expected2d[0])**2 + (py - expected2d[1])**2)**0.5
        print(f"      3D=({w3d[0]:7.0f},{w3d[1]:7.0f},{w3d[2]:7.0f}) -> "
              f"pred=({px:7.0f},{py:7.0f}) expected=({expected2d[0]:7.0f},{expected2d[1]:7.0f}) err={err:.0f}")

def main():
    if not os.path.isdir(CAMERA_DIR):
        print(f"ERROR: Camera directory not found: {CAMERA_DIR}")
        print("Run extract_ca.py first.")
        sys.exit(1)
    
    # Redirect all output to a log file
    log_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "camera_analysis.txt")
    log_file = open(log_path, 'w', encoding='utf-8')
    original_stdout = sys.stdout
    sys.stdout = log_file
    print(f"Camera analysis log — written to {log_path}")
    
    # Key fields to analyze
    key_fields = [
        'bgroom_1',  # flat classroom (Z≈0, identity transform)
        'bg2f_11',   # Garden 2F corridor (angled, Z=8341) - note: extracted as bg2f_11
        'bghall_1',  # Garden hall (angled?)
        'bghall_4',  # Garden hall section
        'bggate_1',  # Garden front gate
    ]
    
    # Known 3D->2D test points for each field (from COORD data)
    # Format: (3D_x, 3D_y, 3D_z), (2D_x, 2D_y)
    test_data = {
        'bgroom_1': [
            # Flat field: 2D ≈ 3D (identity)
            ((537, 432, 0), (537, 432)),
            ((539, -433, 0), (539, -433)),
            ((817, -2851, 22), (817, -2851)),
        ],
        'bg2f_11': [
            # Angled field (bg2f_1 in game, bg2f_11 in archive)
            # From v06.12 triangle center COORD data
            ((-33.3, -2438.7, 8341), (0, -2437)),
            ((271.3, -2884.3, 8341), (247, -2879)),
            ((271.7, -3415.3, 8341), (247, -3312)),
            ((218.3, -3529.0, 8341), (247, -3498)),
            ((271.7, -4112.7, 8341), (258, -4225)),
            ((218.3, -3879.7, 8341), (258, -3978)),
            ((-61.7, -4113.3, 8341), (-131, -4012)),
            ((-276.3, -3881.0, 8341), (-287, -3978)),
            ((-223.7, -3763.0, 8341), (-287, -3730)),
        ],
        'bghall_4': [
            # From v06.13 COORD data
            ((66.0, -2722.3, 1344), (26, -2703)),
            ((-45.0, -2233.7, 1347), (-14, -2238)),
            ((-45.0, -2383.0, 1347), (-14, -2355)),
            ((-91.0, -2433.0, 1347), (-14, -2472)),
            ((-53.3, -2658.7, 1344), (-14, -2628)),
            ((-112.7, -2722.3, 1344), (-14, -2784)),
            ((-55.0, -3066.3, 1344), (-14, -3019)),
        ],
    }
    
    print("=" * 70)
    print("FF8 Camera Analysis")
    print("=" * 70)
    
    # List all available .ca files
    ca_files = sorted([f for f in os.listdir(CAMERA_DIR) if f.endswith('.ca')])
    print(f"\nTotal .ca files available: {len(ca_files)}")
    
    # File size distribution
    sizes = {}
    for f in ca_files:
        size = os.path.getsize(os.path.join(CAMERA_DIR, f))
        sizes[size] = sizes.get(size, 0) + 1
    print(f"Size distribution:")
    for size in sorted(sizes.keys()):
        print(f"  {size:5d} bytes: {sizes[size]:3d} files")
    
    print()
    
    for field_name in key_fields:
        ca_path = os.path.join(CAMERA_DIR, f"{field_name}.ca")
        if not os.path.exists(ca_path):
            print(f"\n{'='*70}")
            print(f"Field: {field_name} — NOT FOUND")
            continue
        
        print(f"\n{'='*70}")
        print(f"Field: {field_name}")
        print(f"  File size: {os.path.getsize(ca_path)} bytes")
        
        entries = parse_ca_file(ca_path)
        if not entries:
            print("  FAILED to parse camera entries (no valid magnitude-4096 vectors found)")
        
        # Always dump raw hex and int16 values for inspection
        with open(ca_path, 'rb') as f:
            raw = f.read()
        print(f"  Raw hex ({len(raw)} bytes): {raw[:80].hex()}")
        print(f"  All int16 values:")
        for bi in range(0, min(len(raw), 80), 2):
            val = struct.unpack_from('<h', raw, bi)[0]
            print(f"    offset {bi:3d}: {val:7d}")
        
        if not entries:
            continue
        
        print(f"  Parsed {len(entries)} camera entries")
        for i, entry in enumerate(entries):
            print_camera_entry(field_name, entry, i)
        
        # Test projection if we have test data
        if field_name in test_data:
            test_projection_formulas(field_name, entries[0], test_data[field_name])
    
    print(f"\n{'='*70}")
    print("Analysis complete.")
    
    # Close log and restore stdout
    sys.stdout = original_stdout
    log_file.close()
    print(f"Analysis written to: {log_path}")
    print("Upload this file to Claude for analysis.")

if __name__ == '__main__':
    main()
