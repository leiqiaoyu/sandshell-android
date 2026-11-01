import struct
def load(p,off=0x3f74):
    d=open(p,'rb').read(); return [struct.unpack_from('<HBBI',d,off+8*i) for i in range(60)]
def run(prog,nr,ip):
    data=struct.pack('<iIQ6Q',nr,0xc00000b7,ip,*[0]*6); A=0;pc=0
    while True:
        c,jt,jf,k=prog[pc]
        if c==0x20: A=struct.unpack_from('<I',data,k)[0]; pc+=1
        elif c==0x15: pc+=1+(jt if A==k else jf)
        elif c==0x35: pc+=1+(jt if A>=k else jf)
        elif c==0x06: return k
        else: raise Exception(hex(c))
o=load('rt17.so'); n=load('rt18/rt18.so')
diff=[(nr,hex(run(o,nr,0x7f0000)),hex(run(n,nr,0x7f0000))) for nr in range(0,600) if run(o,nr,0x7f0000)!=run(n,nr,0x7f0000)]
print(diff)
print(set(hex(run(n,nr,0x6000001234)) for nr in range(600)))
print('notified', [nr for nr in range(600) if run(o,nr,0x7f00)==0x7fc00000])
