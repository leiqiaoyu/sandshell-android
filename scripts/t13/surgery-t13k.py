#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""NOTE (2026-10-31): docstring below is inherited from the abandoned 0.1.2-gexec line (never released).
This script builds 0.1.3 directly from v0.1.1 (code 1003). Asset name debian-bookworm-arm64-minbase.tar is kept
because the dex looks it up by name; its content is Debian 13 trixie.

Sandshell Stage 2 surgery (v0.1.2-gexec-r2), derived from P0 r2 surgery: manifest version bump + asset swaps + zip rebuild.

Base: v0.1.1 release APK. The diagnostic slot
'assets/getrandom-observer-arm64.bin' (deployed as
usr/local/lib/debian-mobile/libgetrandom_observer.so, deferred deploy used only
by the node-musl getrandom-observer diagnostic wrapper) carries the P0 glibc
probe bundle; the node-wall-probe slot is left untouched so the musl Node22
bundle stays intact. bashrc = v0.1.1 original + appended P0 deploy block.
Zero dex changes."""
import struct, zipfile, os, sys

SRC = '<build>/sandshell-v0.1.1-arm64-v8a.apk'
OUT_DIR = '<build>/out'

GEXEC_BASHRC = '<build>/debian-mobile-bashrc'
NEW_VER = '0.1.3-trixie-t13k'
NEW_CODE = 1014

RES_STRING_POOL = 0x0001
RES_XML = 0x0003
RES_XML_START_ELEM = 0x0102
RES_XML_END_ELEM = 0x0103
NO_INDEX = 0xFFFFFFFF


def encode_string(text, utf8):
    if utf8:
        b = text.encode('utf-8')
        def u8len(n):
            return bytes(((n >> 8) | 0x80, n & 0xFF)) if n > 0x7F else bytes((n,))
        return u8len(len(text)) + u8len(len(b)) + b + b'\x00'
    else:
        def u16len(n):
            if n > 0x7FFF:
                return struct.pack('<HH', (n >> 16) | 0x8000, n & 0xFFFF)
            return struct.pack('<H', n)
        return u16len(len(text)) + text.encode('utf-16-le') + b'\x00\x00'


class Axml:
    def __init__(self, buf):
        self.buf = bytearray(buf)
        b = self.buf
        t, hs, sz = struct.unpack_from('<HHI', b, 0)
        assert t == RES_XML and hs == 8, 'not an AXML document'
        self.chunks = []
        pos = 8
        while pos + 8 <= len(b):
            ct, chs, csz = struct.unpack_from('<HHI', b, pos)
            assert csz >= 8 and pos + csz <= len(b), (hex(pos), hex(ct), csz)
            self.chunks.append({'type': ct, 'off': pos, 'size': csz, 'hdr': chs})
            pos += csz
        assert pos == len(b), 'trailing bytes in AXML'
        self.pool = next(c for c in self.chunks if c['type'] == RES_STRING_POOL)
        self._parse_pool()

    def _parse_pool(self):
        b = self.buf
        po = self.pool['off']
        (self.str_count, self.style_count, self.flags,
         self.strings_start, self.styles_start) = struct.unpack_from('<IIIII', b, po + 8)
        self.utf8 = bool(self.flags & 0x100)
        self.offsets = list(struct.unpack_from('<%dI' % self.str_count, b, po + 28))
        base = po + self.strings_start
        self.entries = []
        for i in range(self.str_count):
            p = base + self.offsets[i]
            start = p
            if self.utf8:
                n = b[p]; p += 1
                if n & 0x80:
                    n = ((n & 0x7F) << 8) | b[p]; p += 1
                nb = b[p]; p += 1
                if nb & 0x80:
                    nb = ((nb & 0x7F) << 8) | b[p]; p += 1
                text = bytes(b[p:p + nb]).decode('utf-8', 'replace')
                p += nb + 1
            else:
                v = struct.unpack_from('<H', b, p)[0]; p += 2
                if v & 0x8000:
                    v2 = struct.unpack_from('<H', b, p)[0]; p += 2
                    v = ((v & 0x7FFF) << 16) | v2
                units = struct.unpack_from('<%dH' % v, b, p)
                text = ''.join(chr(x) for x in units)
                p += 2 * v + 2
            self.entries.append((start, p, text))

    def s(self, i):
        if i == NO_INDEX or i >= self.str_count:
            return None
        return self.entries[i][2]

    def elements(self):
        stack = []
        out = []
        for ci, c in enumerate(self.chunks):
            if c['type'] == RES_XML_START_ELEM:
                off = c['off']
                _, _, ns_i, name_i = struct.unpack_from('<IIII', self.buf, off + 8)
                (attr_start, attr_size, attr_count,
                 _id, _cl, _st) = struct.unpack_from('<HHHHHH', self.buf, off + 24)
                assert attr_start == 20 and attr_size == 20 and c['hdr'] == 16
                assert c['size'] == 36 + 20 * attr_count
                attrs = []
                for a in range(attr_count):
                    ao = off + 36 + a * attr_size
                    a_ns, a_name, a_raw = struct.unpack_from('<III', self.buf, ao)
                    _vs, _r0, dtype = struct.unpack_from('<HBB', self.buf, ao + 12)
                    data = struct.unpack_from('<I', self.buf, ao + 16)[0]
                    attrs.append({'off': ao, 'ns': a_ns, 'name': a_name,
                                  'raw': a_raw, 'dtype': dtype, 'data': data})
                out.append({'ci': ci, 'name': self.s(name_i), 'attrs': attrs,
                            'stack': list(stack)})
                stack.append(ci)
            elif c['type'] == RES_XML_END_ELEM:
                stack.pop()
        return out


def attr_str(ax, a):
    if a['raw'] != NO_INDEX:
        return a['raw']
    if a['dtype'] == 0x03:
        return a['data']
    return None


def find_attr(ax, info, local_name):
    for a in info['attrs']:
        if ax.s(a['name']) == local_name:
            return a
    return None


# ---------- 1. read manifest ----------
zin = zipfile.ZipFile(SRC)
names = zin.namelist()
print('zip entries:', len(names))
man = zin.read('AndroidManifest.xml')
ax = Axml(man)
print('pool: count=%d utf8=%s styles=%d' % (ax.str_count, ax.utf8, ax.style_count))

a_code = a_name = None
for info in ax.elements():
    if info['name'] == 'manifest':
        a_code = find_attr(ax, info, 'versionCode')
        a_name = find_attr(ax, info, 'versionName')
assert a_code is not None and a_name is not None
assert a_code['dtype'] == 0x10
print('current versionCode attr data:', ax.s(0) is not None and a_code['data'], '-> new', NEW_CODE)
cur_name = ax.s(attr_str(ax, a_name))
print('current versionName:', repr(cur_name))
assert cur_name == '0.1.1', cur_name
assert a_code['data'] == 1003, a_code['data']

# ---------- 2. in-place versionCode ----------
buf = bytearray(ax.buf)
struct.pack_into('<I', buf, a_code['off'] + 16, NEW_CODE)

# ---------- 3. rebuild string pool: keep all + append NEW_VER ----------
new_entries = [bytes(ax.buf[st:en]) for (st, en, t) in ax.entries]
new_entries.append(encode_string(NEW_VER, ax.utf8))
blob = b''
new_offsets = []
for e in new_entries:
    new_offsets.append(len(blob))
    blob += e
data_padded = blob + b'\x00' * ((-len(blob)) % 4)
ss = ax.strings_start
n_new = ax.str_count + 1
ns = 28 + 4 * n_new
if ns < ss:
    ns = ss
pad_after = b'\x00' * (ns - 28 - 4 * n_new)
head = struct.pack('<HHIIIIII', RES_STRING_POOL, 28, 0,
                   n_new, 0, ax.flags, ns, 0)
pool_bytes = bytearray(head + b''.join(struct.pack('<I', o) for o in new_offsets) + pad_after + data_padded)
struct.pack_into('<I', pool_bytes, 4, len(pool_bytes))

# ---------- 4. versionName attr -> new index ----------
new_idx = ax.str_count
struct.pack_into('<I', buf, a_name['off'] + 8, new_idx)
struct.pack_into('<I', buf, a_name['off'] + 16, new_idx)

# ---------- 5. rebuild manifest xml ----------
pool_ci = ax.chunks.index(ax.pool)
body = bytearray()
for ci, c in enumerate(ax.chunks):
    if ci == pool_ci:
        body += bytes(pool_bytes)
    else:
        body += buf[c['off']:c['off'] + c['size']]
out = bytearray()
out += struct.pack('<HHI', RES_XML, 8, 8 + len(body))
out += body
new_manifest = bytes(out)
print('manifest rebuilt: %d bytes (was %d)' % (len(new_manifest), len(man)))

# ---------- 6. verify ----------
ax2 = Axml(new_manifest)
v_ok = c_ok = None
for info in ax2.elements():
    if info['name'] == 'manifest':
        a2n = find_attr(ax2, info, 'versionName')
        a2c = find_attr(ax2, info, 'versionCode')
        v_ok = ax2.s(attr_str(ax2, a2n))
        c_ok = a2c['data']
assert v_ok == NEW_VER, v_ok
assert c_ok == NEW_CODE, c_ok
print('verified: versionName=%s versionCode=%d' % (v_ok, c_ok))

# ---------- 7. zip rebuild ----------
os.makedirs(OUT_DIR, exist_ok=True)
OUT = os.path.join(OUT_DIR, 'sandshell-v0.1.3-trixie-t13k-unsigned.apk')
repl = {
    'AndroidManifest.xml': new_manifest,
    # g-exec bootstrap + payload (shim + static rg) ride inside the PTY bashrc asset;
    # every other asset (node-wall-probe musl Node22 bundle, noshm3, observer...) is byte-identical to v0.1.1.
    'assets/debian-mobile-bashrc': open(GEXEC_BASHRC, 'rb').read(),
    # sw3: TerminalView long-press drag -> mouse drag for mouse-tracking apps (smali patch, <build-tree>)
    'classes14.dex': open('<build>/classes14.new.dex', 'rb').read(),
    # sw5: TermuxActivityRootView keyboard-margin loop disabled in multi-window/freeform (small-window jitter)
    'classes21.dex': open('<build>/classes21.new.dex', 'rb').read(),
    # sw4: supervisor fd-registry leak fix (2-instruction patch, <build-tree>)
    # t13: supervisor BPF also ALLOWs pathless 261 prlimit64, 269 sendmmsg, 278 getrandom, 436 close_range (Android app filter allows them)
    'lib/arm64-v8a/libdebian_runtime.so': open('<build>/libdebian_runtime.t13j.so', 'rb').read(),
    # t13: Debian 13 trixie rootfs (gzip, same asset name the dex expects; name is legacy)
    'assets/debian-bookworm-arm64-minbase.tar': open('<build>/debian-trixie-arm64.tar.gz', 'rb').read(),
    # t13: loader = Debian 13 ld.so with set_robust_list/rseq svc -> ENOSYS
    'lib/arm64-v8a/libdebian-ld.so': open('<build>/ld-linux-aarch64.so.1', 'rb').read(),
    # t13: DebianRootfs + RootfsMigrator (Debian 11 -> 13 migration, glibc self-heal)
    # t13j: diagnostic crash reporter as the PTY preload; real exec interposer beside it
    'lib/arm64-v8a/libdebian_exec.so': open('<build>/libdebian_exec.t13k.so','rb').read(),
    'classes24.dex': open('<build>/classes24.new.dex', 'rb').read(),
}
print('replacing:')
for k, v in repl.items():
    print('  %s -> %d bytes' % (k, len(v)))

zout = zipfile.ZipFile(OUT, 'w')
cnt_repl = cnt_copy = 0
for item in zin.infolist():
    if item.filename in ('assets/node-wall-probe-arm64', 'assets/install-node22-musl-candidate.sh'):
        print('dropped', item.filename); continue  # sw6 pure glibc
    if item.filename in repl:
        data = repl[item.filename]
        zi = zipfile.ZipInfo(item.filename, item.date_time)
        zi.external_attr = item.external_attr
        zi.compress_type = item.compress_type
        zout.writestr(zi, data)
        cnt_repl += 1
    else:
        zout.writestr(item, zin.read(item.filename))
        cnt_copy += 1
# gw1: glibc opencode as extractable native lib (deflate, like the other libs)
zi = zipfile.ZipInfo('lib/arm64-v8a/libopencode_glibc.so', (1981,1,1,1,1,0))
zi.compress_type = zipfile.ZIP_DEFLATED; zi.external_attr = 0
zout.writestr(zi, open('<build>/opencode','rb').read(), compresslevel=6)
print('added lib/arm64-v8a/libopencode_glibc.so')
# gw3: glibc node (nodejs.org v22.23.4 linux-arm64) + npm tarball, both as extractable native libs
for nm, src in (('libnode_glibc.so', '<build>/ngx-node'), ('libnpm_glibc.so', '<build>/libnpm_glibc.so')):
    zi = zipfile.ZipInfo('lib/arm64-v8a/' + nm, (1981,1,1,1,1,0))
    zi.compress_type = zipfile.ZIP_DEFLATED; zi.external_attr = 0
    zout.writestr(zi, open(src,'rb').read(), compresslevel=6)
    print('added lib/arm64-v8a/' + nm)
zi = zipfile.ZipInfo('lib/arm64-v8a/libss_exec_real.so', (1981,1,1,1,1,0))
zi.compress_type = zipfile.ZIP_DEFLATED; zi.external_attr = 0
zout.writestr(zi, open('<build>/libss_exec_real.so','rb').read(), compresslevel=6)
print('added libss_exec_real.so')
zout.close()
zin.close()
print('zip rebuild done: replaced=%d copied=%d' % (cnt_repl, cnt_copy))
print('output:', OUT)
print('size:', os.path.getsize(OUT))
print('SURGERY_DONE')
