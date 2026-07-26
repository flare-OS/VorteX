#include "kernel/console.hpp"
#include <stdint.h>

namespace {
    volatile uint16_t* video_memory = reinterpret_cast<volatile uint16_t*>(0xB8000);
    const int width = 80;
    const int height = 25;
    int cursor_x = 0;
    int cursor_y = 0;

    void newline() {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= height) {
            cursor_y = height - 1;
            for (int y = 1; y < height; ++y) {
                for (int x = 0; x < width; ++x) {
                    video_memory[(y - 1) * width + x] = video_memory[y * width + x];
                }
            }
            for (int x = 0; x < width; ++x) {
                video_memory[(height - 1) * width + x] = 0x0720;
            }
        }
    }
}

namespace console {
    void init() {
        clear();
    }

    void putchar(char c) {
        if (c == '\n') {
            newline();
            return;
        }
        if (c == '\r') {
            cursor_x = 0;
            return;
        }
        if (c == '\b') {
            if (cursor_x > 0) {
                cursor_x--;
                video_memory[cursor_y * width + cursor_x] = 0x0720;
            }
            return;
        }

        if (cursor_x >= width) {
            newline();
        }

        video_memory[cursor_y * width + cursor_x++] = static_cast<uint16_t>(0x0700 | static_cast<unsigned char>(c));
    }

    void write(const char* str) {
        for (const char* p = str; *p; ++p) {
            putchar(*p);
        }
    }

    void clear() {
        for (int i = 0; i < width * height; ++i) {
            video_memory[i] = 0x0720;
        }
        cursor_x = 0;
        cursor_y = 0;
    }
}
