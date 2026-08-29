"""Offline JSM entity scanner -- engine-accurate decode and ordering.

INSTRUCTION DECODE (decoder at FF8_EN.exe 0x00530760, dispatch at 0x0052A647):

    w = code[ip]
    if (w & 0xFF000000) == 0:  opcode = w          ; no parameter
    else:                      opcode = w >> 24    ; parameter = sign_extend24(w)

    call opcodeTable[opcode]   with opcodeTable at 0x00B8DE94

So a word whose high byte is zero is an OPCODE, not a literal push -- which is
how opcodes above 0xFF (MENUSAVE 0x12E, ADDITEM 0x125, SETDRAWPOINT 0x155, ...)
are encoded. Literals reach the VM stack only through PSHN_L.

PUSH OPCODES (handlers read straight off the table):
    0x07 PSHN_L  0x0051C990  pushes the INLINE PARAM itself  -- a literal
    0x08 PSHL    0x0051CAB0  pushes local  [ctx + n*4 + 0x140]
    0x09 POPL    0x0051CC70  pops to local
    0x0A PSHM_B  0x0051CAF0  pushes byte from the field var bank 0x01CFE9B8
    0x0C PSHM_W  pushes word from the same bank

ARGUMENT EXTRACTION: arguments are read from the contiguous run of push opcodes
immediately preceding the consumer, rather than from a simulated stack carried
across the whole method. The compiler emits them that way, and it cannot drift.

CONSUMERS verified against their handlers:
    SET3 0x1E  @0x0051D780  pops z,y,x into [0x198]/[0x194]/[0x190] (<<12);
                            the INLINE PARAM is the walkmesh triangle -> [0x1FA]
    SETLINE 0x39 @0x0051DC30 pops 6 words into [0x192]..[0x188]
    MAPJUMP  0x29 @0x00521A20 pops 4; the DEEPEST is the destination field
                            ([0x01CE4762]); the inline param is the entrance id
                            ([0x01CE476C] -> 0x02036B4D)
    MAPJUMP3 0x2A @0x00521AC0 same, popping 5
"""
import ffield

OP = dict(PSHN_L=0x07, PSHL=0x08, POPL=0x09, PSHM_B=0x0A, POPM_B=0x0B,
          PSHM_W=0x0C, POPM_W=0x0D,
          SET=0x01D, SET3=0x01E, SETLINE=0x039, SETMODEL=0x02B,
          TALKRADIUS=0x062, PUSHRADIUS=0x063, TALKON=0x057, TALKOFF=0x058,
          MAPJUMP=0x029, MAPJUMP3=0x02A, MAPJUMPO=0x05C, DISCJUMP=0x038,
          SETDRAWPOINT=0x155, DRAWPOINT=0x137, MENUSAVE=0x12E, SAVEENABLE=0x12F,
          PHSENABLE=0x130, MENUSHOP=0x11E, CARDGAME=0x13A,
          LADDERUP=0x025, LADDERDOWN=0x026, LADDERUP2=0x027, LADDERDOWN2=0x028,
          SHOW=0x060, HIDE=0x061, UNUSE=0x01A, USE=0x0E5,
          ADDITEM=0x125, WORLDMAPJUMP=0x10D, DOORLINEON=0x143, DOORLINEOFF=0x142,
          BGDRAW=0x099, BGOFF=0x09A, BGANIME=0x095,
          MES=0x047, ASK=0x04A, AMES=0x065, AASK=0x06F, BATTLE=0x069, MOVE=0x03E,
          REQ=0x014, REQSW=0x015, REQEW=0x016)

# Push opcodes, derived by walking every handler in the 0x00B8DE94 table and
# counting writes to the VM stack pointer at [ctx+0x184]:
#   0x04 CALL      pushes the return IP        (run terminator, not an argument)
#   0x05 prologue  pushes all EIGHT locals     (run terminator, not an argument)
#   0x07 PSHN_L    pushes the inline param     -- LITERAL
#   0x13           pushes the inline param     -- LITERAL (0x0051CD30)
#   0x08 PSHL      pushes a local
#   0x0A PSHM_B / 0x0C PSHM_W / 0x0E PSHM_L    push from the field var bank
#   0x10 0x11 0x12                             push, value not statically known
PUSH_OPS = {0x07, 0x13, 0x08, 0x0A, 0x0C, 0x0E, 0x10, 0x11, 0x12}
LITERAL_PUSH = {0x07, 0x13}

class Ent:
    __slots__ = ('group','slot','cat','name','methods','flags','mapjumps','setline',
                 'pos','tri','reqSlots','drawId','shopId','model','posMethod')
    def __init__(self):
        self.flags=set(); self.mapjumps=[]; self.setline=None; self.pos=None
        self.tri=None; self.reqSlots=[]; self.drawId=None; self.shopId=None
        self.model=None; self.posMethod=None

def decode(code):
    """-> [(opcode, param_or_None)] using the engine's decoder."""
    out=[]
    for w in code:
        if (w & 0xFF000000) == 0:
            out.append((w, None))
        else:
            p = w & 0xFFFFFF
            if p & 0x800000: p -= 0x1000000
            out.append((w >> 24, p))
    return out

def _args(ins, i, n):
    """The n arguments the opcode at i consumes: the contiguous preceding run of
    push opcodes, deepest first. None for a value that came from memory."""
    run=[]; k=i-1
    while k >= 0 and ins[k][0] in PUSH_OPS and len(run) < n:
        op, par = ins[k]
        run.insert(0, par if op in LITERAL_PUSH else None)
        k -= 1
    return run if len(run) == n else None

def scan(f, j=None):
    if j is None: j = ffield.Jsm(f)
    if not getattr(j,'ok',False): return None
    gn = ffield.group_names(f, j)
    ents=[]
    for g in range(j.ngrp):
        e=Ent(); e.group=g; e.slot=j.group_to_slot(g); e.cat=j.cat(g); e.name=gn[g]
        cnt,_ = j.groups[g]; e.methods=cnt
        for m in range(-1, cnt):
            r = j.method_range(g, m)
            if not r: continue
            _method(e, decode(j.code[r[0]:r[1]]), m)
        ents.append(e)
    ents.sort(key=lambda x: x.slot)
    return j, ents

def _method(e, ins, m):
    F = e.flags
    isInit = (m <= 0)
    for i,(op,par) in enumerate(ins):
        if op in PUSH_OPS: continue
        if op == OP['SETMODEL']:
            F.add('setmodel')
            if isInit: F.add('setmodelInit')
            a=_args(ins,i,1)
            if a and a[0] is not None: e.model=a[0]
        elif op == OP['TALKON']:  F.add('talkon')
        elif op == OP['TALKRADIUS']: F.add('talkRad')
        elif op == OP['PUSHRADIUS']: F.add('pushRad')
        elif op in (OP['SET'], OP['SET3']):
            n = 2 if op==OP['SET'] else 3
            a=_args(ins,i,n)
            F.add('placed')
            if a and a[0] is not None and a[1] is not None:
                # first SET3 in the earliest method wins: that is the entity's home
                if e.pos is None or (e.posMethod is not None and m < e.posMethod):
                    e.pos=(a[0],a[1]); e.tri=par; e.posMethod=m
            else:
                F.add('posFromMemory')
        elif op == OP['SETLINE']:
            F.add('setline')
            a=_args(ins,i,6)
            if a and all(v is not None for v in a) and e.setline is None:
                e.setline=(a[0],a[1],a[3],a[4])
        elif op in (OP['MAPJUMP'], OP['MAPJUMP3'], OP['MAPJUMPO']):
            F.add('mapjump')
            n = 5 if op==OP['MAPJUMP3'] else 4
            a=_args(ins,i,n)
            if a and a[0] is not None:
                if a[0] not in e.mapjumps: e.mapjumps.append(a[0])
            else: F.add('mapjumpFromMemory')
        elif op == OP['DISCJUMP']: F.add('discjump')
        elif op == OP['WORLDMAPJUMP']: F.add('worldmapjump')
        elif op == OP['SETDRAWPOINT']:
            F.add('drawpoint')
            a=_args(ins,i,1)
            if a and a[0] is not None: e.drawId=a[0]
        elif op == OP['DRAWPOINT']: F.add('drawpoint')
        elif op in (OP['MENUSAVE'], OP['SAVEENABLE'], OP['PHSENABLE']): F.add('savepoint')
        elif op == OP['MENUSHOP']:
            F.add('shop')
            a=_args(ins,i,1)
            if a and a[0] is not None: e.shopId=a[0]
        elif op == OP['CARDGAME']: F.add('cardgame')
        elif op in (OP['LADDERUP'],OP['LADDERDOWN'],OP['LADDERUP2'],OP['LADDERDOWN2']): F.add('ladder')
        elif op in (OP['MES'],OP['ASK'],OP['AMES'],OP['AASK']): F.add('dialog')
        elif op == OP['BATTLE']: F.add('battle')
        elif op == OP['HIDE']: F.add('hide')
        elif op == OP['SHOW']: F.add('show')
        elif op == OP['ADDITEM']: F.add('additem')
        elif op in (OP['BGDRAW'],OP['BGOFF'],OP['BGANIME']): F.add('bg')
        elif op in (OP['DOORLINEON'],OP['DOORLINEOFF']): F.add('doorline')
        elif op in (OP['REQ'],OP['REQSW'],OP['REQEW']):
            if par is not None and par >= 0: e.reqSlots.append(par)
        elif op in (OP['POPM_B'],OP['POPM_W']):
            F.add('varwrite')
            if not isInit: F.add('varwriteNonInit')

def classify(e):
    F=e.flags
    if 'drawpoint' in F: return 'DRAW_POINT'
    if 'savepoint' in F: return 'SAVE_POINT'
    if 'shop' in F: return 'SHOP'
    if 'cardgame' in F: return 'CARD_GAME'
    if 'ladder' in F: return 'LADDER'
    if e.cat=='L':
        if 'mapjump' in F: return 'LINE_EXIT'
        if 'dialog' in F: return 'LINE_INTERACTIVE'
        if 'bg' in F: return 'LINE_CAMERA'
        return 'LINE_EVENT'
    if e.cat=='D': return 'DOOR'
    if e.cat=='B':
        if 'dialog' in F and 'placed' in F: return 'INTERACTIVE_OBJECT'
        return 'BACKGROUND'
    if 'setmodelInit' in F:
        if is_item_pickup(e): return 'ITEM'
        if 'talkon' in F or 'talkRad' in F or 'dialog' in F: return 'NPC'
        return 'MODEL'
    if 'mapjump' in F: return 'EXIT'
    if 'dialog' in F: return 'INTERACTION'
    return 'SCRIPT'

def is_item_pickup(e):
    F=e.flags
    return ('additem' in F) and ('setmodelInit' in F)
