#pragma once

#include "efi.h"

// ── Output ──────────────────────────────────────────────────────

inline void Print(const wchar_t* str) {
    if (gConOut) gConOut->OutputString(gConOut, str);
}

inline void PrintASCII(const char* str) {
    wchar_t buf[512];
    wchar_t* p = buf;
    while (*str && (p - buf) < 510)
        *p++ = static_cast<wchar_t>(static_cast<unsigned char>(*str++));
    *p = L'\0';
    Print(buf);
}

inline void PrintInt(uint64_t val) {
    wchar_t buf[24];
    int pos = 23;
    buf[pos] = L'\0';
    if (val == 0) buf[--pos] = L'0';
    while (val > 0) {
        buf[--pos] = L'0' + (val % 10);
        val /= 10;
    }
    Print(buf + pos);
}

inline void PrintHex(uint64_t val) {
    wchar_t buf[20];
    int pos = 19;
    buf[pos] = L'\0';
    if (val == 0) buf[--pos] = L'0';
    while (val > 0) {
        int d = val & 0xF;
        buf[--pos] = d < 10 ? L'0' + d : L'A' + d - 10;
        val >>= 4;
    }
    Print(buf + pos);
}

inline void PrintHexPad(uint64_t val, int pad) {
    wchar_t buf[20];
    int pos = 19;
    buf[pos] = L'\0';
    for (int i = 0; i < pad && val == 0; i++) {
        buf[--pos] = L'0';
    }
    while (val > 0) {
        int d = val & 0xF;
        buf[--pos] = d < 10 ? L'0' + d : L'A' + d - 10;
        val >>= 4;
    }
    while (19 - pos < pad) buf[--pos] = L'0';
    Print(buf + pos);
}

// ── Screen ──────────────────────────────────────────────────────

inline void ClearScreen() {
    if (gConOut) gConOut->ClearScreen(gConOut);
}

inline void SetAttribute(uint64_t attr) {
    if (gConOut) gConOut->SetAttribute(gConOut, attr);
}

#define EFI_TEXT_BLACK        0x00
#define EFI_TEXT_BLUE         0x01
#define EFI_TEXT_GREEN        0x02
#define EFI_TEXT_CYAN         0x03
#define EFI_TEXT_RED          0x04
#define EFI_TEXT_MAGENTA      0x05
#define EFI_TEXT_BROWN        0x06
#define EFI_TEXT_LIGHTGRAY    0x07
#define EFI_TEXT_BRIGHT       0x08
#define EFI_TEXT_DARKGRAY     0x78
#define EFI_TEXT_LIGHTBLUE    0x09
#define EFI_TEXT_LIGHTGREEN   0x0A
#define EFI_TEXT_LIGHTCYAN    0x0B
#define EFI_TEXT_LIGHTRED     0x0C
#define EFI_TEXT_LIGHTMAGENTA 0x0D
#define EFI_TEXT_YELLOW       0x0E
#define EFI_TEXT_WHITE        0x0F

#define EFI_ATTR(fg, bg)      (((bg) << 4) | (fg))

// ── Input ───────────────────────────────────────────────────────

inline bool ReadKey(EFI_INPUT_KEY* key) {
    if (!gConIn || !gBS) return false;
    uint64_t index;
    EFI_STATUS status = gBS->WaitForEvent(1, &gConIn->WaitForKey, &index);
    if (status != EFI_SUCCESS) return false;
    return gConIn->ReadKeyStroke(gConIn, key) == EFI_SUCCESS;
}

// ── GUID Helpers ────────────────────────────────────────────────

inline bool GuidCmp(const EFI_GUID* a, const EFI_GUID* b) {
    return a->Data1 == b->Data1 && a->Data2 == b->Data2 &&
           a->Data3 == b->Data3 &&
           a->Data4[0] == b->Data4[0] && a->Data4[1] == b->Data4[1] &&
           a->Data4[2] == b->Data4[2] && a->Data4[3] == b->Data4[3] &&
           a->Data4[4] == b->Data4[4] && a->Data4[5] == b->Data4[5] &&
           a->Data4[6] == b->Data4[6] && a->Data4[7] == b->Data4[7];
}

// ── Initialization ──────────────────────────────────────────────

inline void InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* SystemTable) {
    (void)ImageHandle;
    gST = SystemTable;
    gConOut = SystemTable->ConOut;
    gConIn = SystemTable->ConIn;
    gBS = SystemTable->BootServices;
}
