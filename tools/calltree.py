# Static call tree: follows jsr via mov.l literal, bsr, and bra/jmp tail calls, stops at rts.
import sys;sys.path.insert(0,'tools');from sh2cap import rd,files
def code_at(a):
    for d,b in files.values():
        if b<=a<b+len(d): return True
    return False
def scan(start, limit=0x3000):
    """walk linearly until rts with no pending forward branch beyond; collect calls"""
    calls=set(); a=start; maxfwd=start; regs={}
    end=start
    while a<start+limit:
        w=rd(a,2)
        if w is None: break
        if w>>12==0xD:  # mov.l @(disp,pc),rn
            regs[(w>>8)&15]=rd(((a+4)&~3)+(w&0xff)*4,4)
        elif w&0xF0FF==0x400B or w&0xF0FF==0x402B:  # jsr/jmp @rn
            t=regs.get((w>>8)&15)
            if t: calls.add(t)
        elif w>>12==0xB:  # bsr
            disp=w&0xfff; disp=disp-0x1000 if disp&0x800 else disp; calls.add(a+4+disp*2)
        elif w>>12==0xA:  # bra
            disp=w&0xfff; disp=disp-0x1000 if disp&0x800 else disp; t=a+4+disp*2
            if t>maxfwd: maxfwd=t
            if t<start or t>start+limit: calls.add(t)
        elif (w>>8) in (0x89,0x8B,0x8D,0x8F):
            disp=w&0xff; disp=disp-0x100 if disp&0x80 else disp; t=a+4+disp*2
            if t>maxfwd: maxfwd=t
        if w==0x000B and a>=maxfwd:
            end=a+4; break
        if (w&0xF0FF==0x402B) and a>=maxfwd: end=a+4; break
        if w>>12==0xA and a>=maxfwd and not (start<=a+4+((w&0xfff)-(0x1000 if w&0x800 else 0))*2<=start+limit): end=a+4;break
        a+=2
    return calls,end
seen={}
stack=[int(x,16) for x in sys.argv[1:]]
skip=set(int(x,16) for x in open('tools/ported.txt').read().split()) if __import__('os').path.exists('tools/ported.txt') else set()
while stack:
    f=stack.pop()
    if f in seen or f in skip or not code_at(f) or f<0x06040000: continue
    c,e=scan(f); seen[f]=(e-f,sorted(c))
    stack+= [x for x in c]
tot=0
for f in sorted(seen):
    n,c=seen[f]; tot+=n
    print('%08x %5d  -> %s'%(f,n,' '.join('%08x'%x for x in c if x>=0x06040000)))
print('total bytes',tot,'funcs',len(seen))
