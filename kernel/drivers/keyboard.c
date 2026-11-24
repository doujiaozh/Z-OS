#include "keyboard.h"
#include "input.h"
#include "io.h"
#include "pic.h"
#include "string.h"

static bool shift_down = false;
static bool caps_lock = false;
static bool control_down = false;
static bool extended = false;

static const char normal_map[128] = {
    [0x02] = '1', [0x03] = '2', [0x04] = '3', [0x05] = '4', [0x06] = '5',
    [0x07] = '6', [0x08] = '7', [0x09] = '8', [0x0A] = '9', [0x0B] = '0',
    [0x0C] = '-', [0x0D] = '=',
    [0x0E] = '\b', [0x0F] = '\t',
    [0x10] = 'q', [0x11] = 'w', [0x12] = 'e', [0x13] = 'r', [0x14] = 't',
    [0x15] = 'y', [0x16] = 'u', [0x17] = 'i', [0x18] = 'o', [0x19] = 'p',
    [0x1A] = '[', [0x1B] = ']', [0x1C] = '\n',
    [0x1E] = 'a', [0x1F] = 's', [0x20] = 'd', [0x21] = 'f', [0x22] = 'g',
    [0x23] = 'h', [0x24] = 'j', [0x25] = 'k', [0x26] = 'l',
    [0x27] = ';', [0x28] = '\'', [0x29] = '`',
    [0x2B] = '\\',
    [0x2C] = 'z', [0x2D] = 'x', [0x2E] = 'c', [0x2F] = 'v', [0x30] = 'b',
    [0x31] = 'n', [0x32] = 'm',
    [0x33] = ',', [0x34] = '.', [0x35] = '/',
    [0x39] = ' '
};

static const char shifted_map[128] = {
    [0x02] = '!', [0x03] = '@', [0x04] = '#', [0x05] = '$', [0x06] = '%',
    [0x07] = '^', [0x08] = '&', [0x09] = '*', [0x0A] = '(', [0x0B] = ')',
    [0x0C] = '_', [0x0D] = '+',
    [0x1A] = '{', [0x1B] = '}',
    [0x27] = ':', [0x28] = '"', [0x29] = '~',
    [0x2B] = '|',
    [0x33] = '<', [0x34] = '>', [0x35] = '?'
};

void keyboard_init(void) {
    shift_down = false;
    caps_lock = false;
    control_down = false;
    extended = false;
    while (inb(0x64) & 0x01) {
        inb(0x60);
    }
    pic_set_mask(PIC_IRQ_KEYBOARD, false);
}

static void handle_extended(u8 scancode) {
    if (scancode & 0x80) {
        return;
    }
    switch (scancode) {
        case 0x48:
            input_push(KEY_UP);
            break;
        case 0x50:
            input_push(KEY_DOWN);
            break;
        case 0x4B:
            input_push(KEY_LEFT);
            break;
        case 0x4D:
            input_push(KEY_RIGHT);
            break;
        case 0x47:
            input_push(KEY_HOME);
            break;
        case 0x4F:
            input_push(KEY_END);
            break;
        case 0x53:
            input_push(KEY_DELETE);
            break;
        default:
            break;
    }
}

void keyboard_irq_handler(void) {
    u8 scancode = inb(0x60);
    if (scancode == 0xE0) {
        extended = true;
        return;
    }
    if (scancode == 0xE1) {
        extended = false;
        return;
    }
    if (extended) {
        if (scancode == 0x1D) {
            control_down = true;
        } else if (scancode == 0x9D) {
            control_down = false;
        } else {
            handle_extended(scancode);
        }
        extended = false;
        return;
    }
    if (scancode & 0x80) {
        u8 released = (u8)(scancode & 0x7F);
        if (released == 0x2A || released == 0x36) {
            shift_down = false;
        } else if (released == 0x1D) {
            control_down = false;
        }
        return;
    }
    if (scancode == 0x2A || scancode == 0x36) {
        shift_down = true;
        return;
    }
    if (scancode == 0x3A) {
        caps_lock = !caps_lock;
        return;
    }
    if (scancode == 0x1D) {
        control_down = true;
        return;
    }
    if (scancode == 0x38 || scancode == 0x3B || scancode == 0x3C || scancode == 0x3D ||
        scancode == 0x3E || scancode == 0x3F || scancode == 0x40 || scancode == 0x41 ||
        scancode == 0x42 || scancode == 0x43 || scancode == 0x44 || scancode == 0x57 ||
        scancode == 0x58 || scancode == 0x45 || scancode == 0x46) {
        return;
    }
    char character = 0;
    if (scancode < 128) {
        char base = normal_map[scancode];
        bool letter = base >= 'a' && base <= 'z';
        if (letter) {
            bool uppercase = shift_down != caps_lock;
            character = uppercase ? to_upper(base) : base;
        } else if (shift_down && shifted_map[scancode] != 0) {
            character = shifted_map[scancode];
        } else {
            character = base;
        }
    }
    if (control_down && character >= 'a' && character <= 'z') {
        character = (char)(character - 'a' + 1);
    } else if (control_down && character >= 'A' && character <= 'Z') {
        character = (char)(character - 'A' + 1);
    }
    if (character != 0) {
        input_push(character);
    }
}
