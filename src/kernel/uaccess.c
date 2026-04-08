#include "uaccess.h"
#include "vmm.h"
#include "errno.h"

int copy_user_cstring(uint64_t cr3_phys, const void *user_ptr, char *out, size_t out_cap)
{
    if (!out || out_cap < 2)
        return -EINVAL;
    uint64_t uva = (uint64_t)(uintptr_t)user_ptr;
    for (size_t i = 0; i < out_cap - 1; i++) {
        uint64_t pa = vmm_lookup_phys_pml4(cr3_phys, uva + i);
        if (!pa)
            return -EFAULT;
        if (pa >= 0x100000000ULL)
            return -EFAULT;
        char c = *(const char *)(uintptr_t)pa;
        out[i] = c;
        if (c == '\0')
            return 0;
    }
    out[out_cap - 1] = '\0';
    return -E2BIG;
}
