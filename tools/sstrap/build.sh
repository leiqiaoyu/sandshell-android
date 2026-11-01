#!/bin/sh
set -e
cd "$(dirname "$0")"
CF="-O2 -static -nostdlib -fno-stack-protector -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-pie -no-pie -fno-asynchronous-unwind-tables"
gcc $CF -Wall -Wno-unused-function -Wno-misleading-indentation -Wl,-Ttext-segment=0x6000000000 -Wl,--build-id=none -o sstrap sstrap.c
gcc $CF -Wall -Wno-unused-function -Wno-misleading-indentation -Wl,-Ttext-segment=0x6000000000 -Wl,--build-id=none -o sstrap-dbg sstrap-dbg.c
strip sstrap
gcc -O1 -static -nostdlib -fno-stack-protector -ffreestanding -o fakedroid fakedroid.c
