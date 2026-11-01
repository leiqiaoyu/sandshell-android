import struct,sys
src=open('rt17.so','rb').read()
ALLOW,ENOSYS,NOTIF,ERR0=0x7fff0000,0x50026,0x7fc00000,0x50000
EN=[280,241,273,277,293,334,435,437,439]
AL=[260,261,269,278,436,36]
UID=[174,175,176,177]
NT=[43,56,88,53,54,55,79,80,48,78,34,35,38]
prog=[]  # (code,jt_label,jf_label,k)  labels: 'A','E','N','Z' or None
prog.append((0x20,None,None,12))           # ld ip_hi
prog.append((0x15,'A',None,0x60))          # sandshell in-process region -> allow
prog.append((0x20,None,None,0))            # ld nr
for n in EN: prog.append((0x15,'E',None,n))
for n in AL: prog.append((0x15,'A',None,n))
prog.append((0x35,'E',None,260))           # >=260 -> ENOSYS
for n in UID: prog.append((0x15,'Z',None,n))
for n in NT: prog.append((0x15,'N',None,n))
body=len(prog)
rets={'A':ALLOW,'E':ENOSYS,'N':NOTIF,'Z':ERR0}
order=['A','E','N','Z']
pos={l:body+i for i,l in enumerate(order)}
out=b''
for i,(c,jt,jf,k) in enumerate(prog):
    t=0 if jt is None else pos[jt]-i-1
    f=0 if jf is None else pos[jf]-i-1
    assert t<256
    out+=struct.pack('<HBBI',c,t,f,k)
for l in order: out+=struct.pack('<HBBI',6,0,0,rets[l])
n=len(out)//8; print('insns',n); assert n<=60
out+=struct.pack('<HBBI',6,0,0,ALLOW)*(60-n)
d=bytearray(src); d[0x3f74:0x3f74+480]=out
open(sys.argv[1],'wb').write(d)
