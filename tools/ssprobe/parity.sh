#!/bin/bash
# parity.sh — run the same everyday-Debian checks on Sandshell and on Termux + proot Debian.
# Usage:  bash parity.sh            # check only (missing tools are reported as SKIP)
#         bash parity.sh install    # apt install the toolset first (~1.5 GB: gcc, go, rust, java...)
# Output: one line per check: PASS / FAIL / SKIP  name  [detail]; summary at the end.
set -u
W="${PARITY_DIR:-/tmp/parity.$$}"
mkdir -p "$W" && cd "$W" || exit 1
PKGS="build-essential make strace gdb golang-go rustc cargo default-jre-headless python3 python3-venv python3-pip sqlite3 tmux openssh-client procps lsof iputils-ping file git curl ca-certificates xz-utils zstd unzip locales man-db fakeroot"
if [ "${1:-}" = install ]; then
  if [ "$(id -u)" = 0 ]; then apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y $PKGS
  else sudo apt-get update && sudo DEBIAN_FRONTEND=noninteractive apt-get install -y $PKGS; fi
fi
P=0 F=0 S=0
res(){ case $1 in PASS) P=$((P+1));; FAIL) F=$((F+1));; SKIP) S=$((S+1));; esac; printf '%s\t%s\t%s\n' "$1" "$2" "${3:-}"; }
need(){ for c; do command -v "$c" >/dev/null 2>&1 || { res SKIP "$NAME" "no $c"; return 1; }; done; }
chk(){ # chk "name" cmd...   (PASS if exit 0 within 120 s)
  NAME=$1; shift
  local out; out=$(timeout 120 bash -c "$*" 2>&1); local rc=$?
  if [ $rc = 0 ]; then res PASS "$NAME"; else res FAIL "$NAME" "rc=$rc $(printf '%s' "$out" | tail -n 2 | tr '\n\t' '  ' | cut -c1-160)"; fi
}
echo "# parity 1 — $(uname -srm) — $(. /etc/os-release; echo "$PRETTY_NAME") — uid $(id -u) — dir $W"

# --- filesystem semantics
echo hi > a
chk "hard link (ln)"            'ln a b && [ "$(stat -c %h a)" = 2 ]'
chk "symlink rel + readlink"    'ln -sf a s && [ "$(readlink s)" = a ] && cat s'
chk "symlink abs + cat"         'ln -sf /etc/os-release s2 && cat s2 >/dev/null'
chk "rename over existing"      'echo x > c && mv -f c a && cat a'
chk "chmod +x / -w"             'echo "echo ok" > x.sh && chmod 755 x.sh && ./x.sh && chmod a-w x.sh && [ ! -w x.sh ] || [ "$(id -u)" = 0 ]'
chk "chown to self"             'chown "$(id -u):$(id -g)" a'
chk "chown root:root (fake root ok)" 'chown 0:0 a'
chk "mkfifo"                    'mkfifo ff && [ -p ff ]'
chk "tar -xpf keeps modes"      'mkdir -p t/d && echo 1 > t/d/f && chmod 750 t/d/f && tar cf t.tar t && rm -rf t && tar xpf t.tar && [ "$(stat -c %a t/d/f)" = 750 ]'
chk "touch -d (utimes)"         'touch -d "2020-01-01" a && [ "$(stat -c %Y a)" -lt 1600000000 ]'
chk "pwd -P is not physical"    'cd ~ && case "$(pwd -P)" in /data/*) exit 1;; esac'
chk "realpath ~ not physical"   'case "$(realpath ~)" in /data/*) exit 1;; esac'
chk "unix socket in /tmp"       'python3 -c "import socket,os;p=\"/tmp/ps.sock\";os.path.exists(p) and os.unlink(p);s=socket.socket(socket.AF_UNIX);s.bind(p);s.listen(1);c=socket.socket(socket.AF_UNIX);c.connect(p);print(1)"'
chk "inotify"                   'python3 -c "import ctypes,os;l=ctypes.CDLL(None);fd=l.inotify_init1(0);assert fd>=0;assert l.inotify_add_watch(fd,os.getcwd().encode(),0x100)>=0"'
chk "flock"                     'command -v flock >/dev/null && flock a true'

# --- process / system
chk "id shows root (like proot -0)" '[ "$(id -u)" = 0 ]'
chk "ps sees own processes"     'ps -e | grep -q bash'
chk "top -bn1"                  'top -bn1 | head -5'
chk "lsof -p \$\$"              'lsof -p $$ | head -3'
chk "nproc / free"              'nproc && free -m'
chk "background job + wait"     'sleep 0.2 & wait'
chk "TZ follows device"         '[ "$(date +%Z)" != UTC ]'
chk "locale C.UTF-8"            'LC_ALL=C.UTF-8 python3 -c "print(\"中文\")"'
NAME="ping localhost"; need ping && chk "$NAME" 'ping -c1 -W2 127.0.0.1'
chk "DNS + HTTPS"               'curl -fsS -o /dev/null https://deb.debian.org/'
chk "listen + connect TCP"      'python3 -m http.server 18765 >/dev/null 2>&1 & sleep 1.5; curl -fsS -o /dev/null http://127.0.0.1:18765/; r=$?; kill %1; exit $r'

# --- toolchains (install with: bash parity.sh install)
NAME="gcc hello";    need gcc && chk "$NAME" 'printf "#include <stdio.h>\nint main(){puts(\"ok\");}\n" > h.c && gcc h.c -o h && ./h'
NAME="gcc -static";  need gcc && chk "$NAME" 'gcc -static h.c -o hs && ./hs'
NAME="g++ threads";  need g++ && chk "$NAME" 'printf "#include <thread>\n#include <cstdio>\nint main(){std::thread t([]{puts(\"t\");});t.join();}\n" > t.cc && g++ -pthread t.cc -o tt && ./tt'
NAME="make";         need make && chk "$NAME" 'printf "all:\n\t@echo ok\n" > Makefile && make'
NAME="go run";       need go && chk "$NAME" 'mkdir -p gp && cd gp && printf "package main\nimport (\"fmt\";\"net/http\";\"os\")\nfunc main(){fmt.Println(\"ok\");_,e:=os.ReadDir(\"/root\");_=e;_=http.DefaultClient}\n" > m.go && GOCACHE=$PWD/.gc GOFLAGS=-mod=mod go run m.go'
NAME="go static net"; need go && chk "$NAME" 'cd gp && CGO_ENABLED=0 GOCACHE=$PWD/.gc go build -o gs m.go && ./gs'
NAME="rust cargo";   need cargo && chk "$NAME" 'cargo new -q rp 2>/dev/null; cd rp && CARGO_HOME=$PWD/.ch cargo run -q'
NAME="java";         need java && chk "$NAME" 'printf "public class J{public static void main(String[] a){System.out.println(\"ok\");}}\n" > J.java && java J.java'
NAME="python venv+pip"; need python3 && chk "$NAME" 'python3 -m venv v && v/bin/pip install -q --disable-pip-version-check six && v/bin/python -c "import six"'
NAME="python multiprocessing"; need python3 && chk "$NAME" 'python3 -c "import multiprocessing as m; print(m.Pool(2).map(abs,[-1,-2]))"'
NAME="node + npm i"; need node npm && chk "$NAME" 'mkdir -p np && cd np && npm init -y >/dev/null && npm i -q left-pad >/dev/null 2>&1 && node -e "require(\"left-pad\")"'
NAME="sqlite3";      need sqlite3 && chk "$NAME" 'sqlite3 db "create table t(x);insert into t values(1);select * from t;"'
NAME="git clone https"; need git && chk "$NAME" 'git clone -q --depth 1 https://github.com/git/htmldocs gd'

# --- debugging & tools
NAME="strace -f";    need strace && chk "$NAME" 'strace -f -o /dev/null true'
NAME="gdb run";      need gdb gcc && chk "$NAME" 'gcc -g h.c -o hg && gdb -batch -ex run -ex bt ./hg'
NAME="tmux";         need tmux && chk "$NAME" 'tmux -L par new -d "sleep 2" && tmux -L par ls && tmux -L par kill-server'
NAME="ssh-keygen";   need ssh-keygen && chk "$NAME" 'ssh-keygen -q -t ed25519 -N "" -f k'
NAME="fakeroot";     need fakeroot && chk "$NAME" 'fakeroot sh -c "chown 0:0 a && [ \$(stat -c %u a) = 0 ]"'
NAME="man";          need man && chk "$NAME" 'man -P cat ls >/dev/null'
NAME="file";         need file && chk "$NAME" 'file /bin/ls'
NAME="xz/zstd";      need xz zstd && chk "$NAME" 'echo hi | xz | xz -d && echo hi | zstd | zstd -d'

echo "# summary: PASS $P  FAIL $F  SKIP $S"
cd / && rm -rf "$W"
