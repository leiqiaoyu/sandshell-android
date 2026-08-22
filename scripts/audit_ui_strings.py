#!/usr/bin/env python3
"""UI localization audit: default (en) vs zh-rCN string resources."""
import re
from xml.etree import ElementTree as ET

BASE = "decompiled/resources/res"
CJK = re.compile(r"[\u4e00-\u9fff]")


def load_strings(path):
    tree = ET.parse(path)
    out = {}
    for el in tree.getroot():
        if el.tag.endswith("string") and "name" in el.attrib:
            out[el.attrib["name"]] = "".join(el.itertext()).strip()
    return out


default = load_strings(f"{BASE}/values/strings.xml")
zh = load_strings(f"{BASE}/values-zh-rCN/strings.xml")

print("default(en) strings : %d" % len(default))
print("zh-rCN strings      : %d" % len(zh))

# keys present in zh but missing in default (orphan / stale)
orphan = sorted(set(zh) - set(default))
print("\nzh keys with no default counterpart: %d" % len(orphan))

# untranslated: in default, missing in zh, or value still equals English
untranslated, same_value = [], []
for k, v in sorted(default.items()):
    if k not in zh:
        untranslated.append(k)
    elif zh[k] == v and re.search(r"[A-Za-z]{3}", v):
        same_value.append(k)
print("keys missing zh translation : %d" % len(untranslated))
print("keys zh==en (untranslated)   : %d" % len(same_value))

translated = [k for k in default if k in zh and zh[k] != default[k]]
print("actually translated          : %d (%.0f%% of default keys)" %
      (len(translated), 100.0 * len(translated) / len(default)))

# Chinese present in default (en) locale -> mixed-language UI
zh_in_default = [k for k, v in default.items() if CJK.search(v)]
print("\nChinese text inside default(en) locale: %d" % len(zh_in_default))
for k in zh_in_default[:20]:
    print("   %-40s %r" % (k, default[k][:60]))

# mojibake check across both locales
MOJI = re.compile(r"[ÃÂï¿½]")
moji = [(k, v) for k, v in list(default.items()) + list(zh.items()) if MOJI.search(v)]
print("suspected mojibake entries: %d" % len(moji))

# app-specific strings (termux_ / debian)
print("\n--- app-specific keys (termux_*) and their zh status ---")
app_keys = [k for k in default if k.startswith("termux_") or "debian" in k.lower()]
for k in sorted(app_keys):
    status = "ZH" if k in zh else "--"
    print("   [%s] %-45s en=%r" % (status, k, default[k][:50]))
    if k in zh:
        print("        %s zh=%r" % (" " * len(k), zh[k][:50]))

print("\napp_name(en) = %r" % default.get("app_name"))
print("app_name(zh) = %r" % zh.get("app_name"))
