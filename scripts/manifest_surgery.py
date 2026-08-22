#!/usr/bin/env python3
"""
AndroidManifest.xml (binary AXML) surgical patcher for Debian Mobile Part58.

Changes (maintainer-confirmed 2026-08-23 revision):
 1. versionName: 0.118.3 -> 0.1
 2. application android:debuggable: true -> false
 3. remove 7 uses-permission elements
    (MANAGE_EXTERNAL_STORAGE is KEPT: the /storage/emulated/0/Debian-mobile
     <-> /mnt/sdcard interop bridge depends on it; maintainer decision)
 4. android:exported true -> false on SettingsActivity & RunCommandService

Design: minimal byte-level surgery.
 - untouched strings copied byte-for-byte into rebuilt string pool
 - attribute edits are 4-byte in-place value patches
 - element removal drops whole START/END element chunks
 - doc chunk size + string pool offsets recomputed
"""
import struct
import sys

SRC = "/data/user/work/apkaudit/unpacked/AndroidManifest.xml"
DST = "/data/user/work/apkaudit/AndroidManifest.patched.xml"

REMOVE_PERMS = {
    "android.permission.SYSTEM_ALERT_WINDOW",
    "android.permission.READ_LOGS",
    "android.permission.DUMP",
    "android.permission.WRITE_SECURE_SETTINGS",
    "android.permission.REQUEST_INSTALL_PACKAGES",
    "android.permission.PACKAGE_USAGE_STATS",
    "com.android.alarm.permission.SET_ALARM",
}
KEEP_PERMS = {
    "android.permission.MANAGE_EXTERNAL_STORAGE",  # interop bridge, user-granted toggle
}
UNEXPORT_COMPONENTS = {
    "com.termux.app.activities.SettingsActivity",
    "com.termux.app.RunCommandService",
}
NEW_VERSION = "0.1"

RES_STRING_POOL = 0x0001
RES_XML = 0x0003
RES_XML_START_NS = 0x0100
RES_XML_END_NS = 0x0101
RES_XML_START_ELEM = 0x0102
RES_XML_END_ELEM = 0x0103
RES_XML_CDATA = 0x0104
RES_XML_RES_MAP = 0x0180

NO_INDEX = 0xFFFFFFFF


# ---------------------------------------------------------------- parsing
class Axml:
    def __init__(self, buf):
        self.buf = bytearray(buf)
        b = self.buf
        t, hs, sz = struct.unpack_from("<HHI", b, 0)
        assert t == RES_XML and hs == 8, "not an AXML document"
        self.doc_size = sz
        self.chunks = []
        pos = 8
        while pos + 8 <= len(b):
            ct, chs, csz = struct.unpack_from("<HHI", b, pos)
            assert csz >= 8 and pos + csz <= len(b), (hex(pos), hex(ct), csz)
            self.chunks.append({"type": ct, "off": pos, "size": csz, "hdr": chs})
            pos += csz
        assert pos == len(b), "trailing bytes in AXML"

        self.pool = next(c for c in self.chunks if c["type"] == RES_STRING_POOL)
        self._parse_pool()

    def _parse_pool(self):
        b = self.buf
        po = self.pool["off"]
        (self.str_count, self.style_count, self.flags,
         self.strings_start, self.styles_start) = struct.unpack_from("<IIIII", b, po + 8)
        self.utf8 = bool(self.flags & 0x100)
        self.offsets = list(struct.unpack_from("<%dI" % self.str_count, b, po + 28))
        base = po + self.strings_start
        self.entries = []  # (start, end, text)
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
                text = bytes(b[p:p + nb]).decode("utf-8", "replace")
                p += nb + 1
            else:
                v = struct.unpack_from("<H", b, p)[0]; p += 2
                if v & 0x8000:
                    v2 = struct.unpack_from("<H", b, p)[0]; p += 2
                    v = ((v & 0x7FFF) << 16) | v2
                units = struct.unpack_from("<%dH" % v, b, p)
                text = "".join(chr(x) for x in units)
                p += 2 * v + 2
            self.entries.append((start, p, text))

    def s(self, i):
        if i == NO_INDEX or i >= self.str_count:
            return None
        return self.entries[i][2]

    def elements(self):
        """Yield (chunk_idx, name, attrs, stack) for every START_ELEMENT."""
        stack = []
        out = []
        for ci, c in enumerate(self.chunks):
            if c["type"] == RES_XML_START_ELEM:
                off = c["off"]
                _, _, ns_i, name_i = struct.unpack_from("<IIII", self.buf, off + 8)
                (attr_start, attr_size, attr_count,
                 _id, _cl, _st) = struct.unpack_from("<HHHHHH", self.buf, off + 24)
                # attribute area = chunk headerSize(16) + attributeStart(20)
                assert attr_start == 20 and attr_size == 20 and c["hdr"] == 16
                assert c["size"] == 36 + 20 * attr_count
                attrs = []
                for a in range(attr_count):
                    ao = off + 36 + a * attr_size
                    a_ns, a_name, a_raw = struct.unpack_from("<III", self.buf, ao)
                    _vs, _r0, dtype = struct.unpack_from("<HBB", self.buf, ao + 12)
                    data = struct.unpack_from("<I", self.buf, ao + 16)[0]
                    attrs.append({"off": ao, "ns": a_ns, "name": a_name,
                                  "raw": a_raw, "dtype": dtype, "data": data})
                out.append({"ci": ci, "name": self.s(name_i), "attrs": attrs,
                            "stack": list(stack)})
                stack.append(ci)
            elif c["type"] == RES_XML_END_ELEM:
                stack.pop()
        return out


def attr_str(ax, a):
    """Resolve an attribute value as string index (raw or typed-string)."""
    if a["raw"] != NO_INDEX:
        return a["raw"]
    if a["dtype"] == 0x03:
        return a["data"]
    return None


def find_attr(ax, info, local_name):
    for a in info["attrs"]:
        if ax.s(a["name"]) == local_name:
            return a
    return None


def dump_xml(ax, out=sys.stdout):
    """Canonical decoded XML dump for diffing."""
    depth = 0
    for c in ax.chunks:
        if c["type"] == RES_XML_START_ELEM:
            off = c["off"]
            _, _, ns_i, name_i = struct.unpack_from("<IIII", ax.buf, off + 8)
            (attr_start, attr_size, attr_count,
             _id, _cl, _st) = struct.unpack_from("<HHHHHH", ax.buf, off + 24)
            assert attr_start == 20 and attr_size == 20 and c["hdr"] == 16
            name = ax.s(name_i) or "?"
            parts = []
            for a in range(attr_count):
                ao = off + 36 + a * attr_size
                a_ns, a_name, a_raw = struct.unpack_from("<III", ax.buf, ao)
                _vs, _r0, dtype = struct.unpack_from("<HBB", ax.buf, ao + 12)
                data = struct.unpack_from("<I", ax.buf, ao + 16)[0]
                ns = ax.s(a_ns)
                prefix = "android:" if ns and ns.endswith("res/android") else ""
                an = prefix + (ax.s(a_name) or "?")
                if a_raw != NO_INDEX:
                    val = ax.s(a_raw)
                elif dtype == 0x03:
                    val = ax.s(data)
                elif dtype == 0x12:
                    val = "true" if data == 0xFFFFFFFF else "false"
                elif dtype == 0x11:
                    val = "0x%08x" % data
                elif dtype in (0x10,):
                    val = str(struct.unpack("<i", struct.pack("<I", data))[0])
                elif dtype == 0x01:
                    val = "@0x%08x" % data
                else:
                    val = "(dtype=%02x data=%d)" % (dtype, data)
                parts.append('%s="%s"' % (an, val))
            print("%s<%s%s>" % ("  " * depth, name, (" " + " ".join(parts)) if parts else ""), file=out)
            depth += 1
        elif c["type"] == RES_XML_END_ELEM:
            depth -= 1
        elif c["type"] == RES_XML_CDATA:
            pass


# ---------------------------------------------------------------- surgery
orig = Axml(open(SRC, "rb").read())

# 1. locate versionName string index (assert unique usage)
ver_idx = None
usage_count = 0
for info in orig.elements():
    if info["name"] == "manifest":
        a = find_attr(orig, info, "versionName")
        assert a is not None, "no versionName attribute"
        idx = attr_str(orig, a)
        assert orig.s(idx) == "0.118.3", "unexpected versionName: %r" % orig.s(idx)
        ver_idx = idx
# count references to that string index across all attributes
for info in orig.elements():
    for a in info["attrs"]:
        if attr_str(orig, a) == ver_idx:
            usage_count += 1
assert usage_count == 1, "string %r referenced %d times" % (orig.s(ver_idx), usage_count)

# 2. in-place patches on a copy of the buffer
buf = bytearray(orig.buf)
patched = 0

elems = orig.elements()
for info in elems:
    name_a = find_attr(orig, info, "name")
    comp = orig.s(attr_str(orig, name_a)) if name_a else None

    if info["name"] == "application":
        a = find_attr(orig, info, "debuggable")
        assert a is not None and a["dtype"] == 0x12 and a["data"] == 0xFFFFFFFF
        struct.pack_into("<I", buf, a["off"] + 16, 0)  # false
        patched += 1

    if comp in UNEXPORT_COMPONENTS:
        a = find_attr(orig, info, "exported")
        assert a is not None, "no exported attr on %s" % comp
        assert a["dtype"] == 0x12 and a["data"] == 0xFFFFFFFF, "unexpected exported encoding"
        struct.pack_into("<I", buf, a["off"] + 16, 0)  # false
        patched += 1

# 3. find uses-permission leaf chunks to remove
remove_cis = set()
removed_perms = []
stack = []
for ci, c in enumerate(orig.chunks):
    if c["type"] == RES_XML_START_ELEM:
        stack.append(ci)
    elif c["type"] == RES_XML_END_ELEM:
        si = stack.pop()
        if si != ci - 1:
            continue  # not a leaf
        info = next(e for e in elems if e["ci"] == si)
        if info["name"] != "uses-permission":
            continue
        name_a = find_attr(orig, info, "name")
        perm = orig.s(attr_str(orig, name_a))
        if perm in REMOVE_PERMS:
            remove_cis.add(si)
            remove_cis.add(ci)
            removed_perms.append(perm)
assert set(removed_perms) == REMOVE_PERMS, "missing: %s" % (REMOVE_PERMS - set(removed_perms))
assert len(remove_cis) == 2 * len(REMOVE_PERMS)

# sanity: kept permissions must still be present
orig_perms = []
for info in elems:
    if info["name"] == "uses-permission":
        orig_perms.append(orig.s(attr_str(orig, find_attr(orig, info, "name"))))
for kp in KEEP_PERMS:
    assert kp in orig_perms, "expected permission missing from source: %s" % kp

# 4. rebuild string pool, replacing only the versionName entry
def encode_string(text, utf8):
    if utf8:
        b = text.encode("utf-8")

        def u8len(n):
            return bytes(((n >> 8) | 0x80, n & 0xFF)) if n > 0x7F else bytes((n,))
        return u8len(len(text)) + u8len(len(b)) + b + b"\x00"
    else:
        def u16len(n):
            if n > 0x7FFF:
                return struct.pack("<HH", (n >> 16) | 0x8000, n & 0xFFFF)
            return struct.pack("<H", n)
        return u16len(len(text)) + text.encode("utf-16-le") + b"\x00\x00"

new_entries = []
for i, (st, en, text) in enumerate(orig.entries):
    if i == ver_idx:
        new_entries.append(encode_string(NEW_VERSION, orig.utf8))
    else:
        new_entries.append(bytes(orig.buf[st:en]))

blob = b""
new_offsets = []
for e in new_entries:
    new_offsets.append(len(blob))
    blob += e
data_padded = blob + b"\x00" * ((-len(blob)) % 4)

ss = orig.strings_start
assert ss >= 28 + 4 * orig.str_count
pad_after_offsets = b"\x00" * (ss - 28 - 4 * orig.str_count)

assert orig.style_count == 0, "style pool present - extra handling needed"
head = struct.pack("<HHIIIIII", RES_STRING_POOL, 28, 0,
                   orig.str_count, 0, orig.flags, ss, 0)
pool_bytes = bytearray(
    head
    + b"".join(struct.pack("<I", o) for o in new_offsets)
    + pad_after_offsets
    + data_padded)
struct.pack_into("<I", pool_bytes, 4, len(pool_bytes))
pool_bytes = bytes(pool_bytes)

# 5. assemble
pool_ci = orig.chunks.index(orig.pool)
out = bytearray()
body = bytearray()
for ci, c in enumerate(orig.chunks):
    if ci in remove_cis:
        continue
    if ci == pool_ci:
        body += pool_bytes
    else:
        body += buf[c["off"]:c["off"] + c["size"]]
out += struct.pack("<HHI", RES_XML, 8, 8 + len(body))
out += body
open(DST, "wb").write(bytes(out))
print("patched manifest written: %d bytes (was %d)" % (len(out), len(orig.buf)))
print("in-place attr patches: %d (debuggable + 2x exported)" % patched)
print("removed permission elements: %d" % len(removed_perms))

# ---------------------------------------------------------------- verify
new = Axml(open(DST, "rb").read())

import io
o1, o2 = io.StringIO(), io.StringIO()
dump_xml(orig, o1)
dump_xml(new, o2)

old_lines = o1.getvalue().splitlines()
new_lines = o2.getvalue().splitlines()

import difflib
diff = list(difflib.unified_diff(old_lines, new_lines, "original", "patched", lineterm=""))
print("\n=== semantic diff (%d lines) ===" % len(diff))
for line in diff[:80]:
    print(line)

# summary assertions
def summarize(ax):
    ver = dbg = None
    perms = []
    exports = {}
    for info in ax.elements():
        if info["name"] == "manifest":
            ver = ax.s(attr_str(ax, find_attr(ax, info, "versionName")))
        if info["name"] == "application":
            a = find_attr(ax, info, "debuggable")
            if a:
                dbg = "true" if a["data"] == 0xFFFFFFFF else "false"
        if info["name"] == "uses-permission":
            perms.append(ax.s(attr_str(ax, find_attr(ax, info, "name"))))
        name_a = find_attr(ax, info, "name")
        comp = ax.s(attr_str(ax, name_a)) if name_a else None
        if comp in UNEXPORT_COMPONENTS:
            e = find_attr(ax, info, "exported")
            exports[comp] = "true" if (e and e["data"] == 0xFFFFFFFF) else "false"
    return ver, dbg, sorted(perms), exports

ver, dbg, perms, exports = summarize(new)
assert ver == "0.1", ver
assert dbg == "false", dbg
assert len(perms) == 8, perms
assert "android.permission.MANAGE_EXTERNAL_STORAGE" in perms, perms
assert all(v == "false" for v in exports.values()), exports
print("\n=== verified summary ===")
print("versionName :", ver)
print("debuggable  :", dbg)
print("permissions :")
for p in perms:
    print("   ", p)
print("exported    :", exports)
print("\nALL CHECKS PASSED")
