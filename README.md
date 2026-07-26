# VorteX

A tiny, shell-first operating system skeleton written in C++ and designed for x86-compatible UEFI systems.

## What is included

- a simple UEFI entry point
- a minimal rEFInd-friendly EFI binary
- a placeholder shell banner for future kernel work

## Build requirements

You will need:

- a MinGW64 cross-compiler such as x86_64-w64-mingw32-g++
- the MinGW EFI toolchain: `x86_64-w64-mingw32-ld` and `x86_64-w64-mingw32-objcopy`
- rEFInd installed on the target machine to launch the EFI binary

## Build

```bash
make
```

This produces:

- build/efi/VorteX.efi

## Install for rEFInd

Copy the generated EFI binary to a rEFInd-visible location, for example:

```bash
mkdir -p /boot/efi/EFI/VorteX
cp build/efi/VorteX.efi /boot/efi/EFI/VorteX/VorteX.efi
```

Then reboot and select the entry from rEFInd.

## Notes

This is intentionally minimal and avoids GRUB because the original GRUB-based flow is not a good fit for a MinGW64 toolchain.
