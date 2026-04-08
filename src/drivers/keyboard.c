#include "keyboard.h"
#include "idt.h"
#include "cpu.h"

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

static const char sc_lower[128] = {
    0, 27,'1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};
static const char sc_upper[128] = {
    0, 27,'!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
};

#define KBD_BUF_SIZE 256
static volatile uint8_t  kbd_buf[KBD_BUF_SIZE];
static volatile uint32_t kbd_head = 0, kbd_tail = 0;
static volatile bool     shift_held = false;
static volatile bool     caps_lock  = false;
static volatile bool     e0_pending = false;

static void kbd_push(uint8_t c)
{
    uint32_t next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next != kbd_tail) { kbd_buf[kbd_head] = c; kbd_head = next; }
}

static void kbd_handle_byte(uint8_t sc)
{
    if (sc == 0xE0) {
        e0_pending = true;
        return;
    }

    bool released = (sc & 0x80) != 0;
    sc &= 0x7F;

    if (e0_pending) {
        e0_pending = false;
        if (!released) {
            uint8_t ext = 0;
            switch (sc) {
                case 0x48: ext = KEY_UP;    break;
                case 0x50: ext = KEY_DOWN;  break;
                case 0x4B: ext = KEY_LEFT;  break;
                case 0x4D: ext = KEY_RIGHT; break;
                case 0x47: ext = KEY_HOME;  break;
                case 0x4F: ext = KEY_END;   break;
                case 0x53: ext = KEY_DEL;   break;
                case 0x49: ext = KEY_PGUP;  break;
                case 0x51: ext = KEY_PGDN;  break;
            }
            if (ext) kbd_push(ext);
        }
        return;
    }

    if (sc == 0x2A || sc == 0x36) {
        shift_held = !released;
        return;
    }
    if (!released && sc == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }

    if (!released && sc < 128) {
        /*
         * Caps Lock must only toggle A–Z. XOR with shift on the whole table
         * breaks the number row: '1' becomes '!' while caps is on.
         */
        char c;
        if (shift_held)
            c = sc_upper[sc];
        else {
            c = sc_lower[sc];
            if (caps_lock && c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
        }
        if (c) kbd_push((uint8_t)c);
    }
}

static void keyboard_irq(Registers *r)
{
    (void)r;

    /* PS/2 shares 0x60 with the aux (mouse). Status bit 5 = aux data.
     * Decoding mouse bytes as scancodes fills the buffer with junk. */
    for (int n = 0; n < 32; n++) {
        uint8_t st = inb(KBD_STATUS);
        if (!(st & 0x01))
            break;
        uint8_t data = inb(KBD_DATA);
        if (st & 0x20)
            continue;
        kbd_handle_byte(data);
    }

    outb(0x20, 0x20);
}

void keyboard_init(void)  { idt_set_handler(33, keyboard_irq); }

int keyboard_poll(void)
{
    if (kbd_head == kbd_tail) return -1;
    uint8_t c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return (int)c;
}

int keyboard_getkey(void)
{
    int c;
    while ((c = keyboard_poll()) < 0) asm volatile("hlt");
    return c;
}

char keyboard_getchar(void)
{
    int c;
    while ((c = keyboard_getkey()) >= KEY_UP);
    return (char)c;
}
