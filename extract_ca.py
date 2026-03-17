#!/usr/bin/env python3
"""
Extract ALL .ca camera files from FF8's two-level field archive.

Usage:
    python extract_ca.py [game_data_path]

Extracts every field's .ca camera file into a CameraFiles/ subdirectory.
Requires the game's field.fi/fl/fs archives.
Default path: C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VIII\Data\lang-en
"""

import struct
import os
import sys
import zlib

# Default Steam game path — adjust if your install is elsewhere
GAME_DATA = r"C:\Program Files (x86)\Steam\steamapps\common\FINAL FANTASY VIII\Data\lang-en"

def read_fi_entries(fi_data):
    """Parse fi index file: 12-byte entries (uint32 size, uint32 offset, uint32 compression)."""
    entries = []
    for i in range(0, len(fi_data), 12):
        if i + 12 > len(fi_data):
            break
        size, offset, comp = struct.unpack_from('<III', fi_data, i)
        entries.append((size, offset, comp))
    return entries

def read_fl_names(fl_data):
    """Parse fl file list: newline-separated paths."""
    text = fl_data.decode('ascii', errors='replace')
    return [line.strip() for line in text.split('\n') if line.strip()]

def decompress_lzs(data):
    """Decompress FF8 LZS (LZSS variant) compressed data."""
    if len(data) < 4:
        return data
    dec_size = struct.unpack_from('<I', data, 0)[0]
    src = 4
    dst = bytearray()
    
    while src < len(data) and len(dst) < dec_size:
        flags = data[src]
        src += 1
        for bit in range(8):
            if src >= len(data) or len(dst) >= dec_size:
                break
            if flags & (1 << bit):
                dst.append(data[src])
                src += 1
            else:
                if src + 1 >= len(data):
                    break
                b0 = data[src]
                b1 = data[src + 1]
                src += 2
                back_offset = ((b1 & 0xF0) << 4) | b0
                length = (b1 & 0x0F) + 3
                pos = len(dst) - ((len(dst) - back_offset - 18) & 0xFFF)
                for j in range(length):
                    if pos + j >= 0 and pos + j < len(dst):
                        dst.append(dst[pos + j])
                    else:
                        dst.append(0)
    
    return bytes(dst)

def extract_entry(fs_data, fi_entries, idx):
    """Extract a single entry from an fi/fs archive pair."""
    if idx >= len(fi_entries):
        return None
    size, offset, comp = fi_entries[idx]
    if size == 0:
        return None
    if offset + size > len(fs_data):
        return None
    raw = fs_data[offset:offset + size]
    if comp == 0:
        return raw
    elif comp == 2:
        try:
            return zlib.decompress(raw)
        except:
            return raw
    elif comp == 1:
        return decompress_lzs(raw)
    return raw

def get_basename(path):
    """Extract filename from a path with either backslash or forward slash."""
    return path.replace('\\', '/').split('/')[-1].lower()

def main():
    game_data = GAME_DATA
    if len(sys.argv) > 1:
        game_data = sys.argv[1]
    
    script_dir = os.path.dirname(os.path.abspath(__file__))
    output_dir = os.path.join(script_dir, "CameraFiles")
    os.makedirs(output_dir, exist_ok=True)
    
    fi_path = os.path.join(game_data, "field.fi")
    fl_path = os.path.join(game_data, "field.fl")
    fs_path = os.path.join(game_data, "field.fs")
    
    for p in [fi_path, fl_path, fs_path]:
        if not os.path.exists(p):
            print(f"ERROR: File not found: {p}")
            print(f"Adjust GAME_DATA path at top of script or pass it as argument.")
            sys.exit(1)
    
    print(f"Reading outer archive from: {game_data}")
    
    with open(fi_path, 'rb') as f:
        outer_fi_data = f.read()
    with open(fl_path, 'rb') as f:
        outer_fl_data = f.read()
    with open(fs_path, 'rb') as f:
        outer_fs_data = f.read()
    
    outer_fi_entries = read_fi_entries(outer_fi_data)
    outer_fl_names = read_fl_names(outer_fl_data)
    
    print(f"Outer archive: {len(outer_fi_entries)} entries, {len(outer_fl_names)} names")
    
    # Build a map of field names to their outer archive indices.
    # The outer archive groups files in triples: fieldname.fi, fieldname.fl, fieldname.fs
    # We scan all names and group by the field name prefix.
    field_archives = {}  # field_name -> {fi: idx, fl: idx, fs: idx}
    
    for i, name in enumerate(outer_fl_names):
        bn = get_basename(name)
        if bn.endswith('.fi') or bn.endswith('.fl') or bn.endswith('.fs'):
            field_name = bn.rsplit('.', 1)[0]
            ext = bn.rsplit('.', 1)[1]
            if field_name not in field_archives:
                field_archives[field_name] = {}
            field_archives[field_name][ext] = i
    
    print(f"Found {len(field_archives)} field archive groups")
    
    # Now extract .ca from each field's inner archive
    extracted = 0
    failed = 0
    skipped = 0
    
    for field_name in sorted(field_archives.keys()):
        indices = field_archives[field_name]
        if 'fi' not in indices or 'fl' not in indices or 'fs' not in indices:
            skipped += 1
            continue
        
        # Extract inner fi/fl/fs
        inner_fi_data = extract_entry(outer_fs_data, outer_fi_entries, indices['fi'])
        inner_fl_data = extract_entry(outer_fs_data, outer_fi_entries, indices['fl'])
        inner_fs_data = extract_entry(outer_fs_data, outer_fi_entries, indices['fs'])
        
        if not inner_fi_data or not inner_fl_data or not inner_fs_data:
            failed += 1
            continue
        
        # Parse inner archive
        inner_fi_entries = read_fi_entries(inner_fi_data)
        inner_fl_names = read_fl_names(inner_fl_data)
        
        # Find .ca file in inner archive
        ca_idx = -1
        for j, iname in enumerate(inner_fl_names):
            ibn = get_basename(iname)
            if ibn.endswith('.ca'):
                ca_idx = j
                break
        
        if ca_idx < 0:
            # Some fields may not have a .ca file
            skipped += 1
            continue
        
        if ca_idx >= len(inner_fi_entries):
            failed += 1
            continue
        
        ca_data = extract_entry(inner_fs_data, inner_fi_entries, ca_idx)
        if not ca_data or len(ca_data) == 0:
            failed += 1
            continue
        
        # Write to output
        out_path = os.path.join(output_dir, f"{field_name}.ca")
        with open(out_path, 'wb') as f:
            f.write(ca_data)
        extracted += 1
    
    print(f"\nResults:")
    print(f"  Extracted: {extracted} .ca files")
    print(f"  Failed:    {failed}")
    print(f"  Skipped:   {skipped} (missing fi/fl/fs triple or no .ca in inner archive)")
    print(f"  Output:    {output_dir}")
    
    # Quick summary of some key fields
    print(f"\nKey field camera files:")
    for fn in ['bg2f_1', 'bgroom_1', 'bghall_1', 'bghall_4', 'bggate_1', 'bggate_2']:
        ca_path = os.path.join(output_dir, f"{fn}.ca")
        if os.path.exists(ca_path):
            size = os.path.getsize(ca_path)
            print(f"  {fn}.ca: {size} bytes")
        else:
            print(f"  {fn}.ca: NOT FOUND")

if __name__ == '__main__':
    main()
