#include "mouse.h"
#include "cpu.h"
#include "idt.h"
#include "irq.h"

#define KBC_DATA   0x60
#define KBC_STATUS 0x64
#define KBC_CMD    0x64

#define MOUSE_QUEUE 64
#define KBC_IO_TRIES 400000

static volatile struct {
    int8_t dx;
    int8_t dy;
    uint8_t buttons;
} s_q[MOUSE_QUEUE];
static volatile uint32_t s_head;
static volatile uint32_t s_tail;

static uint8_t s_pkt[3];
static uint32_t s_pkt_len;
static bool s_mouse_ready;

static void kbc_wait_write(void)
{
    for (int n = 0; n < KBC_IO_TRIES; n++) {
        if (!(inb(KBC_STATUS) & 0x02))
            return;
    }
}

/* Returns true only if the output buffer had real data. */
static bool kbc_try_read(uint8_t *out)
{
    for (int n = 0; n < KBC_IO_TRIES; n++) {
        if (inb(KBC_STATUS) & 0x01) {
            *out = inb(KBC_DATA);
            return true;
        }
    }
    return false;
}

static bool kbc_write_cfg(uint8_t cfg)
{
    kbc_wait_write();
    outb(KBC_CMD, 0x60);
    kbc_wait_write();
    outb(KBC_DATA, cfg);
    return true;
}

static bool mouse_write_cmd(uint8_t cmd)
{
    kbc_wait_write();
    outb(KBC_CMD, 0xD4);
    kbc_wait_write();
    outb(KBC_DATA, cmd);
    uint8_t ack;
    if (!kbc_try_read(&ack) || ack != 0xFA)
        return false;
    return true;
}

static void mouse_irq(Registers *r)
{
    (void)r;
    if (!s_mouse_ready)
        goto done;
    while (inb(KBC_STATUS) & 0x01) {
        uint8_t st = inb(KBC_STATUS);
        uint8_t b  = inb(KBC_DATA);
        if (!(st & 0x20)) /* keyboard byte */
            continue;

        if (s_pkt_len == 0) {
            if (!(b & 0x08))
                continue;
        }

        s_pkt[s_pkt_len++] = b;
        if (s_pkt_len < 3)
            continue;

        s_pkt_len = 0;
        uint8_t flags = s_pkt[0];
        int8_t dx = (int8_t)s_pkt[1];
        int8_t dy = (int8_t)s_pkt[2];
        uint8_t bt = flags & 7;
        uint32_t n = (s_head + 1) % MOUSE_QUEUE;
        if (n == s_tail)
            continue;
        s_q[s_head].dx = dx;
        s_q[s_head].dy = dy;
        s_q[s_head].buttons = bt;
        s_head = n;
    }
done:
    irq_eoi(12);
}

void mouse_flush(void)
{
    s_head = s_tail = 0;
    s_pkt_len = 0;
    for (int i = 0; i < 64 && (inb(KBC_STATUS) & 0x01); i++)
        (void)inb(KBC_DATA);
}

bool mouse_poll(int *dx, int *dy, uint8_t *buttons)
{
    if (s_tail == s_head)
        return false;
    *dx = s_q[s_tail].dx;
    *dy = s_q[s_tail].dy;
    *buttons = s_q[s_tail].buttons;
    s_tail = (s_tail + 1) % MOUSE_QUEUE;
    return true;
}

void mouse_init(void)
{
    irq_mask(12);
    s_mouse_ready = false;
    s_head = s_tail = 0;
    s_pkt_len = 0;
    mouse_flush();

    kbc_wait_write();
    outb(KBC_CMD, 0x20);
    uint8_t cfg_orig;
    if (!kbc_try_read(&cfg_orig))
        return;

    kbc_wait_write();
    outb(KBC_CMD, 0xA8);

    uint8_t cfg = cfg_orig;
    cfg |= 0x02;
    cfg &= (uint8_t)~0x20;

    mouse_flush();
    if (!kbc_write_cfg(cfg))
        return;

    mouse_flush();
    if (!mouse_write_cmd(0xF6) || !mouse_write_cmd(0xF4)) {
        kbc_write_cfg(cfg_orig);
        return;
    }

    s_mouse_ready = true;
    idt_set_handler(44, mouse_irq);
    irq_unmask(12);
}
