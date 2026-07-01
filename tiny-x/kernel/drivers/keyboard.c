/*
 * keyboard.c - PS/2 keyboard driver
 *
 * Handles IRQ1 (keyboard), translates AT scancode set 1 → ASCII,
 * stores characters in a circular buffer.
 */

#include "keyboard.h"
#include "../arch/x86_64/idt.h"
#include <stdint.h>

/* PS/2 ports */
#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

static inline uint8_t inb(uint16_t port)
{
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ── Circular key buffer ─────────────────────────────────────────────── */
static char kb_buf[KB_BUFSIZE];
static int  kb_head = 0;
static int  kb_tail = 0;

static void kb_push(char c)
{
    int next = (kb_head + 1) % KB_BUFSIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

static int kb_pop(void)
{
    if (kb_head == kb_tail) return -1;
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUFSIZE;
    return (unsigned char)c;
}

/* ── Scancode set 1 → ASCII table ────────────────────────────────────── */
static const char sc_normal[128] = {
    0,   27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,   'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,   '\\','z','x','c','v','b','n','m',',','.','/',
    0,   '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,  /* F1-F10 */
    0,   /* Num Lock */
    0,   /* Scroll Lock */
    '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0,
    0,0  /* F11-F12 */
};

static const char sc_shifted[128] = {
    0,   27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,   'A','S','D','F','G','H','J','K','L',':','"','~',
    0,   '|','Z','X','C','V','B','N','M','<','>','?',
    0,   '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.', 0,0,0,
    0,0
};

static int shift_held = 0;
static int ctrl_held  = 0;

static void kb_irq_handler(interrupt_frame_t *frame)
{
    (void)frame;
    uint8_t sc = inb(PS2_DATA);

    /* Key release: bit 7 set */
    if (sc & 0x80) {
        uint8_t rel = sc & 0x7F;
        if (rel == 0x2A || rel == 0x36) shift_held = 0;
        if (rel == 0x1D) ctrl_held = 0;
        return;
    }

    if (sc == 0x2A || sc == 0x36) { shift_held = 1; return; }
    if (sc == 0x1D) { ctrl_held  = 1; return; }

    if (sc >= 128) return;

    char c = shift_held ? sc_shifted[sc] : sc_normal[sc];
    if (!c) return;

    if (ctrl_held && c >= 'a' && c <= 'z') c -= 'a' - 1; /* Ctrl+letter */
    if (ctrl_held && c >= 'A' && c <= 'Z') c -= 'A' - 1;

    kb_push(c);
}

void keyboard_init(void)
{
    shift_held = 0;
    ctrl_held  = 0;
    kb_head = kb_tail = 0;
    idt_set_handler(IRQ_KEYBOARD, kb_irq_handler);

    /* Unmask IRQ1 in PIC */
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 1);
    __asm__ volatile("outb %0, %1" : : "a"(mask), "Nd"((uint16_t)0x21));
}

int keyboard_getchar(void)
{
    int c;
    while ((c = kb_pop()) < 0) {
        __asm__ volatile("hlt");  /* wait for interrupt */
    }
    return c;
}

int keyboard_poll(void)
{
    return kb_pop();
}
