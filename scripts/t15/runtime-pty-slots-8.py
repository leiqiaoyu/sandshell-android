#!/usr/bin/env python3
"""Widen libdebian_runtime PTY slots 4 -> 8 (table moved to new bss at 0x19000, id = gen<<3 | idx)."""
import struct, subprocess, sys, os
SRC, DST = sys.argv[1], sys.argv[2]
b = bytearray(open(SRC,'rb').read())
NEW = 0x19000; STRIDE = 0x1078; N = 8
def asm(addr, text, syms={}):
    open('/tmp/s.S','w').write('.text\n.globl _start\n_start:\n'+text+'\n')
    subprocess.check_call(['aarch64-linux-gnu-as','-o','/tmp/s.o','/tmp/s.S'])
    args=['aarch64-linux-gnu-ld','-o','/tmp/s.elf','-Ttext=%#x'%addr,'-e','_start']
    for k,v in syms.items(): args+=['--defsym','%s=%#x'%(k,v)]
    subprocess.check_call(args+['/tmp/s.o'])
    subprocess.check_call(['aarch64-linux-gnu-objcopy','-O','binary','-j','.text','/tmp/s.elf','/tmp/s.bin'])
    return open('/tmp/s.bin','rb').read()
def put(addr, code, limit=None, expect=None):
    if limit: assert len(code) <= limit, (hex(addr), len(code), limit)
    o = addr-0x1000; b[o:o+len(code)] = code   # text LOAD: vaddr 0x52cc @ file 0x42cc
def word(addr): return struct.unpack_from('<I', b, addr-0x1000)[0]
def ins(addr, text, old=None):
    if old is not None: assert word(addr)==old, (hex(addr), hex(word(addr)))
    put(addr, asm(addr, text))
S = dict(memset=0xf3d0, mutinit=0xf900, NEWB=NEW)
# 1. id encoding / decoding
ins(0x71a4,'orr w22, w9, w8, lsl #3',0x2a080936)
ins(0xec40,'orr w3, w10, w9, lsl #3',0x2a090943)
ins(0xefb8,'orr w3, w10, w9, lsl #3',0x2a090943)
for a,r in [(0x765c,'w8, w21'),(0x7878,'w8, w20'),(0x7ad0,'w8, w19'),(0x7bd4,'w8, w19')]:
    ins(a,'and %s, #0x7'%r)
for a,r in [(0x7674,'w22, w21'),(0x788c,'w26, w20'),(0x7ae4,'w20, w19'),(0x7bec,'w21, w19')]:
    ins(a,'lsr %s, #3'%r)
ins(0x713c,'tst w8, #0x0fffffff',0x7200711f)
ins(0x7140,'and w8, w8, #0x0fffffff',0x12007108)
# 2. repoint table base (adrp/add pairs)
for ad,ar,reg in [(0x70f4,0x70fc,'x10'),(0x74f8,0x74fc,'x10'),(0x7554,0x755c,'x10'),(0x76b8,0x76c4,'x26'),
                  (0x7894,0x78a0,'x25'),(0x7aec,0x7af8,'x24'),(0x7bf4,0x7c00,'x22'),(0x7cc0,0x7cc4,'x20'),
                  (0x7cf8,0x7cfc,'x0'),(0xed1c,0xed20,'x19')]:
    assert word(ar) & 0xffc00000 == 0x91000000 and ((word(ar)>>10)&0xfff)==0xf28, hex(ar)
    ins(ad,'adrp %s, NEWB'%reg) if False else None
    put(ad, asm(ad,'adrp %s, NEWB'%reg, S)); put(ar, asm(ar,'add %s, %s, #0'%(reg,reg)))
# legacy TerminalActivity.isPtyRunning slot0 flag
put(0x7ce8, asm(0x7ce8,'adrp x8, NEWB',S)); ins(0x7cec,'ldr w8, [x8, #0x34]',0xb94f5d08)
# 3. start: free-slot search over N slots (flag at +0x30), x9 = index -> 0x70f4; none -> 0x70c4
assert word(0x70c4)==0x90000060
put(0x7094, asm(0x7094, '''
  adrp x10, NEWB
  add x10, x10, #0x30
  mov x9, #0
  mov w11, #%d
1: ldr w8, [x10]
  cbz w8, found
  add x9, x9, #1
  add x10, x10, x11
  cmp x9, #%d
  b.lt 1b
  b fail
''' % (STRIDE, N), dict(S, found=0x70f4, fail=0x70c4)), limit=0x70c4-0x7094)
# 4. once-init of N slots (replaces unrolled 4) at 0xed14..0xee13
assert word(0xee10)==0xd65f03c0
put(0xed14, asm(0xed14, '''
  stp x29, x30, [sp, #-48]!
  stp x20, x19, [sp, #32]
  str x21, [sp, #16]
  mov x29, sp
  adrp x19, NEWB
  mov w20, #0
1: mov x0, x19
  mov w1, wzr
  mov w2, #%d
  bl memset
  mov x0, x19
  mov x1, xzr
  bl mutinit
  str w20, [x19, #40]
  mov w8, #-1
  str w8, [x19, #64]
  str w8, [x19, #68]
  str w8, [x19, #72]
  add w20, w20, #1
  mov w9, #%d
  add x19, x19, x9
  cmp w20, #%d
  b.lt 1b
  ldr x21, [sp, #16]
  ldp x20, x19, [sp, #32]
  ldp x29, x30, [sp], #48
  ret
''' % (STRIDE, STRIDE, N), S), limit=0xee14-0xed14)
# 5. ELF: grow last PT_LOAD memsz + .bss size to cover NEW..NEW+N*STRIDE
end = NEW + N*STRIDE
e_phoff, = struct.unpack_from('<Q', b, 0x20); phentsize, phnum = struct.unpack_from('<HH', b, 0x36)
last=None
for i in range(phnum):
    o=e_phoff+i*phentsize; t,=struct.unpack_from('<I',b,o)
    if t==1: last=o
vaddr,=struct.unpack_from('<Q',b,last+16); memsz,=struct.unpack_from('<Q',b,last+40)
assert vaddr+memsz <= NEW
struct.pack_into('<Q',b,last+40,end-vaddr)
e_shoff,=struct.unpack_from('<Q',b,0x28); shentsize,shnum=struct.unpack_from('<HH',b,0x3a)
for i in range(shnum):
    o=e_shoff+i*shentsize; t,=struct.unpack_from('<I',b,o+4); a,=struct.unpack_from('<Q',b,o+16)
    if t==8 and a==0x11f00: struct.pack_into('<Q',b,o+32,end-0x11f00)
open(DST,'wb').write(b); print('ok', DST, hex(end))
