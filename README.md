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

## Install rEFInd Boot Manager

### On Windows
To install rEFInd on a Windows machine, open **Command Prompt as Administrator** and follow these steps:

1. Mount your EFI System Partition (ESP) to a temporary drive letter (e.g., `Z:`):
   ```cmd
   mountvol Z: /S
   ```
2. Download the rEFInd binary zip from the official site and extract it.
3. Navigate into the extracted directory and copy the rEFInd files to your EFI partition:
   ```cmd
   xcopy /E refind Z:\EFI\refind\
   ```
4. Register rEFInd as the default boot entry using `bcdedit`:
   ```cmd
   bcdedit /set {bootmgr} path \EFI\refind\refind_x64.efi
   ```

### On Linux
```bash
sudo refind-install
```

## Install VorteX for rEFInd

### On Windows
Keep your Administrator Command Prompt open (with the ESP mounted as `Z:`) and run:
```cmd
mkdir Z:\EFI\VorteX
copy build\efi\VorteX.efi Z:\EFI\VorteX\VorteX.efi
```

### On Linux
Copy the generated EFI binary to a rEFInd-visible location, for example:
```bash
mkdir -p /boot/efi/EFI/VorteX
cp build/efi/VorteX.efi /boot/efi/EFI/VorteX/VorteX.efi
```

Then reboot and select the entry from rEFInd.

## Notes

This is intentionally minimal and avoids GRUB because the original GRUB-based flow is not a good fit for a MinGW64 toolchain.
