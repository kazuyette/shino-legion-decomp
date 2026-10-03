import sys,capstone as C
md=C.Cs(C.CS_ARCH_SH,C.CS_MODE_SH2|C.CS_MODE_BIG_ENDIAN)

import os;M=os.path.expanduser("~/mnt/Shinobi_Legions_(USA)_(DW0536)/extracted/")
files={'A':(open(M+'A.BIN','rb').read(),0x06004000),'S':(open(M+'SHINOBI.dec.bin','rb').read(),0x06040000)}
# DIRECTOR.PPB overlays SHINOBI at the same address: select it with SL_OVERLAY=D
if os.environ.get('SL_OVERLAY')=='D': files['S']=(open(M+'DIRECTOR.dec.bin','rb').read(),0x06040000)
files['D']=(open(M+'DIRECTOR.dec.bin','rb').read(),0x06040000)
def rd(a,n):
    for d,b in files.values():
        if b<=a<b+len(d): return int.from_bytes(d[a-b:a-b+n],'big')
def dis(f,lo,hi):
    d,b=files[f]
    for a in range(lo,hi,2):
        w=d[a-b:a-b+2]; ins=list(md.disasm(w,a))
        x=int.from_bytes(w,'big'); note=''
        if x>>12==0xD: p=((a+4)&~3)+(x&0xff)*4; note='; =%08x'%rd(p,4)
        elif x>>12==0x9: p=a+4+(x&0xff)*2; note='; =%04x'%rd(p,2)
        s=(ins[0].mnemonic+' '+ins[0].op_str) if ins else '.word %04x'%x
        print('%08x %04x  %-28s%s'%(a,x,s,note))
if __name__=='__main__': dis(sys.argv[1],int(sys.argv[2],16),int(sys.argv[3],16))
