#!/usr/bin/env python3
"""
FF8 Interactive Object Interaction Range Scanner
=================================================
For specific fields, extracts all interaction-related parameters from JSM scripts:
- TALKRADIUS / PUSHRADIUS values
- TALKONOFF / PUSHONOFF / THROUGHONOFF flags
- SET3 positions
- SETLINE trigger line coordinates
- Cross-entity REQ calls

Goal: understand how the game determines interaction range for objects like
the Directory, classroom rules, Squall's desk, and dormitory bed.
"""
import struct, sys, os, json
from collections import defaultdict

# Target fields to analyze
TARGET_FIELDS = [
    'bghall_1',   # B-Garden Hall (Directory)
    'bgroom_1',   # B-Garden Classroom (rules list + desk)
    'bgroom_2',   # B-Garden Classroom 2 (if different)
    'bgdorm_1',   # B-Garden Dormitory (Squall's bed)
    'bgdorm_2',   # Alternate dorm
    'bggate_2',   # Front Gate (draw point + guard, for reference)
    'bggate_6',   # Front Gate (NPC, for reference)
]

# Extended opcode names (dispatched via native 0x1C)
EXT_OP_NAMES = {
    41: 'MAPJUMP', 42: 'MAPJUMP3',
    57: 'HIDE', 58: 'SHOW',
    59: 'SETPC',
    71: 'MES', 72: 'MESSYNC', 74: 'ASK',
    76: 'BGDRAW', 77: 'BGOFF',
    89: 'BGANIME',
    93: 'PUSHONOFF', 95: 'TALKONOFF', 96: 'THROUGHONOFF',
    97: 'SETMODEL', 98: 'TALKRADIUS', 99: 'PUSHRADIUS',
    100: 'SETPARTY',
    103: 'ADDPARTY', 104: 'SUBPARTY',
    149: 'MENUSAVE', 150: 'SAVEENABLE',
    151: 'MENUSHOP',
    153: 'DRAWPOINT', 154: 'SETDRAWPOINT',
    165: 'CARDGAME',
    175: 'DOORLINEON', 176: 'DOORLINEOFF',
    208: 'LADDERUP', 209: 'LADDERDOWN',
    210: 'BATTLE',
    221: 'ADDITEM',
    228: 'REQSW', 229: 'REQEW',
    281: 'USE', 282: 'UNUSE',
    305: 'MUSICCHANGE',
    307: 'MENUPHS', 308: 'MENUNAME',
    311: 'SETLINE',
    322: 'LINEON', 323: 'LINEOFF',
    324: 'WAIT',
}

# ============================================================================
# Archive extraction (same as scan_drawpoints.py)
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
# Detailed JSM scanner — extracts interaction parameters
# ============================================================================

def signed16(v):
    return v - 65536 if v >= 32768 else v

def scan_jsm_detailed(jsm_data, sym_names, field_name):
    """Detailed JSM scan extracting interaction parameters for each entity."""
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
        si = e - cd  # SYM index (excludes doors)
        sym = sym_names[si] if 0 <= si < len(sym_names) else ''
        
        ent = {
            'jsm_idx': e,
            'cat': cat,
            'sym': sym,
            'methods': mc + 1,
            'talk_radius': None,
            'push_radius': None,
            'talkonoff': None,
            'pushonoff': None,
            'throughonoff': None,
            'set3_pos': None,
            'setline': None,
            'model': None,
            'ext_ops_used': [],
            'req_calls': [],
            'all_opcodes': [],  # for debugging
        }
        
        # Scan ALL methods (not just init)
        for m in range(mc+1):
            midx = sm + m
            if midx >= tm: break
            ss = mep[midx]
            se = mep[midx+1] if midx+1 < tm else nsd
            stk = []
            
            for ip in range(ss, min(se, nsd)):
                w = sd[ip]; hb = (w >> 24) & 0xFF; param = w & 0xFFFFFF
                
                if hb == 0:  # PUSH literal
                    # Sign-extend 24-bit
                    val = param if param < 0x800000 else param - 0x1000000
                    stk.append(val)
                    
                elif hb == 0x1C:  # Extended dispatch
                    ext_op = stk.pop() if stk else -1
                    op_name = EXT_OP_NAMES.get(ext_op, 'EXT_%d' % ext_op)
                    
                    if ext_op not in [e2 for e2, _ in ent['ext_ops_used']]:
                        ent['ext_ops_used'].append((ext_op, op_name))
                    
                    if ext_op == 98:  # TALKRADIUS
                        val = stk[-1] if stk else None
                        if val is not None and ent['talk_radius'] is None:
                            ent['talk_radius'] = val
                            ent['all_opcodes'].append('m%d: TALKRADIUS(%d)' % (m, val))
                    
                    elif ext_op == 99:  # PUSHRADIUS
                        val = stk[-1] if stk else None
                        if val is not None and ent['push_radius'] is None:
                            ent['push_radius'] = val
                            ent['all_opcodes'].append('m%d: PUSHRADIUS(%d)' % (m, val))
                    
                    elif ext_op == 95:  # TALKONOFF
                        val = stk[-1] if stk else None
                        if val is not None:
                            ent['talkonoff'] = val
                            ent['all_opcodes'].append('m%d: TALKONOFF(%d)' % (m, val))
                    
                    elif ext_op == 93:  # PUSHONOFF
                        val = stk[-1] if stk else None
                        if val is not None:
                            ent['pushonoff'] = val
                            ent['all_opcodes'].append('m%d: PUSHONOFF(%d)' % (m, val))
                    
                    elif ext_op == 96:  # THROUGHONOFF
                        val = stk[-1] if stk else None
                        if val is not None:
                            ent['throughonoff'] = val
                            ent['all_opcodes'].append('m%d: THROUGHONOFF(%d)' % (m, val))
                    
                    elif ext_op == 97:  # SETMODEL
                        val = stk[-1] if stk else None
                        if val is not None:
                            ent['model'] = val
                            ent['all_opcodes'].append('m%d: SETMODEL(%d)' % (m, val))
                    
                    elif ext_op == 311:  # SETLINE
                        # SETLINE takes 6 params from stack: x1,y1,z1,x2,y2,z2
                        if len(stk) >= 6:
                            vals = stk[-6:]
                            ent['setline'] = {
                                'x1': signed16(vals[0] & 0xFFFF), 'y1': signed16(vals[1] & 0xFFFF),
                                'z1': signed16(vals[2] & 0xFFFF),
                                'x2': signed16(vals[3] & 0xFFFF), 'y2': signed16(vals[4] & 0xFFFF),
                                'z2': signed16(vals[5] & 0xFFFF),
                            }
                            ent['all_opcodes'].append('m%d: SETLINE(%s)' % (m, ent['setline']))
                    
                    elif ext_op in (228, 229):  # REQSW / REQEW
                        if len(stk) >= 2:
                            target_ent = stk[-2] if len(stk) >= 2 else -1
                            target_method = stk[-1] if stk else -1
                            ent['req_calls'].append({
                                'type': 'REQSW' if ext_op == 228 else 'REQEW',
                                'target_entity': target_ent,
                                'target_method': target_method,
                                'in_method': m,
                            })
                            ent['all_opcodes'].append('m%d: %s(ent=%d, method=%d)' % (
                                m, 'REQSW' if ext_op == 228 else 'REQEW', target_ent, target_method))
                    
                    # Pop consumed params (rough — most ext ops consume some stack)
                    # This is imprecise but good enough for parameter extraction
                    
                elif hb == 0x1E:  # SET3 (native)
                    if m == 0 and len(stk) >= 4:
                        # SET3 params: ..., X, Y, Z, triId (top of stack)
                        # But some have more params. Take last 4+ values.
                        vals = stk[-7:] if len(stk) >= 7 else stk[:]
                        # Check for PSHM_W markers (>= 0x80000000 unsigned)
                        has_pshm = any((v & 0xFFFFFFFF) >= 0x80000000 for v in vals if v is not None)
                        if not has_pshm and len(vals) >= 2:
                            ent['set3_pos'] = {
                                'x': signed16(vals[-4] & 0xFFFF) if len(vals) >= 4 else 0,
                                'y': signed16(vals[-3] & 0xFFFF) if len(vals) >= 3 else 0,
                                'pshm': False,
                            }
                        else:
                            ent['set3_pos'] = {
                                'raw_stack': [hex(v & 0xFFFFFFFF) for v in vals],
                                'pshm': True,
                            }
                        ent['all_opcodes'].append('m%d: SET3(stack=%s)' % (m, [hex(v & 0xFFFFFFFF) for v in vals]))
                
                elif hb == 0x07:  # PSHN_L - push number literal (24-bit param IS the value)
                    val = param if param < 0x800000 else param - 0x1000000
                    stk.append(val)
                elif hb == 0x09:  # PSHI_L - push immediate literal
                    val = param if param < 0x800000 else param - 0x1000000
                    stk.append(val)
                elif hb in (0x0A,):  # POPI_L - pop to local
                    if stk: stk.pop()
                elif hb in (0x0C, 0x0D):  # PSHM_W / POPM_W - memory ops
                    if hb == 0x0C:
                        stk.append(0x80000000 | param)  # unknown runtime value
                    else:
                        if stk: stk.pop()
                elif hb in (0x02, 0x08, 0x0B):  # Pops
                    if stk: stk.pop()
                elif hb == 0x01:  # CAL (binary op)
                    if len(stk) >= 2: stk.pop(); stk.pop(); stk.append(0x80000000)
                elif hb == 0x03:  # JMP variants  
                    pass
                elif hb == 0x05:  # JNE
                    if stk: stk.pop()
                elif hb == 0x06:  # JMP
                    pass
        
        entities.append(ent)
    
    return entities, cd, cl, cb

# ============================================================================
# INF gateway parser
# ============================================================================

def parse_inf_gateways(inf_data, field_names_by_id):
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
            'line': '(%d,%d)->(%d,%d)' % (x1, y1, x2, y2),
            'center': '(%d,%d)' % ((x1+x2)//2, (y1+y2)//2),
            'dest_id': dest_id, 'dest_name': dest_name
        })
    return gateways

# ============================================================================
# Main
# ============================================================================

def main():
    sd = os.path.dirname(os.path.abspath(__file__))
    dd = os.path.join(sd, "Game Files", "FINAL FANTASY VIII", "Data", "lang-en")
    ad = sys.argv[1] if len(sys.argv) > 1 else dd
    print("Archive dir:", ad)
    
    with open(os.path.join(ad, "field.fl"), 'rb') as f: ofl = parse_fl_bytes(f.read())
    with open(os.path.join(ad, "field.fi"), 'rb') as f: ofi = parse_fi(f.read())
    
    # Build field name map
    field_names_by_id = {}
    for i in range(0, len(ofl), 3):
        base = ofl[i].replace('\\', '/').split('/')[-1]
        if '.' in base:
            name = base.rsplit('.', 1)[0].lower()
            field_names_by_id[i // 3] = name
    
    fsf = open(os.path.join(ad, "field.fs"), 'rb')
    
    results = {}
    
    for fn in TARGET_FIELDS:
        fi_i = find_outer_idx(ofl, fn, '.fi')
        fl_i = find_outer_idx(ofl, fn, '.fl')
        fs_i = find_outer_idx(ofl, fn, '.fs')
        if fi_i is None or fl_i is None or fs_i is None:
            print("  %s: NOT FOUND" % fn)
            continue
        
        try:
            ifi = extract_outer(fsf, ofi, fi_i)
            ifl = extract_outer(fsf, ofi, fl_i)
            ifs = extract_outer(fsf, ofi, fs_i)
        except Exception as ex:
            print("  %s: ERROR %s" % (fn, ex))
            continue
        if not ifi or not ifl or not ifs:
            print("  %s: empty archive data" % fn)
            continue
        
        ifl_list = parse_fl_bytes(ifl)
        ifi_list = parse_fi(ifi)
        
        # SYM
        si_idx = find_inner_idx(ifl_list, fn, '.sym')
        sym_names = []
        if si_idx is not None:
            sd2 = extract_inner(ifs, ifi_list, si_idx)
            if sd2: sym_names = parse_sym(sd2)
        
        # JSM
        ji = find_inner_idx(ifl_list, fn, '.jsm')
        entities = []
        cd = cl = cb = 0
        if ji is not None:
            jd = extract_inner(ifs, ifi_list, ji)
            if jd:
                entities, cd, cl, cb = scan_jsm_detailed(jd, sym_names, fn)
        
        # INF
        ii = find_inner_idx(ifl_list, fn, '.inf')
        gateways = []
        if ii is not None:
            inf_data = extract_inner(ifs, ifi_list, ii)
            if inf_data:
                gateways = parse_inf_gateways(inf_data, field_names_by_id)
        
        # Report
        print("\n" + "=" * 70)
        print("FIELD: %s" % fn)
        print("=" * 70)
        print("JSM header: doors=%d lines=%d bg=%d others=%d" % (cd, cl, cb, len(entities) - cd - cl - cb))
        print("SYM names: %d" % len(sym_names))
        print("INF gateways: %d" % len(gateways))
        for gw in gateways:
            print("  GW: %s -> %s (%s)" % (gw['line'], gw['dest_name'], gw['center']))
        
        print("\nENTITIES WITH INTERACTION DATA:")
        print("-" * 70)
        
        for ent in entities:
            # Show entities that have any interaction-related opcodes
            has_interaction = (
                ent['talk_radius'] is not None or
                ent['push_radius'] is not None or
                ent['talkonoff'] is not None or
                ent['pushonoff'] is not None or
                ent['throughonoff'] is not None or
                ent['setline'] is not None or
                ent['set3_pos'] is not None or
                ent['req_calls']
            )
            
            if not has_interaction and ent['cat'] in ('door', 'line'):
                continue  # Skip non-interactive doors/lines to reduce noise
            
            print("\n  ent%d [%s] sym='%s' (methods=%d)" % (
                ent['jsm_idx'], ent['cat'], ent['sym'], ent['methods']))
            
            if ent['model'] is not None:
                print("    MODEL: %d" % ent['model'])
            if ent['set3_pos'] is not None:
                if ent['set3_pos'].get('pshm'):
                    print("    SET3: PSHM_W %s" % ent['set3_pos'].get('raw_stack', '?'))
                else:
                    print("    SET3: pos=(%d, %d)" % (ent['set3_pos']['x'], ent['set3_pos']['y']))
            if ent['talk_radius'] is not None:
                print("    TALKRADIUS: %d" % ent['talk_radius'])
            if ent['push_radius'] is not None:
                print("    PUSHRADIUS: %d" % ent['push_radius'])
            if ent['talkonoff'] is not None:
                print("    TALKONOFF: %d" % ent['talkonoff'])
            if ent['pushonoff'] is not None:
                print("    PUSHONOFF: %d" % ent['pushonoff'])
            if ent['throughonoff'] is not None:
                print("    THROUGHONOFF: %d" % ent['throughonoff'])
            if ent['setline'] is not None:
                sl = ent['setline']
                print("    SETLINE: (%d,%d,%d)->(%d,%d,%d) center=(%d,%d)" % (
                    sl['x1'], sl['y1'], sl['z1'], sl['x2'], sl['y2'], sl['z2'],
                    (sl['x1']+sl['x2'])//2, (sl['y1']+sl['y2'])//2))
            if ent['req_calls']:
                for rc in ent['req_calls']:
                    print("    %s -> ent%d method%d (in m%d)" % (
                        rc['type'], rc['target_entity'], rc['target_method'], rc['in_method']))
            
            # Show extended ops used (compact)
            ops_str = ', '.join('%s(%d)' % (name, op) for op, name in ent['ext_ops_used'])
            if ops_str:
                print("    EXT_OPS: %s" % ops_str)
        
        # Save per-field data
        results[fn] = {
            'header': {'doors': cd, 'lines': cl, 'bg': cb, 'others': len(entities) - cd - cl - cb},
            'sym_names': sym_names,
            'gateways': gateways,
            'entities': [{
                'idx': e['jsm_idx'], 'cat': e['cat'], 'sym': e['sym'],
                'talk_radius': e['talk_radius'], 'push_radius': e['push_radius'],
                'talkonoff': e['talkonoff'], 'pushonoff': e['pushonoff'],
                'throughonoff': e['throughonoff'],
                'set3_pos': e['set3_pos'], 'setline': e['setline'],
                'model': e['model'],
                'req_calls': e['req_calls'],
            } for e in entities],
        }
    
    fsf.close()
    
    # Save results
    out_path = os.path.join(sd, "interaction_range_data.json")
    with open(out_path, 'w') as f:
        json.dump(results, f, indent=2)
    print("\n\nResults saved to: %s" % out_path)

if __name__ == '__main__':
    main()
