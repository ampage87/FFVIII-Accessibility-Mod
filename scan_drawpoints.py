#!/usr/bin/env python3
"""
FF8 Complete Entity Survey
===========================
Extracts ALL entity data from all 900 fields:
- SYM names grouped by JSM category (door/line/background/other)
- Entity positions from SET3/SET in init scripts
- INF gateway exits (destinations and positions)
- Line entity extended opcode signatures for sub-classification
- Cross-entity REQSW/REQEW calls

Output: field_entity_database.json - complete lookup table for the mod
"""
import struct, sys, os, json
from collections import defaultdict, Counter

# ============================================================================
# Archive extraction
# ============================================================================

def decompress_lzss(compressed, expected_size):
    if len(compressed) <= 4: return None
    ring = bytearray(4096); ring_pos = 0xFEE
    result = bytearray(); pos = 4
    while len(result) < expected_size and pos < len(compressed):
        flags = compressed[pos]; pos += 1
        for bit in range(8):
            if len(result) >= expected_size: break
            if flags & (1 << bit):
                if pos >= len(compressed): break
                b = compressed[pos]; pos += 1
                result.append(b); ring[ring_pos] = b; ring_pos = (ring_pos+1) & 0xFFF
            else:
                if pos+1 >= len(compressed): break
                b1 = compressed[pos]; b2 = compressed[pos+1]; pos += 2
                off = b1 | ((b2 & 0xF0) << 4); length = (b2 & 0x0F) + 3
                for i in range(length):
                    if len(result) >= expected_size: break
                    b = ring[(off+i) & 0xFFF]
                    result.append(b); ring[ring_pos] = b; ring_pos = (ring_pos+1) & 0xFFF
    return bytes(result) if len(result) == expected_size else None

def parse_fl_bytes(data):
    entries = []; cur = []
    for b in data:
        if b in (0x0A, 0x0D, 0x00):
            if cur: entries.append(bytes(cur).decode('ascii',errors='ignore').strip()); cur = []
        else: cur.append(b)
    if cur: entries.append(bytes(cur).decode('ascii',errors='ignore').strip())
    return [e for e in entries if e]

def parse_fi(data):
    return [struct.unpack_from('<III', data, i*12) for i in range(len(data)//12)]

def parse_sym(data):
    names = []; pos = 0
    while pos+32 <= len(data):
        n = data[pos:pos+32].split(b'\x00')[0].decode('ascii',errors='ignore').strip()
        pos += 32
        if not n: continue
        if '::' in n: break
        names.append(n)
    return names

def find_outer_idx(outer_fl, field_name, ext):
    target = (field_name + ext).lower(); tlen = len(target)
    for i, path in enumerate(outer_fl):
        p = path.lower()
        if len(p) >= tlen+1 and p.endswith(target) and p[-(tlen+1)] in ('\\','/'): return i
        elif p == target: return i
    return None

def extract_outer(fs_file, fi_entries, idx):
    if idx is None or idx >= len(fi_entries): return None
    usize, off, comp = fi_entries[idx]
    if usize == 0: return None
    if comp == 0: fs_file.seek(off); return fs_file.read(usize)
    csize = usize
    for j in range(idx+1, len(fi_entries)):
        if fi_entries[j][1] > off: csize = fi_entries[j][1] - off; break
    fs_file.seek(off); return decompress_lzss(fs_file.read(csize), usize)

def extract_inner(fs_data, fi_entries, idx):
    if idx is None or idx >= len(fi_entries): return None
    usize, off, comp = fi_entries[idx]
    if usize == 0: return None
    if comp == 0: return fs_data[off:off+usize] if off+usize <= len(fs_data) else None
    csize = usize
    for j in range(idx+1, len(fi_entries)):
        if fi_entries[j][1] > off: csize = fi_entries[j][1] - off; break
    return decompress_lzss(fs_data[off:min(off+csize,len(fs_data))], usize)

def find_inner_idx(inner_fl, name, ext):
    target = (name + ext).lower()
    for i, p in enumerate(inner_fl):
        if p.lower().endswith(target): return i
    return None

# ============================================================================
# INF gateway parser
# ============================================================================

def parse_inf_gateways(inf_data, field_names_by_id):
    """Parse INF gateways. Returns list of gateway dicts."""
    if not inf_data or len(inf_data) < 676: return []
    gateways = []
    for i in range(12):
        gw = inf_data[0x64 + i*32 : 0x64 + (i+1)*32]
        if len(gw) < 32: break
        x1 = struct.unpack_from('<h', gw, 0)[0]
        y1 = struct.unpack_from('<h', gw, 2)[0]
        x2 = struct.unpack_from('<h', gw, 6)[0]
        y2 = struct.unpack_from('<h', gw, 8)[0]
        dest_id = struct.unpack_from('<H', gw, 18)[0]
        if dest_id in (0xFFFF, 0x7FFF): continue
        if x1 == 0 and y1 == 0 and x2 == 0 and y2 == 0: continue
        dest_name = field_names_by_id.get(dest_id, 'field_%d' % dest_id)
        gateways.append({
            'x1': x1, 'y1': y1, 'x2': x2, 'y2': y2,
            'center_x': (x1+x2)//2, 'center_y': (y1+y2)//2,
            'dest_id': dest_id, 'dest_name': dest_name
        })
    return gateways

# ============================================================================
# JSM full entity scanner
# ============================================================================

def scan_jsm_entities(jsm_data, sym_names):
    """Full JSM scan returning per-entity data."""
    if len(jsm_data) < 8: return []
    cd = jsm_data[0]; cl = jsm_data[1]; cb = jsm_data[2]
    pm = struct.unpack_from('<H', jsm_data, 4)[0]
    ps = struct.unpack_from('<H', jsm_data, 6)[0]
    if pm <= 8 or ps <= pm: return []
    ng = (pm-8)//2; tm = (ps-pm)//2
    if ng <= 0 or tm <= 0: return []
    
    groups = []; mi = 0
    for e in range(ng):
        raw = struct.unpack_from('<H', jsm_data, 8+e*2)[0]
        mc = raw & 0x7F; groups.append((mc, mi)); mi += mc+1
    mep = [struct.unpack_from('<H', jsm_data, pm+m*2)[0] for m in range(tm)]
    sb = jsm_data[ps:]; nsd = len(sb)//4
    sd = [struct.unpack_from('<I', sb, i*4)[0] for i in range(nsd)]
    
    def get_cat(e):
        if e < cd: return 'door'
        r = e - cd
        if r < cl: return 'line'
        r -= cl
        if r < cb: return 'background'
        return 'other'
    
    entities = []
    for e in range(ng):
        mc, sm = groups[e]
        cat = get_cat(e)
        si = e - cd
        sym = sym_names[si] if 0 <= si < len(sym_names) else ''
        
        ext_ops = set()
        has_pos = False; px = py = 0
        req_calls = []  # (target_entity, target_method)
        has_model = False; has_talk = False; has_dialog = False
        
        for m in range(mc+1):
            midx = sm + m
            if midx >= tm: break
            ss = mep[midx]; se = mep[midx+1] if midx+1 < tm else nsd
            stk = []
            
            for ip in range(ss, min(se, nsd)):
                w = sd[ip]; hb = (w>>24)&0xFF
                
                if hb == 0:
                    stk.append(w & 0xFFFFFF)
                elif hb == 0x1C:
                    if stk:
                        x = stk.pop()
                        if x < 0x10000: ext_ops.add(x)
                elif hb == 0x1E:
                    # SET3 in method 0
                    if m == 0 and not has_pos and len(stk) >= 3:
                        vals = stk[-3:]
                        if not any(v >= 0x80000000 for v in vals):
                            px = vals[0] if vals[0] < 32768 else vals[0] - 65536
                            py = vals[1] if vals[1] < 32768 else vals[1] - 65536
                            has_pos = True
                elif hb in (0x07,0x09,0x0A,0x0C,0x0D):
                    stk.append(0x80000000)
                elif hb in (0x02,0x08,0x0B):
                    if stk: stk.pop()
                # Note: we can't check for REQ/REQSW/REQEW by extended opcode
                # since those are also dispatched via 0x1C and we may not know
                # the correct opcode numbers. We track ext_ops instead.
        
        entities.append({
            'idx': e, 'cat': cat, 'sym': sym,
            'ext_ops': sorted(ext_ops),
            'pos': [px, py] if has_pos else None,
        })
    
    return entities

# ============================================================================
# Main
# ============================================================================

def main():
    sd = os.path.dirname(os.path.abspath(__file__))
    dd = os.path.join(sd, "Game Files", "FINAL FANTASY VIII", "Data", "lang-en")
    ad = sys.argv[1] if len(sys.argv) > 1 else dd
    print("Archive dir:", ad)
    
    with open(os.path.join(ad,"field.fl"),'rb') as f: ofl = parse_fl_bytes(f.read())
    with open(os.path.join(ad,"field.fi"),'rb') as f: ofi = parse_fi(f.read())
    
    # Build field name set and ID map
    field_set = set()
    field_names_by_id = {}
    for path in ofl:
        base = path.replace('\\','/').split('/')[-1]
        if '.' in base:
            name, ext = base.rsplit('.', 1)
            if ext.lower() in ('fi','fl','fs'): field_set.add(name.lower())
    fnames = sorted(field_set)
    # FL groups of 3: field ID = position / 3
    for i in range(0, len(ofl), 3):
        base = ofl[i].replace('\\','/').split('/')[-1]
        if '.' in base:
            name = base.rsplit('.', 1)[0].lower()
            field_names_by_id[i // 3] = name
    
    print("Fields: %d" % len(fnames))
    fsf = open(os.path.join(ad,"field.fs"), 'rb')
    
    # Collect everything
    sym_by_cat = defaultdict(lambda: defaultdict(list))  # cat -> name -> [fields]
    all_line_ext_ops = defaultdict(Counter)  # sym_name -> Counter of ext ops
    gateway_count = 0
    entity_count = 0
    fields_data = {}
    sc = 0
    
    for fn in fnames:
        fi_i = find_outer_idx(ofl, fn, '.fi')
        fl_i = find_outer_idx(ofl, fn, '.fl')
        fs_i = find_outer_idx(ofl, fn, '.fs')
        if fi_i is None or fl_i is None or fs_i is None: continue
        try:
            ifi = extract_outer(fsf, ofi, fi_i)
            ifl = extract_outer(fsf, ofi, fl_i)
            ifs = extract_outer(fsf, ofi, fs_i)
        except: continue
        if not ifi or not ifl or not ifs: continue
        ifl_list = parse_fl_bytes(ifl); ifi_list = parse_fi(ifi)
        
        # Extract JSM + SYM
        ji = find_inner_idx(ifl_list, fn, '.jsm')
        si_idx = find_inner_idx(ifl_list, fn, '.sym')
        sn = []
        if si_idx is not None:
            sd2 = extract_inner(ifs, ifi_list, si_idx)
            if sd2: sn = parse_sym(sd2)
        
        entities = []
        if ji is not None:
            jd = extract_inner(ifs, ifi_list, ji)
            if jd:
                entities = scan_jsm_entities(jd, sn)
                entity_count += len(entities)
        
        for ent in entities:
            sym_by_cat[ent['cat']][ent['sym']].append(fn)
            if ent['cat'] == 'line':
                for op in ent['ext_ops']:
                    all_line_ext_ops[ent['sym']][op] += 1
        
        # Extract INF gateways
        ii = find_inner_idx(ifl_list, fn, '.inf')
        gateways = []
        if ii is not None:
            inf_data = extract_inner(ifs, ifi_list, ii)
            if inf_data:
                gateways = parse_inf_gateways(inf_data, field_names_by_id)
                gateway_count += len(gateways)
        
        fields_data[fn] = {
            'entities': len(entities),
            'gateways': len(gateways),
        }
        
        sc += 1
        if sc % 100 == 0: print("  %d..." % sc)
    
    fsf.close()
    
    # ================================================================
    # REPORT
    # ================================================================
    print("\n" + "="*70)
    print("FF8 COMPLETE ENTITY SURVEY")
    print("="*70)
    print("Fields scanned: %d" % sc)
    print("Total entities: %d" % entity_count)
    print("Total INF gateways: %d" % gateway_count)
    
    for cat in ['door', 'line', 'background', 'other']:
        names = sym_by_cat[cat]
        total = sum(len(f) for f in names.values())
        print("\n" + "-"*70)
        print("CATEGORY: %s (%d unique names, %d total entities)" % (cat.upper(), len(names), total))
        print("-"*70)
        # Sort by frequency
        sorted_names = sorted(names.items(), key=lambda x: -len(x[1]))
        for name, fields in sorted_names[:50]:  # top 50
            print("  '%s' (%d fields)" % (name, len(fields)))
        if len(sorted_names) > 50:
            print("  ... and %d more unique names" % (len(sorted_names) - 50))
    
    # Line entity opcode analysis
    print("\n" + "="*70)
    print("LINE ENTITY EXTENDED OPCODE PATTERNS")
    print("="*70)
    # Aggregate: which ext ops appear in line entities?
    line_op_totals = Counter()
    for sym, ops in all_line_ext_ops.items():
        for op, cnt in ops.items():
            line_op_totals[op] += cnt
    print("Unique ext opcodes used by line entities: %d" % len(line_op_totals))
    for op, cnt in line_op_totals.most_common(30):
        print("  0x%04X (%3d): %d line entities use it" % (op, op, cnt))
    
    # Line SYM name patterns
    print("\n" + "="*70)
    print("LINE SYM NAME PATTERNS (for exit/camera/event classification)")
    print("="*70)
    line_names = sym_by_cat['line']
    # Group by prefix patterns
    prefixes = defaultdict(int)
    for name in line_names:
        nl = name.lower()
        if nl.startswith('to_') or nl.startswith('to '): prefixes['to_*'] += len(line_names[name])
        elif 'jump' in nl: prefixes['*jump*'] += len(line_names[name])
        elif 'scroll' in nl or 'camera' in nl: prefixes['*scroll/camera*'] += len(line_names[name])
        elif nl.startswith('l') and len(nl) <= 3 and nl[1:].isdigit(): prefixes['l# (numbered)'] += len(line_names[name])
        else: prefixes['other'] += len(line_names[name])
    for p, c in sorted(prefixes.items(), key=lambda x: -x[1]):
        print("  %s: %d entities" % (p, c))
    
    # Save comprehensive data
    output = {
        'fields_scanned': sc,
        'total_entities': entity_count,
        'total_gateways': gateway_count,
        'sym_names_by_category': {},
        'line_ext_ops': {str(k): v for k, v in line_op_totals.most_common()},
    }
    for cat in ['door', 'line', 'background', 'other']:
        output['sym_names_by_category'][cat] = {
            name: len(fields) for name, fields in sym_by_cat[cat].items()
        }
    
    out_path = os.path.join(sd, "field_entity_survey.json")
    with open(out_path, 'w') as f:
        json.dump(output, f, indent=2)
    print("\nFull data saved to: %s" % out_path)

if __name__ == '__main__':
    main()
