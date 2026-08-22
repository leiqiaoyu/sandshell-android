#!/usr/bin/env python3
"""Final verification of the signed v0.1 APK (2026-08-23 revision,
MANAGE_EXTERNAL_STORAGE retained for the /mnt/sdcard interop bridge)."""
import struct
import zipfile
from pyaxmlparser.axmlprinter import AXMLPrinter

FINAL = "signed/debian-mobile-v0.1-aligned-signed.apk"
ORIG = "part58.apk"

z = zipfile.ZipFile(FINAL)
zo = zipfile.ZipFile(ORIG)
ok = []

# ---- 1. manifest checks
xml = AXMLPrinter(z.read("AndroidManifest.xml")).get_xml().decode()
assert 'android:versionName="0.1"' in xml, "versionName"
assert 'android:debuggable="false"' in xml, "debuggable"
assert 'android:versionCode="1002"' in xml, "versionCode"
assert 'android:sharedUserId="com.debian.runtime"' in xml, "sharedUserId"
assert 'android:targetSdkVersion="28"' in xml, "targetSdk"
assert 'android:minSdkVersion="24"' in xml, "minSdk"
import re
perms = re.findall(r'uses-permission android:name="([^"]+)"', xml)
assert len(perms) == 8, perms
assert 'android.permission.MANAGE_EXTERNAL_STORAGE' in perms, "interop bridge permission must be present"
assert 'android.permission.SYSTEM_ALERT_WINDOW' not in xml
assert 'android.permission.READ_LOGS' not in xml
assert 'android.permission.DUMP' not in xml
assert 'android.permission.WRITE_SECURE_SETTINGS' not in xml
assert 'android.permission.REQUEST_INSTALL_PACKAGES' not in xml
assert 'android.permission.PACKAGE_USAGE_STATS' not in xml
assert 'com.android.alarm.permission.SET_ALARM' not in xml
assert 'com.termux.app.activities.SettingsActivity" android:exported="false"' in xml, "SettingsActivity"
assert 'com.termux.app.RunCommandService" android:permission="com.debian.runtime.permission.RUN_COMMAND" android:exported="false"' in xml, "RunCommandService"
assert 'com.debian.runtime.MainActivity" android:exported="false"' in xml, "MainActivity internal"
assert 'android.intent.category.LAUNCHER' in xml, "launcher intact"
ok.append("manifest: version 0.1, debuggable=false, 8 permissions (bridge kept), exports locked")

# ---- 2. resources.arsc stored + 4-byte aligned
i = z.getinfo("resources.arsc")
assert i.compress_type == zipfile.ZIP_STORED, "arsc must be stored"
with open(FINAL, "rb") as f:
    f.seek(i.header_offset)
    lh = f.read(30)
    fn_len, extra_len = struct.unpack("<HH", lh[26:30])
    data_off = i.header_offset + 30 + fn_len + extra_len
assert data_off % 4 == 0, "arsc not 4-aligned (%d)" % data_off
ok.append("resources.arsc: stored, data offset 4-byte aligned (%d)" % data_off)

# ---- 3. every payload entry byte-identical to audited Part58
old = {x.filename: x for x in zo.infolist()}
skipped_meta = 0
for x in z.infolist():
    if x.filename == "AndroidManifest.xml":
        continue
    if x.filename.startswith("META-INF/"):
        skipped_meta += 1
        continue
    o = old.get(x.filename)
    assert o is not None, "new entry: %s" % x.filename
    assert o.CRC == x.CRC, "content changed: %s" % x.filename
    assert o.compress_type == x.compress_type, "compression changed: %s" % x.filename
ok.append("payload: %d entries CRC-identical to Part58 (dex/native libs/assets/resources)"
          % (len(z.infolist()) - skipped_meta - 1))

# ---- 4. v1 signature files present?
meta_root = sorted(x.filename for x in z.infolist()
                   if x.filename.startswith("META-INF/") and x.filename.count("/") == 1)
assert "META-INF/MANIFEST.MF" in meta_root
assert any(m.endswith(".SF") for m in meta_root) and any(m.endswith(".RSA") for m in meta_root)
ok.append("v1 JAR signature files present (MANIFEST.MF / DEBIAN-M.SF / DEBIAN-M.RSA)")

# ---- 5. APK Signing Block (v2/v3) present before central directory
with open(FINAL, "rb") as f:
    f.seek(-22, 2)
    eocd = f.read()
    cd_off = struct.unpack("<I", eocd[16:20])[0]
    f.seek(cd_off - 16)
    magic = f.read(16)
assert magic == b"APK Sig Block 42", magic
ok.append("APK Signing Block (v2/v3) present")

import hashlib
h = hashlib.sha256(open(FINAL, "rb").read()).hexdigest()
ok.append("sha256: %s" % h)

print("\n".join("PASS  " + line for line in ok))
print("\nALL FINAL CHECKS PASSED")
