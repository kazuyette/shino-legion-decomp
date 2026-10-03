import sys;sys.path.insert(0,'tools');from sh2cap import rd
for a in sys.argv[1:]:
    x=int(a,16); print('%08x  w=%04x  l=%08x'%(x,rd(x,2) or 0,rd(x,4) or 0))
