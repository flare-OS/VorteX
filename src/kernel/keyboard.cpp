#include "kernel/keyboard.hpp"
#include "kernel/console.hpp"
#include <stdint.h>

namespace {
    constexpr uint16_t kPortData = 0x60;
    constexpr uint16_t kPortStatus = 0x64;

    bool keyboard_ready() {
        return (reinterpret_cast<volatile uint8_t*>(kPortStatus))[0] & 0x01;
    }

    uint8_t read_scancode() {
        while (!keyboard_ready()) {
            __asm__ volatile("pause");
        }
        return reinterpret_cast<volatile uint8_t*>(kPortData)[0];
    }
}

namespace keyboard {
    void init() {
        (void)read_scancode();
    }

    char read_char() {
        uint8_t sc = read_scancode();
        switch (sc) {
            case 0x1C: return '\n';
            case 0x0E: return '\b';
            case 0x1D: return 0;
            case 0x39: return ' ';
            case 0x0F: return '\t';
            default:
                if (sc >= 0x02 && sc <= 0x0C) return "1234567890"[sc - 0x02];
                if (sc >= 0x10 && sc <= 0x1B) return "qwertyuiop"[sc - 0x10];
                if (sc >= 0x1E && sc <= 0x26) return "asdfghjkl"[sc - 0x1E];
                if (sc >= 0x2C && sc <= 0x32) return "zxcvbnm"[sc - 0x2C];
                return 0;
        }
    }
}
