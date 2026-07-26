CXX      ?= x86_64-w64-mingw32-g++
CC       ?= x86_64-w64-mingw32-gcc
AS       ?= nasm

EFI_CXXFLAGS ?= -Iinclude/efi -std=c++17 -fno-stack-protector \
                -ffreestanding -fno-builtin -fshort-wchar \
                -mno-red-zone -mno-stack-arg-probe \
                -Wall -Wextra -Werror \
                -fno-exceptions -fno-rtti -fno-unwind-tables \
                -fno-asynchronous-unwind-tables -fno-threadsafe-statics

BUILD_DIR := build
EFI_DIR   := $(BUILD_DIR)/efi
KERNEL_DIR := $(BUILD_DIR)/kernel

.PHONY: all clean efi kernel

all: efi

# ── EFI target (rEFInd / UEFI) ──────────────────────────────────

efi: $(EFI_DIR)/VorteX.efi

$(EFI_DIR) $(KERNEL_DIR):
	mkdir -p $@

$(EFI_DIR)/VorteX.efi: src/efi/uefi_main.cpp | $(EFI_DIR)
	$(CXX) $(EFI_CXXFLAGS) \
	    -nostdlib \
	    -Wl,-dll \
	    -Wl,--subsystem,10 \
	    -Wl,--image-base,0 \
	    -Wl,-e,efi_main \
	    $< -o $@

# ── Legacy kernel target (GRUB / multiboot2) ────────────────────

KERNEL_CXXFLAGS ?= -Iinclude/kernel -std=c++17 -ffreestanding \
                   -fno-exceptions -fno-rtti -fno-builtin \
                   -m32 -mno-sse -Wall -Wextra

KERNEL_LDFLAGS  ?= -m elf_i386 -nostdlib -T linker.ld

kernel: $(KERNEL_DIR)/kernel.elf

$(KERNEL_DIR)/boot.o: src/boot/boot.asm | $(KERNEL_DIR)
	$(AS) -f elf32 $< -o $@

$(KERNEL_DIR)/kernel.o: src/kernel/kernel.cpp | $(KERNEL_DIR)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

$(KERNEL_DIR)/console.o: src/kernel/console.cpp | $(KERNEL_DIR)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

$(KERNEL_DIR)/keyboard.o: src/kernel/keyboard.cpp | $(KERNEL_DIR)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

$(KERNEL_DIR)/shell.o: src/kernel/shell.cpp | $(KERNEL_DIR)
	$(CXX) $(KERNEL_CXXFLAGS) -c $< -o $@

$(KERNEL_DIR)/kernel.elf: $(KERNEL_DIR)/boot.o $(KERNEL_DIR)/kernel.o \
                          $(KERNEL_DIR)/console.o $(KERNEL_DIR)/keyboard.o \
                          $(KERNEL_DIR)/shell.o
	$(LD) $(KERNEL_LDFLAGS) $^ -o $@

# ── Clean ───────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR)
