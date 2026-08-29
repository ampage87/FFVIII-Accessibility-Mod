import struct, sys
OPS = {
0x00:(0,"END"),0x01:(1,"SHOWTEXT_W3 text[%d]"),0x02:(7,"IF"),0x03:(1,"queue MAGIC %d"),
0x04:(1,"TARGET %s"),0x05:(1,"actionParam %d"),0x06:(0,"EXECUTE"),0x07:(1,"queue ITEM %d"),
0x08:(0,"REMOVE_SELF"),0x09:(1,"[1D27ADC]=%d"),0x0A:(0,"nop"),0x0B:(3,"RAND_ATTACK %d/%d/%d"),
0x0C:(1,"ATTACK entry %d"),0x0D:(1,"attackSlot=%d"),0x0E:(2,"var[0x%02X]=%d"),0x0F:(2,"gA[%d]=%d"),
0x10:(0,"nop"),0x11:(2,"gB[%d]=%d"),0x12:(2,"var[0x%02X]+=%d"),0x13:(2,"gA[%d]+=%d"),
0x14:(0,"nop"),0x15:(2,"gB[%d]+=%d"),0x16:(0,"FULL_HEAL"),0x17:(1,"flag17 %d"),
0x18:(1,"SHOWTEXT_W1 text[%d]"),0x19:(1,"skip1 %d"),0x1A:(1,"MSGSLOT text[%d]"),0x1B:(2,"op1B %d %d"),
0x1C:(1,"MSGTIMER 30*%d frames"),0x1D:(1,"op1D %d"),0x1E:(1,"queue kindFC %d"),0x1F:(1,"CAST %d"),
0x20:(1,"MSGTIMER 15*%d frames"),0x21:(0,"nop"),0x22:(2,"MSGSLOT text[%d] param=%d"),
0x23:(2,"JUMP %+d"),0x24:(0,"op24"),0x25:(1,"SET_ENEMY_NAME text[%d]"),0x26:(4,"TARGET_CALC %d %d %d %d"),
0x27:(2,"FLAGBIT %d=%d"),0x28:(2,"AIREC[%d]=%d"),0x29:(0,"DRAW_MSG"),0x2A:(0,"queue drawnMagic"),
0x2B:(1,"TARGET 1<<%d"),0x2C:(0,"op2C"),0x2D:(3,"ANIMDUR[%d]=%d"),0x2E:(0,"DRAW"),
0x2F:(0,"clr40"),0x30:(0,"set40"),0x31:(1,"gfFlag %d"),0x32:(0,"[1D28E06]=1"),0x33:(0,"op33"),
0x34:(1,"CAST2 %d"),0x35:(1,"op35 %d"),0x36:(0,"op36"),0x37:(1,"SPOILS %d"),0x38:(1,"ITEMDROP %d"),
0x39:(0,"op39"),0x3A:(1,"clr40 ent%d"),0x3B:(2,"CAST3 %d tgt %d"),0x3C:(2,"HP += %d"),0x3D:(0,"op3D"),
}
SEL={0xC8:"self",0xC9:"randParty",0xCA:"randEnemy",0xCB:"lastAttacker",0xCC:"allParty",0xCD:"allEnemies",0xCE:"everyone",0xD1:"curActor"}
CMP=["==","<",">","!=","<=",">="]
SUBJ={0x00:"HPfrac",0x02:"rand",0x03:"turnCounter",0x04:"statusBit",0x08:"monsterAlive",0x09:"monsterQ",0x07:"byteBC"}
def sel(b): return SEL.get(b, "var[0x%02X]"%b if 0xDC<=b<=0xE3 else "sel0x%02X"%b)
def subj(b):
    if 0xDC<=b<=0xE3: return "var[0x%02X]"%b
    if 0x50<=b<=0x57: return "gB[0x%02X]"%b
    if 0x60<=b<=0x67: return "gA[0x%02X]"%b
    return SUBJ.get(b,"subj0x%02X"%b)
def dis(code, start, end, label):
    print("--- %s (0x%X..0x%X) ---" % (label, start, end))
    i=start
    while i < end:
        op=code[i]; base=i; i+=1
        if op==0: print("  %04X: END"%base); continue
        ent=OPS.get(op)
        if ent is None or op>0x3D:
            print("  %04X: op%02X (ignored)"%(base,op)); continue
        n,fmt=ent
        args=code[i:i+n]; i+=n
        if op==0x02:
            val=args[3]|(args[4]<<8); jmp=args[5]|(args[6]<<8)
            print("  %04X: IF %s of %s %s %d  else +%d -> %04X"%(base,subj(args[0]),sel(args[1]),CMP[args[2]] if args[2]<6 else "?%d"%args[2],val,jmp,i+jmp))
        elif op==0x23:
            d=args[0]|(args[1]<<8)
            if d>=0x8000: d-=0x10000
            print("  %04X: JUMP %+d -> %04X"%(base,d,i+d))
        elif op==0x04:
            print("  %04X: TARGET %s"%(base,sel(args[0])))
        elif op==0x2D:
            print("  %04X: ANIMDUR[%d] = %d"%(base,args[0],args[1]|(args[2]<<8)))
        elif op==0x3C:
            v=args[0]|(args[1]<<8)
            if v>=0x8000: v-=0x10000
            print("  %04X: HP += %d"%(base,v))
        else:
            try: print("  %04X: "%base + (fmt % tuple(args) if n else fmt))
            except Exception: print("  %04X: op%02X %s"%(base,op,list(args)))
d=open(sys.argv[1],"rb").read()
ns=struct.unpack_from("<I",d,0)[0]
offs=[struct.unpack_from("<I",d,4+4*i)[0] for i in range(ns)]
sec=d[offs[7]:offs[8]]
m=struct.unpack_from("<I",sec,0)[0]
sub=[struct.unpack_from("<I",sec,4+4*i)[0] for i in range(m)]
ai=sub[0]
code=sec[ai:sub[1]]
nscript=(struct.unpack_from("<I",code,0)[0])//4
starts=[struct.unpack_from("<I",code,4*i)[0] for i in range(nscript)]
print("script entry offsets:",[hex(x) for x in starts])
names=["init","execute","counter","death","preturn","x5","x6"]
for i,s0 in enumerate(starts):
    e0 = starts[i+1] if i+1<len(starts) else len(code)
    dis(code, s0, e0, names[i] if i<len(names) else "script%d"%i)
