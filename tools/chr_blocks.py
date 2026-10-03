import sys
def dec(d,p):
    hdr=int.from_bytes(d[p:p+4],'big'); total=hdr&0xFFFFFF; out=[]; q=p+4
    while len(out)<total:
        f=int.from_bytes(d[q:q+2],'big');q+=2
        for b in range(15,-1,-1):
            if len(out)>=total: break
            w=int.from_bytes(d[q:q+2],'big');q+=2
            if f>>b&1: out.append(w)
            else:
                dist,n=w>>4,(w&15)+3
                if not 0<dist<=len(out): raise ValueError('bad ref')
                for _ in range(n): out.append(out[-dist])
    return out,q
def walk(d):
    p=0; blocks=[]; gaps={}
    while p<len(d)-4:
        for skip in range(0,16,2):
            if d[p+skip]==0x80 and 0<int.from_bytes(d[p+skip+1:p+skip+4],'big')<0x100000:
                try: out,q=dec(d,p+skip); break
                except Exception: pass
        else: return blocks,p,gaps
        if skip: gaps[skip]=gaps.get(skip,0)+1
        blocks.append((p+skip,len(out)*2,d[p:p+skip])); p=q
    return blocks,p,gaps
if __name__=='__main__':
    d=open(sys.argv[1],'rb').read(); b,p,g=walk(d)
    import collections
    print(sys.argv[1],len(b),'blocks, end',hex(p),'/',hex(len(d)),'gaps',g)
    print('sizes',collections.Counter(s for _,s,_ in b).most_common(8))
    print('gap bytes sample',[x[2].hex() for x in b if x[2]][:12])
