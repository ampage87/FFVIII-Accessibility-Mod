"""Reproduce the mod's CURRENT sym-naming logic offline, to measure the damage."""
import ffield

def sym_tokens(f):
    return [l for l in ffield.sym_lines(f) if l]

def load_sym_names_mod(f):
    """LoadSYMNames: 32-byte fixed records, stop at first '::'."""
    b = ffield.rd(f, '.sym')
    if b is None: return []
    out=[]; pos=0
    while pos+32 <= len(b):
        rec=b[pos:pos+32].decode('latin1'); pos+=32
        rec=rec.rstrip(' \x00\n\r')
        if not rec: continue
        if '::' in rec: break
        out.append(rec)
    return out

def load_sym_categories_mod(f):
    """LoadSYMCategories: tokenize; bare names until a repeat; classify by method name."""
    toks = sym_tokens(f)
    ent=[]; isLine={}; isOther={}; inFuncs=False
    for t in toks:
        if '::' not in t:
            if t in ent: inFuncs=True
            elif not inFuncs: ent.append(t)
        else:
            nm,meth = t.split('::',1)
            if nm in ent:
                if meth in ('accross','across','lineon'): isLine[nm]=True
                elif meth in ('talk','push','move','seat'): isOther[nm]=True
    cats=[1 if isLine.get(n) else (3 if isOther.get(n) else 2) for n in ent]
    return ent, cats

def mod_names(f, j):
    symNames = load_sym_names_mod(f)
    symCount = len(symNames)
    ent, cats = load_sym_categories_mod(f)
    symCatCount = len(cats)
    symLineIdx=[i for i,c in enumerate(cats) if c==1]
    symBgIdx  =[i for i,c in enumerate(cats) if c==2]
    symOthIdx =[i for i,c in enumerate(cats) if c==3]
    total=j.ngrp; nD=j.nD; nL=j.nL; nB=j.nB
    othersHdr = total-nD-nL-nB
    remap = (len(symLineIdx)==nL and len(symBgIdx)==nB and len(symOthIdx)==othersHdr) and symCatCount>0
    out=[]
    for e in range(total):
        symIdx = e-nD
        if remap:
            if e < nD: symIdx=-1
            elif e < nD+nL: symIdx = symLineIdx[e-nD] if e-nD < len(symLineIdx) else -1
            elif e < nD+nL+nB: symIdx = symBgIdx[e-nD-nL] if e-nD-nL < len(symBgIdx) else -1
            else:
                k=e-nD-nL-nB
                symIdx = symOthIdx[k] if k < len(symOthIdx) else -1
        out.append(symNames[symIdx] if 0<=symIdx<symCount else None)
    return out, remap
