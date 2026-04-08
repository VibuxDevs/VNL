#include "elf_load.h"
#include "vmm.h"
#include "pmm.h"
#include "string.h"
#include "heap.h"
#include "vfs.h"
#include "uspace.h"
#include "sched.h"
#include "printf.h"
#include "errno.h"

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} Elf64_Phdr;

#define PT_LOAD 1
#define EM_X86_64 62
#define ET_EXEC 2

void vnl_copy_to_user_va(uint64_t pml4, uint64_t dst_va, const void *src, size_t n)
{
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0; i < n; i++) {
        uint64_t va = dst_va + i;
        uint64_t pa = vmm_lookup_phys_pml4(pml4, va);
        if (!pa)
            return;
        *(uint8_t *)(uintptr_t)pa = s[i];
    }
}

int elf_load_exec(uint64_t pml4_phys, const void *elf_data, size_t elf_len,
                  uint64_t *out_entry, uint64_t *out_brk)
{
    if (elf_len < sizeof(Elf64_Ehdr))
        return -ENOEXEC;
    const Elf64_Ehdr *eh = (const Elf64_Ehdr *)elf_data;
    if (eh->e_ident[0] != 0x7f || eh->e_ident[1] != 'E' || eh->e_ident[2] != 'L' ||
        eh->e_ident[3] != 'F')
        return -ENOEXEC;
    if (eh->e_ident[4] != 2 || eh->e_type != ET_EXEC || eh->e_machine != EM_X86_64)
        return -ENOEXEC;
    if (eh->e_phentsize != sizeof(Elf64_Phdr) || eh->e_phnum == 0)
        return -ENOEXEC;
    if (eh->e_phoff + (uint64_t)eh->e_phnum * sizeof(Elf64_Phdr) > elf_len)
        return -ENOEXEC;

    uint64_t max_end = 0;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        const Elf64_Phdr *ph =
            (const Elf64_Phdr *)((const uint8_t *)elf_data + eh->e_phoff +
                                 (uint64_t)i * eh->e_phentsize);
        if (ph->p_type != PT_LOAD)
            continue;

        uint64_t v0 = ph->p_vaddr & ~(PAGE_SIZE - 1);
        uint64_t v1 = ALIGN_UP(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);
        if (v1 < v0)
            return -EINVAL;

        for (uint64_t va = v0; va < v1; va += PAGE_SIZE) {
            void *f = pmm_alloc();
            if (!f)
                return -ENOMEM;
            memset(f, 0, PAGE_SIZE);
            vmm_map_in_pml4(pml4_phys, va, (uint64_t)f,
                            VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
        }

        if (ph->p_offset + ph->p_filesz > elf_len)
            return -ENOEXEC;

        if (ph->p_filesz)
            vnl_copy_to_user_va(pml4_phys, ph->p_vaddr,
                                (const uint8_t *)elf_data + ph->p_offset,
                                (size_t)ph->p_filesz);

        if (ph->p_vaddr + ph->p_memsz > max_end)
            max_end = ph->p_vaddr + ph->p_memsz;
    }

    *out_entry = eh->e_entry;
    *out_brk = ALIGN_UP(max_end, PAGE_SIZE);
    return 0;
}

int vnl_spawn_elf_path(const char *path)
{
    int fd = vfs_open(path, VFS_O_READ);
    if (fd < 0)
        return -ENOENT;

    const size_t cap = 512 * 1024;
    char *buf = (char *)kmalloc(cap);
    if (!buf) {
        vfs_close(fd);
        return -ENOMEM;
    }
    int n = vfs_read(fd, buf, cap);
    vfs_close(fd);
    if (n < 64) {
        kfree(buf);
        return -ENOEXEC;
    }

    uint64_t pml4 = uspace_create_pml4();
    if (!pml4) {
        kfree(buf);
        return -ENOMEM;
    }
    uint64_t entry, brk;
    if (elf_load_exec(pml4, buf, (size_t)n, &entry, &brk) < 0) {
        kfree(buf);
        return -ENOEXEC;
    }
    kfree(buf);

    for (size_t i = 0; i < VNL_USER_STACK_PAGES; i++) {
        void *pg = pmm_alloc();
        if (!pg)
            return -ENOMEM;
        vmm_map_in_pml4(pml4, VNL_USER_STACK_BASE + i * PAGE_SIZE, (uint64_t)pg,
                        VMM_FLAG_PRESENT | VMM_FLAG_WRITE | VMM_FLAG_USER);
    }

    uint64_t rsp = VNL_USER_STACK_BASE + VNL_USER_STACK_PAGES * PAGE_SIZE - 16;
    return task_create_user("Xorg", pml4, entry, rsp, brk, brk);
}
