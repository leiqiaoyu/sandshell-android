#!/bin/sh
# install-opencode.sh — one-shot installer for opencode v1.18.x (musl route) inside Sandshell.
#
# Reproduce on a fresh Sandshell 0.1 device:
#   0) You already have the user-supplied musl Node (see case 3) => $D/usr/local/node-musl/
#   1) Download opencode-linux-arm64-musl-<version>.tgz (npm registry / any mirror)
#      and place it as /mnt/sdcard/oc-musl.tgz
#   2) In the Sandshell Debian terminal:  sh /mnt/sdcard/install-opencode.sh
#
# Exit codes: 0 ok; 1 prerequisites missing.
set +e
D=/data/user/0/dev.sandshell.core/files/debian
B=/mnt/sdcard
OC=$D/usr/local/ocp
P=$D/usr/local/node-musl
L="$P/lib/ld-musl-aarch64.so.1"
fail() { echo "[oc-install] FAIL: $1"; exit 1; }

echo '[1/5] prerequisites'
[ -f "$L" ] || fail "missing $L - install the musl Node first (see case 3)"
[ -f "$D/usr/local/lib/noshm3.so" ] || fail "missing noshm3.so under $D/usr/local/lib/"

echo '[2/5] package'
T="$B/oc-musl.tgz"
[ -f "$T" ] || fail "place opencode-linux-arm64-musl tgz at $T"

echo '[3/5] install binary'
mkdir -p "$OC" 2>/dev/null
cd "$OC" || fail "cannot cd $OC"
cp -f "$T" ./oc-musl.tgz || fail "copy failed"
tar -xzf ./oc-musl.tgz || fail "extract failed"
cp -f ./package/bin/opencode "$OC/opencode-musl" || fail "place failed"
chmod 755 "$OC/opencode-musl"
rm -rf ./package ./oc-musl.tgz

echo '[4/5] install wrapper'
mkdir -p "$D/usr/local/bin" 2>/dev/null
cat > "$D/usr/local/bin/opencode" <<'WRAPEOF'
#!/bin/sh
D=/data/user/0/dev.sandshell.core/files/debian
P=$D/usr/local/node-musl
OC=$D/usr/local/ocp
[ -d "$OC/home" ] || mkdir -p "$OC/home" 2>/dev/null
HOME="$OC/home" LD_PRELOAD="$D/usr/local/lib/noshm3.so" exec "$P/lib/ld-musl-aarch64.so.1" --library-path "$P/lib" "$OC/opencode-musl" "$@"
WRAPEOF
chmod 755 "$D/usr/local/bin/opencode"

echo '[5/5] verify'
opencode --version || fail 'version check failed'
echo '[oc-install] OK'