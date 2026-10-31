# 0.1.4: supervisor `openat` and `O_PATH`

**Symptom.** Claude Code 2.1.316 (a Bun-compiled native binary) started and chatted, but its Write and Bash tools hung. strace on the device showed every `openat(..., O_PATH ...)` on a translated path (`/root`, `/tmp/claude-0`, `/root/.claude`) returning `EBADF`.

**Cause.** The supervisor (`libdebian_runtime.so`) handles a mediated `openat` by opening the translated path itself with the caller's flags and injecting the descriptor with `SECCOMP_IOCTL_NOTIF_ADDFD`. The kernel's ADDFD path takes a real file reference, which `O_PATH` files do not provide, so the injection fails with `EBADF`. glibc programs rarely use `O_PATH`; Bun uses it for directory walks.

**Fix (binary patch, 0.1.4).** At 0xabd4 the handler's `mov w1, w21` (flags = notification `args[2]`) becomes `bl 0xd3a0`, a code cave in brk filler. The cave returns the flags unchanged unless `O_PATH` (0x200000) is set; then it clears `O_PATH` and the access mode and adds `O_NONBLOCK`, so the supervisor opens the target read-only without blocking on FIFOs, and ADDFD succeeds.

**Side effects / known limits.**
- `O_PATH|O_NOFOLLOW` on a symlink now yields `ELOOP` instead of a path descriptor.
- Targets the app cannot read (but could reach with `O_PATH`) fail with `EACCES`.
- ADDFD is called with `newfd_flags = 0`, so an injected descriptor loses `O_CLOEXEC` (pre-existing; planned for 0.1.5).
- `getcwd`/`realpath` can reveal the physical rootfs path (`/data/user/0/dev.sandshell.core/files/debian/...`) to native programs (planned for 0.1.5).

**Verified on device.** Claude Code Write, Bash, `apt install git` from inside Claude Code, git init/edit/commit/diff.
