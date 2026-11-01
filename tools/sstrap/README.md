# sstrap — in-process syscall interception (PoC for Sandshell 0.1.5)

Static / non-glibc programs (Go, static Rust, Bun, `gcc -static`) skip the LD_PRELOAD shim, so on Android they
hit unmediated paths, ENOSYS, and SIGSYS kills (rseq, set_robust_list, accept, setuid, shmget, mount...).

`sstrap PROGRAM ARGS` loads the program itself (userland ELF loader, incl. PT_INTERP and `#!`), keeps its own code
mapped at 0x60_0000_0000, installs a SIGSYS handler and a seccomp filter that TRAPs selected syscalls unless they come
from sstrap's own code (`seccomp_data.instruction_pointer`). Android's own TRAPs land in the same handler.
Emulation happens inside the process: no ptrace, no helper process. `execve` re-enters sstrap so children stay covered.

Handled: statx, faccessat2, renameat2, fchmodat2, openat2, memfd_create (emulated); chdir, truncate, *xattr,
inotify_add_watch, mknodat, symlinkat, linkat, AF_UNIX bind/connect (path translated to the physical rootfs);
getcwd, readlink /proc/self/{exe,cwd,root,fd/N} (physical prefix stripped); rseq, set_robust_list, io_uring_setup,
futex_waitv -> ENOSYS; accept -> accept4; set*id -> 0 (fake root); mount -> EPERM; shmget -> ENOSYS (TODO).
SIGSYS is kept deliverable (rt_sigaction / rt_sigprocmask intercepted).

Known PoC limits: absolute symlinks inside the rootfs are resolved physically by unmediated calls; shmget not emulated;
execveat with dirfd only for absolute paths; /proc/self/exe of the loader itself visible in /proc/PID/maps.

Sandbox (Linux 6.12, `fakedroid` = Android-like filter): static glibc + threads + exec chain work; ssprobe goes from
13 fail/kill to Termux-proot level; ~1.3 us extra per intercepted call; exec ~2x slower (double exec).

Device test: `bash sstrap-test.sh > sstrap-result.txt 2>&1`
