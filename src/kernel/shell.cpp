#include "kernel/shell.hpp"
#include "kernel/console.hpp"
#include "kernel/keyboard.hpp"
#include <stdint.h>

namespace {
    static void bench_cpu() {
        // Integer sum
        volatile uint64_t sum = 0;
        for (int i = 0; i < 500000; i++) sum += i;
        (void)sum;

        // Prime finder
        volatile int pc = 0;
        for (int n = 2; n < 10000; n++) {
            int ip = 1;
            for (int d = 2; d * d <= n; d++) {
                if (n % d == 0) { ip = 0; break; }
            }
            if (ip) pc++;
        }
        (void)pc;

        console::write("CPU bench done (");
        console::write("primes: ");

        // Manual int to string
        char buf[12];
        int pos = 11;
        buf[11] = '\0';
        int tmp = pc;
        if (tmp == 0) buf[--pos] = '0';
        while (tmp > 0 && pos > 0) {
            buf[--pos] = '0' + (tmp % 10);
            tmp /= 10;
        }
        console::write(buf + pos);
        console::write(")\n");
    }

    static void handle_cmd(const char* line, int len) {
        if (len == 0) return;

        // Copy to buffer
        char cmd[64];
        int clen = len < 63 ? len : 63;
        for (int i = 0; i < clen; i++) cmd[i] = line[i];
        cmd[clen] = '\0';

        // Skip leading spaces
        const char* p = cmd;
        while (*p == ' ') p++;

        if (p[0] == '\0') return;

        if (p[0] == 'b' && p[1] == 'e' && p[2] == 'n' && p[3] == 'c' && p[4] == 'h' && p[5] == '\0') {
            bench_cpu();
        } else if (p[0] == 'h' && p[1] == 'e' && p[2] == 'l' && p[3] == 'p' && p[4] == '\0') {
            console::write("Commands: bench, help, ver\n");
        } else if (p[0] == 'v' && p[1] == 'e' && p[2] == 'r' && p[3] == '\0') {
            console::write("VorteX Kernel v0.1\n");
        } else if (p[0] == 'c' && p[1] == 'l' && p[2] == 'e' && p[3] == 'a' && p[4] == 'r' && p[5] == '\0') {
            console::clear();
        } else {
            console::write("Unknown: ");
            console::write(p);
            console::write("\n");
        }
    }
}

namespace shell {
    void run() {
        console::init();
        keyboard::init();
        console::write("VorteX Kernel Shell v0.2\n");
        console::write("Type 'help' for commands\n\n");

        char line[128];
        int len = 0;

        console::write("> ");

        for (;;) {
            char c = keyboard::read_char();
            if (c == 0) {
                continue;
            }

            if (c == '\n') {
                console::write("\n");
                line[len] = '\0';
                handle_cmd(line, len);
                len = 0;
                console::write("> ");
            } else if (c == '\b') {
                if (len > 0) {
                    len--;
                    console::putchar('\b');
                    console::putchar(' ');
                    console::putchar('\b');
                }
            } else if (c >= ' ' && len < 126) {
                line[len++] = c;
                console::putchar(c);
            }
        }
    }
}
