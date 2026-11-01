#!/bin/bash
# sstrap device test — run in /tmp with sstrap, hello-static, ptest, loop, ssprobe-arm64 present
cd "$(dirname "$0")"
echo "# $(uname -r) $(date)"
echo "== 1 hello-static direct (expect SIGSYS death)"; ./hello-static; echo "rc=$?"
echo "== 2 hello-static via sstrap (threads, cwd, exe)"; ./sstrap ./hello-static; echo "rc=$?"
echo "== 3 static exec chain"; ./sstrap ./hello-static x; echo "rc=$?"
echo "== 4 ptest direct"; rm -rf /root/.sst; ./ptest; echo "rc=$?"
echo "== 5 ptest via sstrap"; rm -rf /root/.sst; ./sstrap ./ptest; echo "rc=$?"; ls -la /root/.sst; rm -rf /root/.sst
echo "== 6 ssprobe via sstrap"; ./sstrap ./ssprobe-arm64 /tmp/ssprobe.s
echo "== 7 bash under sstrap"; ./sstrap /bin/bash -c 'echo bash $BASH_VERSION; cd /root && pwd; x=$(/bin/echo sub ok); echo $x; readlink /proc/self/exe; ls / | head -3'; echo "rc=$?"
echo "== 8 cost per intercepted call"; ./loop; ./sstrap ./loop
echo "== 9 exec cost (300x /bin/true)"
s=$(date +%s%N); for i in $(seq 300); do /bin/true; done; e=$(date +%s%N); echo "direct: $(( (e-s)/300000 )) us/exec"
./sstrap /bin/bash -c 's=$(date +%s%N); for i in $(seq 300); do /bin/true; done; e=$(date +%s%N); echo "sstrap: $(( (e-s)/300000 )) us/exec"'
echo "== 10 debug trace of hello-static"; SSTRAP_DEBUG=1 ./sstrap ./hello-static 2>&1 | head -40
echo "== 11 exec debug"; SSTRAP_DEBUG=1 ./sstrap /bin/bash -c "/bin/true; echo rc=\$?" 2>&1 | grep -v "nr="
echo "# done"
