/*
 * VNL kernel printf — minimal but useful implementation.
 * Supports: %d %i %u %x %X %o %s %c %p %llu %lld %zu %b (binary)
 * Flags:    width, '0' padding, '-' left-align, '+' sign
 * Output goes to both VGA and serial.
 */
#include "printf.h"
#include "vga.h"
#include "serial.h"
#include "string.h"

#define va_arg    __builtin_va_arg

/* ---- Output redirection hook -------------------------------------
 * When non-NULL, kputchar writes to this buffer instead of VGA+serial.
 * Used by the sh pipe implementation to capture command output.
 */
static char   *kout_buf  = NULL;
static size_t  kout_len  = 0;
static size_t  kout_cap  = 0;

void kout_redirect(char *buf, size_t cap) { kout_buf = buf; kout_len = 0; kout_cap = cap; }
void kout_reset(void)                     { kout_buf = NULL; kout_len = 0; kout_cap = 0; }
size_t kout_written(void)                 { return kout_len; }

/* ---- Output sink ------------------------------------------------- */
static void kputchar(char c)
{
    if (kout_buf) {
        if (kout_len + 1 < kout_cap) {
            kout_buf[kout_len++] = c;
            kout_buf[kout_len]   = '\0';
        }
        return;
    }
    /* Serial first: real hardware may have no usable VGA text (UEFI GOP-only). */
    serial_putchar(c);
    vga_putchar(c);
}

static void kputs_raw(const char *s)
{
    while (*s) kputchar(*s++);
}

/* ---- Number formatting ------------------------------------------- */
static void fmt_uint(char *buf, int *pos, uint64_t val, int base,
                     bool upper, int width, char pad, bool left,
                     bool sign_plus, bool negative)
{
    static const char *dig_low = "0123456789abcdef";
    static const char *dig_up  = "0123456789ABCDEF";
    const char *dig = upper ? dig_up : dig_low;

    char tmp[66];
    int  len = 0;

    if (val == 0) {
        tmp[len++] = '0';
    } else {
        while (val) {
            tmp[len++] = dig[val % (uint64_t)base];
            val /= (uint64_t)base;
        }
    }

    char prefix[3] = {0};
    int  pfx_len = 0;
    if (negative)       prefix[pfx_len++] = '-';
    else if (sign_plus) prefix[pfx_len++] = '+';

    int total = len + pfx_len;
    int padding = (width > total) ? (width - total) : 0;

    if (!left && pad == ' ')
        for (int i = 0; i < padding; i++) buf[(*pos)++] = ' ';

    for (int i = 0; i < pfx_len; i++) buf[(*pos)++] = prefix[i];

    if (!left && pad == '0')
        for (int i = 0; i < padding; i++) buf[(*pos)++] = '0';

    for (int i = len - 1; i >= 0; i--) buf[(*pos)++] = tmp[i];

    if (left)
        for (int i = 0; i < padding; i++) buf[(*pos)++] = ' ';
}

/* ---- Core formatter ---------------------------------------------- */
int kvsnprintf(char *out, size_t out_size, const char *fmt, va_list ap)
{
    char   buf[2048];
    int    pos = 0;

#define EMIT(c) do { if ((size_t)pos < sizeof(buf)-1) buf[pos++] = (c); } while(0)

    while (*fmt && (size_t)pos < sizeof(buf) - 1) {
        if (*fmt != '%') { EMIT(*fmt++); continue; }
        fmt++;  /* skip '%' */

        /* Flags */
        bool left = false, sign_plus = false, alt = false;
        char pad = ' ';
        while (*fmt == '-' || *fmt == '+' || *fmt == '0' || *fmt == '#') {
            if (*fmt == '-') left = true;
            if (*fmt == '+') sign_plus = true;
            if (*fmt == '0') pad = '0';
            if (*fmt == '#') alt = true;
            fmt++;
        }
        (void)alt;

        /* Width */
        int width = 0;
        if (*fmt == '*') { width = va_arg(ap, int); fmt++; }
        else while (*fmt >= '0' && *fmt <= '9') width = width * 10 + (*fmt++ - '0');

        /* Length modifier */
        bool is_long = false;
        bool is_ll   = false;
        if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { is_ll = true; fmt++; }
            else is_long = true;
        } else if (*fmt == 'z') {
            is_ll = true; fmt++;
        }

        char spec = *fmt++;
        switch (spec) {
            case 'c':
                EMIT((char)va_arg(ap, int));
                break;
            case 's': {
                const char *s = va_arg(ap, const char *);
                if (!s) s = "(null)";
                int slen = (int)strlen(s);
                int pad_n = (width > slen) ? width - slen : 0;
                if (!left) for (int i = 0; i < pad_n; i++) EMIT(' ');
                while (*s) EMIT(*s++);
                if ( left) for (int i = 0; i < pad_n; i++) EMIT(' ');
                break;
            }
            case 'd': case 'i': {
                int64_t v;
                if (is_ll || is_long) v = va_arg(ap, int64_t);
                else v = va_arg(ap, int);
                bool neg = (v < 0);
                uint64_t uv = neg ? (uint64_t)-v : (uint64_t)v;
                fmt_uint(buf, &pos, uv, 10, false, width, pad, left, sign_plus, neg);
                break;
            }
            case 'u': {
                uint64_t v;
                if (is_ll || is_long) v = va_arg(ap, uint64_t);
                else v = (uint32_t)va_arg(ap, unsigned int);
                fmt_uint(buf, &pos, v, 10, false, width, pad, left, false, false);
                break;
            }
            case 'x': case 'X': {
                uint64_t v;
                if (is_ll || is_long) v = va_arg(ap, uint64_t);
                else v = (uint32_t)va_arg(ap, unsigned int);
                fmt_uint(buf, &pos, v, 16, spec == 'X', width, pad, left, false, false);
                break;
            }
            case 'o': {
                uint64_t v;
                if (is_ll || is_long) v = va_arg(ap, uint64_t);
                else v = (uint32_t)va_arg(ap, unsigned int);
                fmt_uint(buf, &pos, v, 8, false, width, pad, left, false, false);
                break;
            }
            case 'b': {
                uint64_t v = va_arg(ap, uint64_t);
                fmt_uint(buf, &pos, v, 2, false, width, pad, left, false, false);
                break;
            }
            case 'p': {
                uint64_t v = (uint64_t)va_arg(ap, void *);
                EMIT('0'); EMIT('x');
                fmt_uint(buf, &pos, v, 16, false, 16, '0', false, false, false);
                break;
            }
            case '%':
                EMIT('%');
                break;
            case 'n':
                /* ignored for safety */
                break;
            default:
                EMIT('%');
                EMIT(spec);
        }
    }
    buf[pos] = '\0';

    if (out) {
        size_t n = (size_t)pos < out_size ? (size_t)pos : out_size - 1;
        memcpy(out, buf, n);
        out[n] = '\0';
    } else {
        kputs_raw(buf);
    }
    return pos;
}

/* ---- Public API -------------------------------------------------- */

int kvprintf(const char *fmt, va_list ap)
{
    return kvsnprintf(NULL, 0, fmt, ap);
}

int kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    return n;
}

int ksprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

void kputs(const char *s)
{
    kputs_raw(s);
    kputchar('\n');
}
