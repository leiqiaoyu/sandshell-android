# ssprobe + parity (0.1.5 work)

- `ssprobe-arm64`: static, libc-free syscall probe. Bypasses every LD_PRELOAD shim, so it sees what Go / static Rust / Bun programs see. One line per test: OK / ERR / KILL (signal) / LEAK. Build: `gcc -O1 -static -nostdlib -fno-stack-protector -ffreestanding -fno-builtin -o ssprobe-arm64 ssprobe.c`.
- `parity.sh`: the same everyday Debian checks on Sandshell and on Termux + proot Debian. `bash parity.sh` checks; `bash parity.sh install` installs the toolset first (~1.5 GB).
