# VNL — Vibe Not Linux

A 64-bit x86_64 Unix-like hobby kernel written from scratch in C and NASM assembly.

---

## Features

### Boot & CPU
- **Multiboot2** compliant — boots from GRUB2 or any Multiboot2 loader
- **32→64-bit transition** — CPUID long-mode check, PAE, `IA32_EFER.LME`, paging enable
- **Higher-half kernel** — VMA `0xFFFFFFFF80000000`, LMA `0x100000`
- **4-level page tables** — 1 GiB huge pages for boot, 4 KiB pages via VMM
- **GDT** — kernel/user code+data, 64-bit TSS
- **IDT** — 256 vectors, C dispatch table, exception handler with register dump
- **8259A PIC** — IRQ 0-15 remapped to INT 32-47
- **Syscall interface** — `INT 0x80` dispatcher (12 syscalls)

### Memory
- **Physical Memory Manager** — bitmap allocator, 4 KiB frames, `pmm_reserve()` protects kernel image
- **Virtual Memory Manager** — 4-level page table walk, `vmm_map/unmap/get_phys`
- **Kernel heap** — boundary-tag allocator (`kmalloc/kfree/krealloc/kcalloc`), at `0xFFFFC00000000000`

### Scheduler
- **Preemptive round-robin** — custom `sched_timer_stub` (NASM) swaps RSP between `PUSH_REGS` and `iretq`; 1 ms timeslice via PIT at 1000 Hz
- **Kernel threads** — `task_create(name, fn)`, heap-allocated 32 KiB stacks, fake interrupt frame bootstrap
- **Voluntary yield** — `INT 0x81` (`sched_yield_stub`)
- **`task_sleep(ms)`** — tick-based sleep, woken by scheduler
- **`task_exit()`** — marks task dead, yields immediately

### Drivers

| Driver | Details |
|---|---|
| VGA | 80×25 text mode, scrolling, hardware cursor |
| Serial | COM1 at 38400 baud (debug console in QEMU via `-serial stdio`) |
| PIT Timer | 1000 Hz, `timer_ticks()` returns milliseconds since boot |
| PS/2 Keyboard | Scancode set 1, E0-prefix extended keys (arrows, Home, End, Del, PgUp/PgDn) |
| PCI | Config-space enumeration of all buses/devices/functions, class string table |
| ACPI | RSDP → RSDT → FADT → DSDT `_S5_` parse; `acpi_shutdown()` |

### Filesystem
- **ramfs VFS** — up to 256 flat nodes with parent-inode path resolution
- **Operations** — `open/read/write/close/mkdir/unlink/readdir/stat/getcwd`
- **File descriptors** — fd 0/1/2 = stdin/stdout/stderr; fds 3+ are VFS files
- **Pre-created directories** — `/bin`, `/etc`, `/tmp`, `/home`

### Shell

A full POSIX-subset shell interpreter built into the kernel (`src/shell/sh.c`).

**Language features:**
- Variables: `VAR=val`, `$VAR`, `${VAR}`, `${VAR:-default}`, `${#VAR}`
- Special vars: `$?`, `$#`, `$0`–`$9`, `$*`, `$@`, `$$`, `$RANDOM`
- Quoting: `'single'`, `"double"` (expands `$var`), `\escape`
- Command substitution: `$(cmd)` and `` `cmd` ``
- Arithmetic: `$((expr))` — `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `~`, `!`, `<<`, `>>`
- Pipelines: `cmd1 | cmd2 | cmd3`
- Redirects: `>`, `>>`, `<`, `2>`, `2>&1`
- Conditionals: `if / elif / else / fi`
- Loops: `while / do / done`, `for var in list; do / done`
- Case statements: `case $x in pat) ;; esac`
- Functions: `name() { ... }` and `function name { ... }`
- Logical: `&&`, `||`
- Line continuation: `\` at end of line
- Builtins: `echo` (`-n`/`-e`), `printf`, `read`, `export`, `unset`, `shift`, `alias`, `type`, `which`, `env`, `test`/`[`, `true`, `false`, `break`, `continue`, `return`, `source`/`.`, `exit`

**Pipeline utilities:** `grep`, `wc`, `head`, `tail`, `sort`, `uniq`, `tr`, `tee`

**VNL shell commands:**

| Command | Description |
|---|---|
| `help` | List all commands |
| `uname` | Kernel version string |
| `mem` | Physical memory statistics |
| `uptime` | Time since boot |
| `clear` | Clear the screen |
| `echo` | Print text |
| `color <fg> <bg>` | Set VGA color (0–15) |
| `hello` | ASCII art banner |
| `ls [path]` | List directory |
| `cat <file>` | Print file contents |
| `write <file> <text>` | Write text to file |
| `mkdir <dir>` | Create directory |
| `rm <path>` | Remove file or empty directory |
| `cd <path>` | Change directory |
| `pwd` | Print working directory |
| `ps` | List scheduler tasks |
| `kill <pid>` | Kill a task |
| `sleep <ms>` | Sleep N milliseconds |
| `lspci` | List PCI devices |
| `poweroff` | Shutdown via ACPI |
| `reboot` | Reset via keyboard controller |
| `panic [msg]` | Trigger kernel panic |
| `halt` | Halt the CPU |
| `sh [file]` | Start sh session or run script |
| `bash [file]` | Start bash session or run script |
| `eval <script>` | Execute a shell string |
| `source <file>` | Run a script in current context |

### Syscalls

| # | Name | Description |
|---|---|---|
| 1 | `SYS_WRITE` | Write to fd (1/2 = VGA, 3+ = VFS) |
| 2 | `SYS_READ` | Read from fd (0 = keyboard, 3+ = VFS) |
| 3 | `SYS_OPEN` | Open or create a file |
| 4 | `SYS_CLOSE` | Close a file descriptor |
| 7 | `SYS_MKDIR` | Create a directory |
| 8 | `SYS_UNLINK` | Remove a file or empty directory |
| 39 | `SYS_GETPID` | Get current task PID |
| 60 | `SYS_EXIT` | Exit current task |
| 63 | `SYS_UNAME` | Copy kernel version string |
| 100 | `SYS_UPTIME` | Get tick count (ms since boot) |
| 101 | `SYS_TASK_CREATE` | Create a kernel thread |
| 103 | `SYS_TASK_SLEEP` | Sleep N milliseconds |

Calling convention: `rax` = syscall number, `rdi`/`rsi`/`rdx` = args, return value in `rax`.

---

## Building

### Requirements

- `nasm`
- `x86_64-elf-gcc` or system `gcc` (Makefile detects automatically)
- `x86_64-elf-ld` (cross binutils)
- `grub-mkrescue` + `xorriso` (for ISO)
- `qemu-system-x86_64` (for running)

On Arch Linux:
```bash
sudo pacman -S nasm qemu-system-x86_64 grub xorriso
# cross toolchain (if not already installed):
# build from source or use AUR: cross-x86_64-elf-gcc
```

### Commands

```bash
make              # build kernel ELF → build/vnl.kernel
make iso          # build bootable ISO → vnl.iso
make run          # boot ISO in QEMU, serial → terminal
make run-kernel   # boot kernel directly (no GRUB)
make clean        # remove build artifacts
```

---

## Running

```bash
make run
```

QEMU boots VNL, serial output goes to your terminal. VGA output is in the QEMU window. Both outputs are always active simultaneously.

### Example session

```
vnl:/# write /tmp/hello.sh echo hello from sh
vnl:/# sh /tmp/hello.sh
hello from sh

vnl:/# bash
bash interactive session. Type 'exit' to return.
root@vnl:/# for i in 1 2 3; do echo "item $i"; done
item 1
item 2
item 3
root@vnl:/# exit

vnl:/# ls | grep tmp
tmp/
vnl:/# mem
Total: 255 MiB  Used: 1 MiB  Free: 254 MiB
vnl:/# lspci
00:00.0 8086:1237 [06.00] Host Bridge
00:01.0 8086:7000 [06.01] ISA Bridge
...
vnl:/# poweroff
Shutting down...
```

---

## Source Layout

```
src/
  boot/boot.asm          Multiboot2 header, 32→64 mode, initial page tables
  cpu/
    cpu.asm              Port I/O, MSR, CR regs, GDT/IDT flush
    isr_stubs.asm        256 ISR stubs + isr_stub_table[]
    sched.asm            sched_timer_stub / sched_yield_stub (RSP swap)
    gdt.c                GDT + TSS init
    idt.c                IDT init, idt_set_handler, idt_set_raw_handler
    irq.c                8259A PIC remapping (IRQ→INT 32-47)
  drivers/
    vga.c                80×25 VGA text driver
    serial.c             COM1 38400 baud
    timer.c              PIT 1000 Hz
    keyboard.c           PS/2 keyboard + E0 extended keys
    pci.c                PCI bus enumeration
    acpi.c               RSDP/FADT/DSDT parse, shutdown
  mm/
    pmm.c                Bitmap physical frame allocator
    vmm.c                4-level page table walk
    heap.c               Boundary-tag kmalloc/kfree
  fs/
    vfs.c                Flat-node ramfs + file descriptor table
  kernel/
    main.c               kernel_main() boot sequence
    panic.c              kpanic() — red screen + halt
    sched.c              Round-robin scheduler, task lifecycle
    syscall.c            INT 0x80 dispatcher
  shell/
    shell.c              VNL interactive shell + all built-in commands
    sh.c                 POSIX sh / bash interpreter (~1400 lines)
  lib/
    string.c             Standard string/memory functions
    printf.c             kprintf / kvprintf / kvsnprintf

include/                 All headers
linker.ld                Kernel memory layout
Makefile
iso/boot/grub/grub.cfg   GRUB2 config
```

---

## Architecture Notes

- **Kernel VMA:** `0xFFFFFFFF80000000` — higher half of the 64-bit address space
- **Boot mapping:** PML4[0] identity-maps 0–4 GiB (1 GiB huge pages); PML4[511] maps KERNEL_VMA→physical 0
- **Heap:** starts at `0xFFFFC00000000000`, grows by mapping PMM frames via VMM
- **Boot stack:** 64 KiB in `.bss`, covered by the boot 1 GiB huge page
- **Scheduler context switch:** `sched_timer_stub` raw-installs at IDT vector 32, performs the full PUSH_REGS save, calls `sched_timer_c()` which returns the new RSP (or 0 for no switch), then MOV RSP, RAX before POP_REGS + IRETQ
- **Interrupt frame layout:** 26×8 bytes — `gs/fs/es/ds`, `r15`–`rax`, `int_no`, `err_code`, `rip/cs/rflags/rsp/ss`
- **New task bootstrap:** `task_create` pre-fills a fake interrupt frame on the new stack so the first IRETQ jumps to the entry function with IF=1

---

## License

Do whatever you want with it. It's a vibe.
# VNL
# Computer-Melter
