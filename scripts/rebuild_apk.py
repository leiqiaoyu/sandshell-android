#!/usr/bin/env python3
"""Rebuild the Part58 APK with the patched AndroidManifest.xml.

- strips the 3 v1 signature files (MANIFEST.MF / DEBIAN-R.SF / DEBIAN-R.RSA);
  the old v2/v3 signing block is dropped implicitly by the full re-zip
- replaces AndroidManifest.xml with the patched binary manifest
- preserves per-entry compression method, timestamps and attrs
- verifies every other entry is byte-identical (CRC32) to the original
"""
import zipfile

SRC = "part58.apk"
MANIFEST = "AndroidManifest.patched.xml"
OUT = "debian-mobile-v0.1-unsigned.apk"
SIG_FILES = {"META-INF/MANIFEST.MF", "META-INF/DEBIAN-R.SF", "META-INF/DEBIAN-R.RSA"}

manifest_bytes = open(MANIFEST, "rb").read()
zin = zipfile.ZipFile(SRC)

with zipfile.ZipFile(OUT, "w") as zout:
    for info in zin.infolist():
        if info.filename in SIG_FILES:
            continue
        data = manifest_bytes if info.filename == "AndroidManifest.xml" else zin.read(info.filename)
        zi = zipfile.ZipInfo(info.filename, date_time=info.date_time)
        zi.compress_type = info.compress_type
        zi.external_attr = info.external_attr
        zi.internal_attr = info.internal_attr
        zi.create_system = info.create_system
        zout.writestr(zi, data)

# ---- verification: only the manifest differs
znew = zipfile.ZipFile(OUT)
zold = zipfile.ZipFile(SRC)
old_infos = {i.filename: i for i in zold.infolist()}
new_list = znew.infolist()
assert len(new_list) == len(old_infos) - len(SIG_FILES), (len(new_list), len(old_infos))

replaced = 0
for i in new_list:
    o = old_infos[i.filename]
    if i.filename == "AndroidManifest.xml":
        assert znew.read(i.filename) == manifest_bytes, "manifest content mismatch"
        assert i.CRC != o.CRC, "manifest CRC unexpectedly unchanged"
        replaced += 1
    else:
        assert i.CRC == o.CRC, "entry changed: %s" % i.filename
        assert i.compress_type == o.compress_type, "compression changed: %s" % i.filename

print("rebuilt %s" % OUT)
print("entries: %d (original %d, removed %d signature files)" %
      (len(new_list), len(old_infos), len(SIG_FILES)))
print("manifest replaced: %d; all other entries CRC-identical: yes" % replaced)
