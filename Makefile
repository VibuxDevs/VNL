# Use native gcc (x86_64-pc-linux-gnu) with freestanding flags.
# x86_64-elf-gcc is preferred if installed; fall back to system gcc.
CC         := $(shell command -v x86_64-elf-gcc 2>/dev/null || echo gcc)
AS         := nasm
LD         := $(shell command -v x86_64-elf-ld 2>/dev/null || echo ld)

CFLAGS     := -std=gnu11 -ffreestanding -O2 -Wall -Wextra \
              -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
              -mcmodel=kernel -Iinclude \
              -fno-stack-protector -fno-builtin -fno-pic
ASFLAGS    := -f elf64
LDFLAGS    := -T linker.ld -nostdlib -z max-page-size=0x1000

SRCDIR     := src
BUILDDIR   := build

ASM_SRCS   := $(shell find $(SRCDIR) -name '*.asm')
C_SRCS     := $(shell find $(SRCDIR) -name '*.c')

ASM_OBJS   := $(patsubst $(SRCDIR)/%.asm,$(BUILDDIR)/%.asm.o,$(ASM_SRCS))
C_OBJS     := $(patsubst $(SRCDIR)/%.c,$(BUILDDIR)/%.c.o,$(C_SRCS))

KERNEL     := $(BUILDDIR)/vnl.kernel
ISO        := vnl.iso

.PHONY: all clean iso run

all: $(KERNEL)

$(KERNEL): $(ASM_OBJS) $(C_OBJS)
	@mkdir -p $(BUILDDIR)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "[LD]  $@"

$(BUILDDIR)/%.asm.o: $(SRCDIR)/%.asm
	@mkdir -p $(dir $@)
	$(AS) $(ASFLAGS) $< -o $@
	@echo "[AS]  $<"

$(BUILDDIR)/%.c.o: $(SRCDIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "[CC]  $<"

iso: $(KERNEL)
	cp $(KERNEL) iso/boot/vnl.kernel
	grub-mkrescue -o $(ISO) iso
	@echo "[ISO] $(ISO)"

run: iso
	qemu-system-x86_64 -cdrom $(ISO) -m 256M -serial stdio -no-reboot -no-shutdown

run-kernel: $(KERNEL)
	qemu-system-x86_64 -kernel $(KERNEL) -m 256M -serial stdio -no-reboot -no-shutdown

clean:
	rm -rf $(BUILDDIR) $(ISO)
