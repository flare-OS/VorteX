#include "kernel/shell.hpp"
#include <stdint.h>

extern "C" void kernel_main(uint32_t magic, uint32_t mbi) {
    (void)magic;
    (void)mbi;
    shell::run();
}
