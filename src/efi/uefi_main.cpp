#include "efi.h"
#include "efilib.h"
#include <cstddef>

EFI_SYSTEM_TABLE* gST = nullptr;
EFI_BOOT_SERVICES* gBS = nullptr;
EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* gConOut = nullptr;
EFI_SIMPLE_TEXT_INPUT_PROTOCOL* gConIn = nullptr;

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE*);
static void* _efi_reloc_anchor __attribute__((used, section(".data"))) = (void*)efi_main;

// ── String helpers ──────────────────────────────────────────────

static int wcslen(const wchar_t* s) {
    int n = 0;
    while (*s++) { ++n; }
    return n;
}
static bool wcscmp(const wchar_t* a, const wchar_t* b) {
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void wcscpy(wchar_t* d, const wchar_t* s) {
    while ((*d++ = *s++)) {}
}
static void wcscat(wchar_t* d, const wchar_t* s) {
    d += wcslen(d);
    wcscpy(d, s);
}
static bool wcsnicmp(const wchar_t* a, const wchar_t* b, int n) {
    for (int i = 0; i < n; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
    return true;
}

// ── Command history ─────────────────────────────────────────────

#define HIST_MAX 16
static wchar_t hist_buf[HIST_MAX][256];
static int hist_count = 0;
static int hist_display = 0;

static void hist_add(const wchar_t* line) {
    if (line[0] == L'\0') return;
    if (hist_count > 0 && wcscmp(hist_buf[hist_count - 1], line)) return;
    if (hist_count < HIST_MAX) {
        wcscpy(hist_buf[hist_count], line);
        hist_count++;
    } else {
        for (int i = 1; i < HIST_MAX; i++) wcscpy(hist_buf[i - 1], hist_buf[i]);
        wcscpy(hist_buf[HIST_MAX - 1], line);
    }
    hist_display = hist_count;
}

static void hist_up(wchar_t* line, int* pos, int* len) {
    if (hist_display <= 0) return;
    hist_display--;
    for (int i = 0; i < *len; i++) Print(L"\b \b");
    wcscpy(line, hist_buf[hist_display]);
    *len = wcslen(line); *pos = *len; Print(line);
}
static void hist_down(wchar_t* line, int* pos, int* len) {
    if (hist_display >= hist_count - 1) {
        hist_display = hist_count;
        for (int i = 0; i < *len; i++) Print(L"\b \b");
        line[0] = L'\0'; *pos = 0; *len = 0; return;
    }
    hist_display++;
    for (int i = 0; i < *len; i++) Print(L"\b \b");
    wcscpy(line, hist_buf[hist_display]);
    *len = wcslen(line); *pos = *len; Print(line);
}

// ── Forward declarations ────────────────────────────────────────

static EFI_FILE_PROTOCOL* open_root_volume();
static void print_prompt();

// ── Current directory ───────────────────────────────────────────

static wchar_t cwd[256] = L"\\";

static void cwd_clean() {
    if (cwd[0] != L'\\') { wcscpy(cwd, L"\\"); return; }
    int l = wcslen(cwd);
    if (l > 1 && cwd[l - 1] == L'\\') cwd[l - 1] = L'\0';
}

static void build_full_path(const wchar_t* rel, wchar_t* out) {
    if (rel[0] == L'\\') { wcscpy(out, rel); return; }
    wcscpy(out, cwd);
    if (out[wcslen(out) - 1] != L'\\') wcscat(out, L"\\");
    wcscat(out, rel);
}

// ── Known commands ──────────────────────────────────────────────

static const wchar_t* commands[] = {
    L"help", L"h", L"clear", L"cls", L"echo", L"ver", L"reboot",
    L"info", L"ls", L"cat", L"time", L"gop", L"color",
    L"history", L"cd", L"pwd", L"memmap",
    L"hexdump", L"mkdir", L"rm", L"pci",
    L"devices", L"bench", L"bench3d", L"realbench", L"pager",
    nullptr
};

// ── Tab completion (double-tab shows all) ───────────────────────

static void do_tab(wchar_t* line, int* pos, int* len, bool* tab_pressed) {
    if (*len == 0) return;
    int first = -1, match_count = 0;
    for (int i = 0; commands[i] != nullptr; i++) {
        if (wcsnicmp(commands[i], line, *len)) {
            if (first < 0) first = i;
            match_count++;
        }
    }
    if (match_count == 0) return;

    if (match_count == 1) {
        const wchar_t* cmd = commands[first];
        int cl = wcslen(cmd);
        for (int i = 0; i < *len; i++) Print(L"\b \b");
        wcscpy(line, cmd); line[cl] = L' ';
        *len = cl + 1; *pos = *len; Print(line);
        *tab_pressed = false;
    } else if (*tab_pressed) {
        Print(L"\r\n");
        for (int i = 0; commands[i] != nullptr; i++) {
            if (wcsnicmp(commands[i], line, *len)) {
                Print(commands[i]); Print(L"  ");
            }
        }
        Print(L"\r\n"); print_prompt(); Print(line);
        *tab_pressed = false;
    } else {
        *tab_pressed = true;
    }
}

// ── Pager state ─────────────────────────────────────────────────

static bool pager_enabled = false;
static int  pager_lines = 0;
static void pager_check() {
    if (!pager_enabled) return;
    pager_lines++;
    if (pager_lines >= 23) {
        pager_lines = 0;
        Print(L"--- More (press any key) ---");
        EFI_INPUT_KEY k;
        ReadKey(&k);
        Print(L"\r                            \r");
    }
}
#define PAGER() pager_check()

// ── Color state ─────────────────────────────────────────────────

static uint64_t prompt_attr = EFI_ATTR(EFI_TEXT_GREEN, EFI_TEXT_BLACK);
static uint64_t text_attr   = EFI_ATTR(EFI_TEXT_WHITE, EFI_TEXT_BLACK);

static void set_colors(uint64_t fg, uint64_t bg) {
    prompt_attr = EFI_ATTR(fg, bg);
    text_attr   = EFI_ATTR(fg, bg);
    SetAttribute(text_attr);
}

static void print_prompt() {
    SetAttribute(prompt_attr);
    Print(L"\r\n");
    if (cwd[0]) { Print(cwd); Print(L" "); }
    Print(L"> ");
    SetAttribute(text_attr);
}

// ── Commands ────────────────────────────────────────────────────

static void cmd_help() {
    Print(L"Available commands:\r\n");
    Print(L"  help,h   Show this help\r\n");
    Print(L"  clear,cls Clear screen\r\n");
    Print(L"  echo     Echo text\r\n");
    Print(L"  ver      Version info\r\n");
    Print(L"  reboot   Reboot the system\r\n");
    Print(L"  info     System information\r\n");
    Print(L"  cd       Change directory\r\n");
    Print(L"  pwd      Print working directory\r\n");
    Print(L"  ls       List files\r\n");
    Print(L"  cat      Display file\r\n");
    Print(L"  hexdump  Hex dump a file\r\n");
    Print(L"  mkdir    Create directory\r\n");
    Print(L"  rm       Delete file\r\n");
    Print(L"  time     Show UEFI time\r\n");
    Print(L"  gop      Graphics output info\r\n");
    Print(L"  pci      List PCI devices\r\n");
    Print(L"  devices  List UEFI handles\r\n");
    Print(L"  memmap   Show memory map\r\n");
    Print(L"  color    Set color\r\n");
    Print(L"  pager    Toggle pager\r\n");
    Print(L"  bench    Run CPU benchmark\r\n");
    Print(L"  bench3d  3D graphics benchmark\r\n");
    Print(L"  realbench Real-world benchmark suite\r\n");
    Print(L"  history  Show history\r\n");
}

static void cmd_ver() {
    Print(L"VorteX OS 0.3\r\n");
    Print(L"UEFI x86-64 Interactive Shell\r\n");
}

static void cmd_echo(const wchar_t* t) {
    while (*t == L' ') { ++t; }
    Print(t);
    Print(L"\r\n");
}

static void cmd_info() {
    Print(L"System Information:\r\n");
    Print(L"  Firmware: "); Print((const wchar_t*)gST->FirmwareVendor); Print(L"\r\n");
    uint32_t rev = gST->FirmwareRevision;
    Print(L"  Revision: "); PrintInt((rev>>16)&0xFFFF); Print(L"."); PrintInt(rev&0xFFFF); Print(L"\r\n");
    Print(L"  UEFI spec: "); PrintInt((gST->Hdr.Revision>>16)&0xFFFF);
    Print(L"."); PrintInt(gST->Hdr.Revision&0xFFFF); Print(L"\r\n");
    Print(L"  Boot services: 0x"); PrintHex((uint64_t)(uintptr_t)gBS); Print(L"\r\n");
    Print(L"  Runtime services: 0x"); PrintHex((uint64_t)(uintptr_t)gST->RuntimeServices); Print(L"\r\n");
    Print(L"  Config tables: "); PrintInt(gST->NumberOfTableEntries); Print(L"\r\n");

    EFI_GUID acpi2  = EFI_GUID(0x8868e871,0xe4f1,0x11d3,0xbc,0x22,0x00,0x80,0xc7,0x3c,0x88,0x81);
    EFI_GUID smbios = EFI_GUID(0xeb9d2d31,0x2d88,0x11d3,0x9a,0x16,0x00,0x90,0x27,0x3f,0xc1,0x4d);
    typedef struct { EFI_GUID g; void* t; } CT;
    CT* ct = (CT*)gST->ConfigurationTable;
    for (uint64_t i = 0; i < gST->NumberOfTableEntries; i++) {
        if (GuidCmp(&ct[i].g, &acpi2))  Print(L"  ACPI 2.0: 0x"); else
        if (GuidCmp(&ct[i].g, &smbios)) Print(L"  SMBIOS: 0x"); else continue;
        PrintHex((uint64_t)(uintptr_t)ct[i].t); Print(L"\r\n");
    }
}

static void cmd_cd(const wchar_t* a) {
    while (*a == L' ') ++a;
    if (*a == L'\0') { wcscpy(cwd, L"\\"); return; }
    if (a[0] == L'\\') { wcscpy(cwd, a); cwd_clean(); return; }
    if (wcscmp(a, L"..")) {
        int l = wcslen(cwd);
        if (l <= 1) return;
        while (l > 0 && cwd[l] == L'\\') l--;
        while (l > 0 && cwd[l] != L'\\') l--;
        if (l <= 0) { wcscpy(cwd, L"\\"); }
        else { cwd[l] = L'\0'; }
        cwd_clean(); return;
    }
    wchar_t full[256]; build_full_path(a, full);
    EFI_FILE_PROTOCOL* root = open_root_volume();
    if (!root) { Print(L"No filesystem\r\n"); return; }
    EFI_FILE_PROTOCOL* t = nullptr;
    if (root->Open(root, &t, full, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !t) {
        Print(L"Not found: "); Print(a); Print(L"\r\n");
        root->Close(root); return;
    }
    t->Close(t); root->Close(root);
    wcscpy(cwd, full); cwd_clean();
}

static void cmd_pwd() { Print(cwd); Print(L"\r\n"); }

static void cmd_memmap() {
    uint64_t mmap_size = 0, map_key = 0, desc_size = 0;
    uint32_t desc_ver = 0;
    gBS->GetMemoryMap(&mmap_size, nullptr, &map_key, &desc_size, &desc_ver);
    uint64_t buf_size = mmap_size + 1024;
    void* buf = nullptr;
    EFI_STATUS s = gBS->AllocatePool(4, buf_size, &buf);
    if (s != EFI_SUCCESS || !buf) { Print(L"Alloc failed\r\n"); return; }
    s = gBS->GetMemoryMap(&mmap_size, buf, &map_key, &desc_size, &desc_ver);
    if (s != EFI_SUCCESS) { Print(L"GetMemoryMap failed\r\n"); gBS->FreePool(buf); return; }

    EFI_MEMORY_DESCRIPTOR* md = (EFI_MEMORY_DESCRIPTOR*)buf;
    uint64_t count = mmap_size / desc_size;
    uint64_t total_mem = 0;

    Print(L" Type                Start              Pages      Attr\r\n");
    Print(L" ---- --------------- ------------------ ---------- ----\r\n");
    for (uint64_t i = 0; i < count; i++) {
        EFI_MEMORY_DESCRIPTOR* d = (EFI_MEMORY_DESCRIPTOR*)((uint8_t*)md + i * desc_size);
        const wchar_t* tname;
        switch (d->Type) {
            case 1: tname = L"LoaderCode     "; break;
            case 2: tname = L"LoaderData     "; break;
            case 3: tname = L"BS_Code        "; break;
            case 4: tname = L"BS_Data        "; break;
            case 5: tname = L"RT_Code        "; break;
            case 6: tname = L"RT_Data        "; break;
            case 7: tname = L"Conventional   "; break;
            case 8: tname = L"Unusable       "; break;
            case 9: tname = L"ACPI_Reclaim   "; break;
            case 10: tname = L"ACPI_NVS       "; break;
            case 11: tname = L"MMIO           "; break;
            case 12: tname = L"MMIO_Port      "; break;
            case 13: tname = L"PalCode        "; break;
            case 14: tname = L"Persistent     "; break;
            default: tname = L"Reserved       "; break;
        }
        Print(L" "); Print(tname);
        Print(L" 0x"); PrintHexPad(d->PhysicalStart, 16);
        Print(L" "); PrintHexPad(d->NumberOfPages, 8);
        Print(L" 0x"); PrintHex(d->Attribute);
        Print(L"\r\n"); PAGER();
        if (d->Type == 7) total_mem += d->NumberOfPages * 4096;
    }
    Print(L"\r\n Total conventional: "); PrintInt(total_mem / (1024*1024));
    Print(L" MB ("); PrintInt(total_mem); Print(L" bytes)\r\n");
    gBS->FreePool(buf);
}

// ── File system helpers ─────────────────────────────────────────

static EFI_FILE_PROTOCOL* open_root_volume() {
    EFI_GUID g = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
    void* fs = nullptr;
    if (gBS->LocateProtocol(&g, nullptr, &fs) != EFI_SUCCESS || !fs) return nullptr;
    EFI_FILE_PROTOCOL* r = nullptr;
    if (((EFI_SIMPLE_FILE_SYSTEM_PROTOCOL*)fs)->OpenVolume(fs, &r) != EFI_SUCCESS) return nullptr;
    return r;
}

static void list_directory(EFI_FILE_PROTOCOL* dir, const wchar_t* path) {
    EFI_FILE_PROTOCOL* t = nullptr;
    EFI_STATUS s = dir->Open(dir, &t, path, EFI_FILE_MODE_READ, 0);
    if (s != EFI_SUCCESS || !t) { Print(L"Cannot open: "); Print(path); Print(L"\r\n"); return; }

    EFI_GUID ig = EFI_FILE_INFO_GUID;
    uint8_t ib[512]; uint64_t is = sizeof(ib);
    bool is_dir = false;
    if (t->GetInfo(t, &ig, &is, ib) == EFI_SUCCESS) {
        EFI_FILE_INFO* fi = (EFI_FILE_INFO*)ib;
        if ((fi->Attribute & 0x10) == 0) {
            Print(L"  "); Print(fi->FileName); Print(L" ("); PrintInt(fi->FileSize); Print(L")\r\n");
            t->Close(t); return;
        }
        is_dir = true;
    }
    if (!is_dir) { t->Close(t); return; }

    Print(L"\r\n Directory: "); Print(path); Print(L"\r\n");
    Print(L"  ----------------------------------------\r\n");
    uint8_t eb[2048];
    while (true) {
        uint64_t es = sizeof(eb); s = t->Read(t, &es, eb);
        if (s != EFI_SUCCESS || es == 0) break;
        EFI_FILE_INFO* fi = (EFI_FILE_INFO*)eb;
        if (fi->FileName[0] == L'.' || fi->FileName[0] == L'\0') continue;
        Print(L"  "); Print(fi->Attribute & 0x10 ? L"[DIR]  " : L"       ");
        Print(fi->FileName);
        int pad = 32 - wcslen(fi->FileName); if (pad < 1) pad = 1;
        for (int i = 0; i < pad; i++) Print(L" ");
        PrintInt(fi->FileSize); Print(L"\r\n"); PAGER();
    }
    t->Close(t);
}

static void cmd_ls(const wchar_t* a) {
    while (*a == L' ') ++a;
    wchar_t path[256];
    if (*a == L'\0') wcscpy(path, cwd);
    else build_full_path(a, path);
    EFI_FILE_PROTOCOL* r = open_root_volume();
    if (!r) { Print(L"No filesystem\r\n"); return; }
    list_directory(r, path); r->Close(r);
}

static void cmd_cat(const wchar_t* a) {
    while (*a == L' ') ++a;
    if (*a == L'\0') { Print(L"Usage: cat <path>\r\n"); return; }
    wchar_t path[256]; build_full_path(a, path);
    EFI_FILE_PROTOCOL* r = open_root_volume();
    if (!r) { Print(L"No filesystem\r\n"); return; }
    EFI_FILE_PROTOCOL* f = nullptr;
    if (r->Open(r, &f, path, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !f) {
        Print(L"Not found: "); Print(a); Print(L"\r\n"); r->Close(r); return;
    }
    EFI_GUID ig = EFI_FILE_INFO_GUID;
    uint8_t ib[512]; uint64_t is = sizeof(ib);
    if (f->GetInfo(f, &ig, &is, ib) == EFI_SUCCESS) {
        if (((EFI_FILE_INFO*)ib)->Attribute & 0x10) {
            Print(L"'"); Print(a); Print(L"' is a directory\r\n");
            f->Close(f); r->Close(r); return;
        }
    }
    uint8_t buf[4096];
    while (true) {
        uint64_t rs = sizeof(buf) - 1;
        if (f->Read(f, &rs, buf) != EFI_SUCCESS || rs == 0) break;
        buf[rs] = 0; wchar_t wb[4096]; int wp = 0;
        for (uint64_t i = 0; i < rs && wp < 4094; i++) {
            if (buf[i] == '\n') { wb[wp++] = '\r'; wb[wp++] = '\n'; }
            else if (buf[i] >= ' ' || buf[i] == '\t') wb[wp++] = buf[i];
        }
        wb[wp] = L'\0'; Print(wb);
    }
    Print(L"\r\n"); f->Close(f); r->Close(r);
}

static void cmd_hexdump(const wchar_t* a) {
    while (*a == L' ') ++a;
    if (*a == L'\0') { Print(L"Usage: hexdump <path>\r\n"); return; }
    wchar_t path[256]; build_full_path(a, path);
    EFI_FILE_PROTOCOL* r = open_root_volume();
    if (!r) { Print(L"No filesystem\r\n"); return; }
    EFI_FILE_PROTOCOL* f = nullptr;
    if (r->Open(r, &f, path, EFI_FILE_MODE_READ, 0) != EFI_SUCCESS || !f) {
        Print(L"Not found\r\n"); r->Close(r); return;
    }
    uint8_t buf[16];
    uint64_t offset = 0;
    while (true) {
        uint64_t rs = 16;
        EFI_STATUS s = f->Read(f, &rs, buf);
        if (s != EFI_SUCCESS || rs == 0) break;

        PrintHexPad(offset, 8); Print(L"  ");
        for (uint64_t i = 0; i < 16; i++) {
            if (i < rs) { PrintHexPad(buf[i], 2); Print(L" "); }
            else { Print(L"   "); }
            if (i == 7) Print(L" ");
        }
        Print(L" |");
        for (uint64_t i = 0; i < rs; i++) {
            wchar_t c = buf[i] >= 0x20 && buf[i] < 0x7F ? buf[i] : L'.';
            wchar_t w[2] = {c, L'\0'};
            Print(w);
        }
        Print(L"|\r\n"); PAGER();
        offset += rs;
        if (rs < 16) break;
    }
    f->Close(f); r->Close(r);
}

static void cmd_mkdir(const wchar_t* a) {
    while (*a == L' ') ++a;
    if (*a == L'\0') { Print(L"Usage: mkdir <path>\r\n"); return; }
    wchar_t path[256]; build_full_path(a, path);
    EFI_FILE_PROTOCOL* r = open_root_volume();
    if (!r) { Print(L"No filesystem\r\n"); return; }
    EFI_FILE_PROTOCOL* d = nullptr;
    EFI_STATUS s = r->Open(r, &d, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
    if (s == EFI_SUCCESS && d) { d->Close(d); Print(L"Created\r\n"); }
    else Print(L"Failed\r\n");
    r->Close(r);
}

static void cmd_rm(const wchar_t* a) {
    while (*a == L' ') ++a;
    if (*a == L'\0') { Print(L"Usage: rm <path>\r\n"); return; }
    wchar_t path[256]; build_full_path(a, path);
    EFI_FILE_PROTOCOL* r = open_root_volume();
    if (!r) { Print(L"No filesystem\r\n"); return; }
    EFI_FILE_PROTOCOL* f = nullptr;
    EFI_STATUS s = r->Open(r, &f, path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0);
    if (s == EFI_SUCCESS && f) {
        s = f->Delete(f);
        Print(s == EFI_SUCCESS ? L"Deleted\r\n" : L"Delete failed\r\n");
    } else Print(L"Not found\r\n");
    r->Close(r);
}

static void cmd_time() {
    EFI_TIME t;
    if (gST->RuntimeServices && gST->RuntimeServices->GetTime &&
        gST->RuntimeServices->GetTime(&t, nullptr) == EFI_SUCCESS) {
        Print(L"Date/Time: "); PrintInt(t.Year); Print(L"-");
        if (t.Month < 10) { Print(L"0"); } PrintInt(t.Month); Print(L"-");
        if (t.Day < 10) { Print(L"0"); } PrintInt(t.Day); Print(L" ");
        if (t.Hour < 10) { Print(L"0"); } PrintInt(t.Hour); Print(L":");
        if (t.Minute < 10) { Print(L"0"); } PrintInt(t.Minute); Print(L":");
        if (t.Second < 10) { Print(L"0"); } PrintInt(t.Second); Print(L"\r\n");
    } else Print(L"Time not available\r\n");
}

static void cmd_gop() {
    EFI_GUID g = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID; void* p = nullptr;
    if (gBS->LocateProtocol(&g, nullptr, &p) != EFI_SUCCESS || !p) {
        Print(L"GOP not found\r\n"); return;
    }
    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL*)p;
    if (!gop->Mode) { Print(L"No mode info\r\n"); return; }
    Print(L"GOP: mode "); PrintInt(gop->Mode->Mode);
    Print(L"/"); PrintInt(gop->Mode->MaxMode);
    Print(L", FB 0x"); PrintHex(gop->Mode->FrameBufferBase);
    Print(L" ("); PrintInt(gop->Mode->FrameBufferSize); Print(L")\r\n");
    uint64_t is; void* ip;
    if (gop->QueryMode(gop, gop->Mode->Mode, &is, &ip) == EFI_SUCCESS && ip && is >= sizeof(EFI_GRAPHICS_OUTPUT_MODE_INFORMATION)) {
        auto* mi = (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*)ip;
        PrintInt(mi->HorizontalResolution); Print(L"x"); PrintInt(mi->VerticalResolution);
        Print(L", stride "); PrintInt(mi->PixelsPerScanLine); Print(L"\r\n");
    }
    for (uint32_t m = 0; m < gop->Mode->MaxMode && m < 24; m++) {
        if (gop->QueryMode(gop, m, &is, &ip) == EFI_SUCCESS && ip) {
            auto* mi = (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*)ip;
            Print(L"  "); PrintInt(m); Print(L": ");
            PrintInt(mi->HorizontalResolution); Print(L"x"); PrintInt(mi->VerticalResolution);
            if (m == gop->Mode->Mode) Print(L" *");
            Print(L"\r\n"); PAGER();
        }
    }
}

static void cmd_pci() {
    EFI_GUID g = EFI_PCI_IO_PROTOCOL_GUID;
    uint64_t count = 0;
    EFI_HANDLE* buf = nullptr;
    EFI_STATUS s = gBS->LocateHandleBuffer(BY_PROTOCOL_GUID, &g, nullptr, &count, &buf);
    if (s != EFI_SUCCESS || !buf || count == 0) {
        Print(L"No PCI devices found\r\n"); return;
    }
    Print(L" BDF  | Vendor Device Class    \r\n");
    Print(L"------+-----------------------\r\n");
    for (uint64_t i = 0; i < count; i++) {
        void* pci = nullptr;
        if (gBS->OpenProtocol(buf[i], &g, &pci, nullptr, nullptr, 0) != EFI_SUCCESS || !pci) continue;
        auto* p = (EFI_PCI_IO_PROTOCOL*)pci;
        uint64_t seg = 0, bus = 0, dev = 0, fun = 0;
        p->GetLocation(p, &seg, &bus, &dev, &fun);

        uint32_t id = 0, cc = 0;
        p->Pci.Read(p, EFI_PCI_WIDTH_UINT32, 0, 1, &id);
        p->Pci.Read(p, EFI_PCI_WIDTH_UINT32, 8, 1, &cc);

        uint16_t vendor = id & 0xFFFF;
        uint16_t device = (id >> 16) & 0xFFFF;
        uint8_t  base_cls = (cc >> 24) & 0xFF;
        uint8_t  sub_cls  = (cc >> 16) & 0xFF;
        uint8_t  prog_if  = (cc >> 8) & 0xFF; (void)prog_if;

        Print(L" "); PrintInt(bus);
        Print(L":"); PrintInt(dev);
        Print(L"."); PrintInt(fun);
        Print(L" | ");
        PrintHexPad(vendor, 4); Print(L" "); PrintHexPad(device, 4);
        Print(L" "); PrintHexPad(base_cls, 2); PrintHexPad(sub_cls, 2);
        Print(L"\r\n"); PAGER();
    }
    gBS->FreePool(buf);
}

static void cmd_devices() {
    uint64_t count = 0; EFI_HANDLE* buf = nullptr;
    EFI_STATUS s = gBS->LocateHandleBuffer(0, nullptr, nullptr, &count, &buf);
    if (s != EFI_SUCCESS) { Print(L"No handles\r\n"); return; }
    Print(L"Handles: "); PrintInt(count); Print(L"\r\n");
    Print(L"  Handle           \r\n");
    Print(L"  -----------------\r\n");
    for (uint64_t i = 0; i < count && i < 64; i++) {
        Print(L"  ");
        PrintHex((uint64_t)(uintptr_t)buf[i]);
        Print(L"\r\n"); PAGER();
    }
    if (count > 64) {
        Print(L"  ... and "); PrintInt(count - 64); Print(L" more\r\n");
    }
    gBS->FreePool(buf);
}

static void cmd_color(const wchar_t* a) {
    while (*a == L' ') ++a;
    if (*a == L'\0') { Print(L"Usage: color <fg> [bg] (0-15)\r\n"); return; }
    uint64_t fg = 0; while (*a >= L'0' && *a <= L'9') { fg = fg * 10 + (*a - L'0'); ++a; }
    while (*a == L' ') ++a;
    uint64_t bg = 0; if (*a >= L'0' && *a <= L'9') { while (*a >= L'0' && *a <= L'9') { bg = bg * 10 + (*a - L'0'); ++a; } }
    if (fg > 15) { fg = 7; }
    if (bg > 7) { bg = 0; }
    set_colors(fg, bg);
}

static void cmd_history() {
    for (int i = 0; i < hist_count; i++) {
        PrintInt(i+1); Print(L": "); Print(hist_buf[i]); Print(L"\r\n");
    }
}

// ── Min/max/abs ──────────────────────────────────────────────────

template<typename T> static inline T tmax(T a, T b) { return a > b ? a : b; }
template<typename T> static inline T tmin(T a, T b) { return a < b ? a : b; }
static inline int tabs(int x) { return x < 0 ? -x : x; }

// ── Timer helpers ─────────────────────────────────────────────────

static uint64_t timer_us() {
    if (!gST->RuntimeServices || !gST->RuntimeServices->GetTime) return 0;
    EFI_TIME t;
    if (gST->RuntimeServices->GetTime(&t, nullptr) != EFI_SUCCESS) return 0;
    return t.Hour * 3600000000ULL + t.Minute * 60000000ULL +
           t.Second * 1000000ULL + t.Nanosecond / 1000;
}

// ── Improved CPU benchmark ───────────────────────────────────────

static void cmd_bench() {
    Print(L"VorteX CPU Benchmark\r\n");
    Print(L"===================\r\n");

    // Integer sum
    uint64_t t0 = timer_us();
    volatile uint64_t sum = 0;
    for (int i = 0; i < 5000000; i++) sum += i;
    (void)sum;
    uint64_t t1 = timer_us();
    uint64_t int_us = t1 - t0;
    Print(L"  Integer sum (5M):      "); PrintInt(int_us); Print(L" us (");
    PrintInt(int_us ? 5000000ULL * 1000000ULL / int_us : 0); Print(L" ops/s)\r\n");

    // Integer multiply
    t0 = timer_us();
    volatile uint64_t prod = 1;
    for (int i = 1; i < 500000; i++) prod *= i;
    (void)prod;
    t1 = timer_us();
    uint64_t mul_us = t1 - t0;
    Print(L"  Integer mul (500K):   "); PrintInt(mul_us); Print(L" us (");
    PrintInt(mul_us ? 500000ULL * 1000000ULL / mul_us : 0); Print(L" ops/s)\r\n");

    // Prime sieve
    t0 = timer_us();
    volatile int prime_count = 0;
    for (int n = 2; n < 50000; n++) {
        int is_prime = 1;
        for (int d = 2; d * d <= n; d++) {
            if (n % d == 0) { is_prime = 0; break; }
        }
        if (is_prime) prime_count++;
    }
    (void)prime_count;
    t1 = timer_us();
    uint64_t prime_us = t1 - t0;
    Print(L"  Prime sieve (50K):    "); PrintInt(prime_us); Print(L" us");
    Print(L" (found "); PrintInt(prime_count); Print(L" primes)\r\n");

    Print(L"\r\nDone.\r\n");
}

// ── 3D Benchmark (software rasterizer via GOP) ───────────────────

static void cmd_bench3d() {
    EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
    void* p = nullptr;
    if (gBS->LocateProtocol(&gop_guid, nullptr, &p) != EFI_SUCCESS || !p) {
        Print(L"No GOP available!\r\n");
        return;
    }
    auto* gop = (EFI_GRAPHICS_OUTPUT_PROTOCOL*)p;
    if (!gop->Mode) { Print(L"No mode info\r\n"); return; }

    uint32_t orig_mode = gop->Mode->Mode;

    // Find best mode (highest res)
    uint32_t best_mode = 0;
    uint32_t best_w = 0, best_h = 0;
    uint64_t info_size;
    void* info_buf;
    for (uint32_t m = 0; m < gop->Mode->MaxMode; m++) {
        if (gop->QueryMode(gop, m, &info_size, &info_buf) == EFI_SUCCESS && info_buf) {
            auto* mi = (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*)info_buf;
            if (mi->PixelFormat <= 2) {
                uint32_t area = mi->HorizontalResolution * mi->VerticalResolution;
                if (area > best_w * best_h) {
                    best_w = mi->HorizontalResolution;
                    best_h = mi->VerticalResolution;
                    best_mode = m;
                }
            }
        }
    }

    if (best_w == 0 || best_h == 0) {
        Print(L"No suitable video mode\r\n");
        return;
    }

    if (gop->SetMode(gop, best_mode) != EFI_SUCCESS) {
        Print(L"Failed to set mode "); PrintInt(best_mode); Print(L"\r\n");
        return;
    }

    uint32_t w = gop->Mode->Info ? ((EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*)gop->Mode->Info)->HorizontalResolution : best_w;
    uint32_t h = gop->Mode->Info ? ((EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*)gop->Mode->Info)->VerticalResolution : best_h;
    uint32_t stride = gop->Mode->Info ? ((EFI_GRAPHICS_OUTPUT_MODE_INFORMATION*)gop->Mode->Info)->PixelsPerScanLine : w;
    volatile void* fb = (volatile void*)(uintptr_t)gop->Mode->FrameBufferBase;

    Print(L"3D Benchmark at "); PrintInt(w); Print(L"x"); PrintInt(h); Print(L" (mode ");
    PrintInt(best_mode); Print(L")\r\n");

    // Fixed-point 3D cube vertices
    typedef int32_t fixed;
    #define FIX_SHIFT 12
    #define FIX_ONE (1 << FIX_SHIFT)
    #define FIX_MUL(a,b) (((int64_t)(a) * (b)) >> FIX_SHIFT)
    #define FIX_DIV(a,b) (((int64_t)(a) << FIX_SHIFT) / (b))

    struct Vec3 { fixed x, y, z; };

    Vec3 cube[8] = {
        {-FIX_ONE, -FIX_ONE, -FIX_ONE},
        { FIX_ONE, -FIX_ONE, -FIX_ONE},
        { FIX_ONE,  FIX_ONE, -FIX_ONE},
        {-FIX_ONE,  FIX_ONE, -FIX_ONE},
        {-FIX_ONE, -FIX_ONE,  FIX_ONE},
        { FIX_ONE, -FIX_ONE,  FIX_ONE},
        { FIX_ONE,  FIX_ONE,  FIX_ONE},
        {-FIX_ONE,  FIX_ONE,  FIX_ONE},
    };

    int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},
        {4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };

    // Face colors (BGR)
    uint32_t face_colors[6] = {0x0000FF, 0x00FF00, 0xFF0000, 0x00FFFF, 0xFF00FF, 0xFFFF00};

    int faces[6][4] = {
        {0,1,2,3},
        {4,5,6,7},
        {0,1,5,4},
        {2,3,7,6},
        {0,3,7,4},
        {1,2,6,5}
    };

    uint64_t t0 = timer_us();
    int frames = 0;
    uint64_t deadline = t0 + 5000000ULL; // 5 seconds

    fixed ang = 0;

    while (timer_us() < deadline) {
        // Clear screen
        for (uint32_t y = 0; y < h; y++) {
            uint32_t* row = (uint32_t*)((uint8_t*)fb + y * stride * 4);
            uint32_t* end = row + w;
            uint32_t* r = row;
            while (r < end) *r++ = 0x00000000;
        }

        // Rotate cube
        fixed sin_a = ang;
        fixed cos_a = FIX_ONE - FIX_MUL(ang, ang) / 2 + FIX_MUL(FIX_MUL(ang, ang), FIX_MUL(ang, ang)) / 24;
        fixed sin_b = ang * 3 / 4;
        fixed cos_b = FIX_ONE - FIX_MUL(sin_b, sin_b) / 2;

        Vec3 proj[8];
        fixed scale = FIX_ONE * 3;
        fixed cx = (int32_t)w * FIX_ONE / 2;
        fixed cy = (int32_t)h * FIX_ONE / 2;

        for (int i = 0; i < 8; i++) {
            fixed x = cube[i].x;
            fixed y = cube[i].y;
            fixed z = cube[i].z;

            // Rotate X
            fixed y1 = FIX_MUL(y, cos_a) - FIX_MUL(z, sin_a);
            fixed z1 = FIX_MUL(y, sin_a) + FIX_MUL(z, cos_a);

            // Rotate Y
            fixed x2 = FIX_MUL(x, cos_b) + FIX_MUL(z1, sin_b);
            fixed z2 = FIX_MUL(x, -sin_b) + FIX_MUL(z1, cos_b);

            proj[i].x = cx + FIX_MUL(x2, scale) / (z2 + FIX_ONE * 4);
            proj[i].y = cy - FIX_MUL(y1, scale) / (z2 + FIX_ONE * 4);
            proj[i].z = z2;
        }

        // Draw faces (painter's algorithm)
        int face_order[6] = {0,1,2,3,4,5};
        // Simple sort by depth
        for (int i = 0; i < 6; i++) {
            for (int j = i+1; j < 6; j++) {
                int zi = 0, zj = 0;
                for (int k = 0; k < 4; k++) { zi += proj[faces[face_order[i]][k]].z; zj += proj[faces[face_order[j]][k]].z; }
                if (zi > zj) { int t = face_order[i]; face_order[i] = face_order[j]; face_order[j] = t; }
            }
        }

        // Rasterize
        for (int fi = 0; fi < 6; fi++) {
            int f = face_order[fi];
            // Backface culling
            int i0 = faces[f][0], i1 = faces[f][1], i2 = faces[f][2];
            fixed ax = proj[i1].x - proj[i0].x;
            fixed ay = proj[i1].y - proj[i0].y;
            fixed bx = proj[i2].x - proj[i0].x;
            fixed by = proj[i2].y - proj[i0].y;
            fixed cross = FIX_MUL(ax, by) - FIX_MUL(ay, bx);
            if (cross <= 0) continue;

            uint32_t color = face_colors[f];

            // Find bounding box
            fixed min_x = proj[i0].x, max_x = proj[i0].x;
            fixed min_y = proj[i0].y, max_y = proj[i0].y;
            for (int k = 1; k < 4; k++) {
                int idx = faces[f][k];
                if (proj[idx].x < min_x) min_x = proj[idx].x;
                if (proj[idx].x > max_x) max_x = proj[idx].x;
                if (proj[idx].y < min_y) min_y = proj[idx].y;
                if (proj[idx].y > max_y) max_y = proj[idx].y;
            }

            int sx = tmax(0, (int)(min_x >> FIX_SHIFT));
            int sy = tmax(0, (int)(min_y >> FIX_SHIFT));
            int ex = tmin((int)w - 1, (int)(max_x >> FIX_SHIFT));
            int ey = tmin((int)h - 1, (int)(max_y >> FIX_SHIFT));

            for (int y = sy; y <= ey; y++) {
                uint32_t* row = (uint32_t*)((uint8_t*)fb + y * stride * 4);
                for (int x = sx; x <= ex; x++) {
                    row[x] = color;
                }
            }
        }

        // Draw edges in white
        for (int e = 0; e < 12; e++) {
            int p1 = edges[e][0], p2 = edges[e][1];
            int x1 = proj[p1].x >> FIX_SHIFT;
            int y1 = proj[p1].y >> FIX_SHIFT;
            int x2 = proj[p2].x >> FIX_SHIFT;
            int y2 = proj[p2].y >> FIX_SHIFT;
            int dx = tabs(x2 - x1), dy = tabs(y2 - y1);
            int sx = x1 < x2 ? 1 : -1;
            int sy = y1 < y2 ? 1 : -1;
            int err = dx - dy;
            int x = x1, y = y1;
            while (true) {
                if (x >= 0 && x < (int)w && y >= 0 && y < (int)h) {
                    uint32_t* row = (uint32_t*)((uint8_t*)fb + y * stride * 4);
                    row[x] = 0xFFFFFF;
                }
                if (x == x2 && y == y2) break;
                int e2 = 2 * err;
                if (e2 > -dy) { err -= dy; x += sx; }
                if (e2 < dx) { err += dx; y += sy; }
            }
        }

        ang += FIX_ONE / 60;
        frames++;
    }

    uint64_t t1 = timer_us();
    uint64_t elapsed = t1 - t0;

    // Restore original mode
    SetAttribute(EFI_ATTR(EFI_TEXT_WHITE, EFI_TEXT_BLACK));
    ClearScreen();
    gop->SetMode(gop, orig_mode);

    // Restore text output
    SetAttribute(EFI_ATTR(EFI_TEXT_WHITE, EFI_TEXT_BLACK));
    ClearScreen();

    Print(L"3D Benchmark Results:\r\n");
    Print(L"  Resolution: "); PrintInt(w); Print(L"x"); PrintInt(h); Print(L"\r\n");
    Print(L"  Frames:     "); PrintInt(frames); Print(L"\r\n");
    Print(L"  Time:       "); PrintInt(elapsed / 1000); Print(L" ms\r\n");
    Print(L"  FPS:        "); PrintInt(elapsed ? frames * 1000000ULL / elapsed : 0); Print(L"\r\n");
    Print(L"  MPixels/s:  "); PrintInt(elapsed ? (uint64_t)frames * w * h * 1000ULL / (elapsed / 1000) / 1000000ULL : 0);
    Print(L"\r\n");
}

// ── Real-world benchmark suite ───────────────────────────────────

static void cmd_realbench() {
    Print(L"VorteX Real-World Benchmark Suite\r\n");
    Print(L"==================================\r\n");
    Print(L"This benchmark runs several real-world workloads...\r\n\r\n");

    uint64_t t0, t1;

    // 1. Dhrystone-like integer workload
    Print(L"[1/5] Dhrystone integer...\r\n");
    t0 = timer_us();
    volatile int dhry = 0;
    for (int i = 0; i < 200000; i++) {
        int a = 1, b = 2, c = 3, d = 4;
        a = a + b; b = c - d; c = a * b; d = c / (b + 1);
        a = a ^ b; b = c | d; c = a & b; d = a << 2;
        a = d >> 1; b = ~a; c = a + b + c + d;
        if (c > 1000) { d = 0; } else { d = 1; }
        dhry += d;
    }
    (void)dhry;
    t1 = timer_us();
    uint64_t dhry_us = t1 - t0;
    Print(L"    "); PrintInt(dhry_us); Print(L" us");
    if (dhry_us > 0) {
        Print(L" (score: "); PrintInt(200000ULL * 1000000ULL / dhry_us); Print(L")\r\n");
    } else { Print(L"\r\n"); }

    // 2. Memory copy bandwidth
    Print(L"[2/5] Memory copy (8 MB)...\r\n");
    #define MEM_SIZE (8 * 1024 * 1024)
    void* src = nullptr;
    void* dst = nullptr;
    EFI_STATUS es = gBS->AllocatePool(4, MEM_SIZE, &src);
    if (es == EFI_SUCCESS) es = gBS->AllocatePool(4, MEM_SIZE, &dst);
    if (es == EFI_SUCCESS && src && dst) {
        volatile uint8_t* s = (volatile uint8_t*)src;
        volatile uint8_t* d = (volatile uint8_t*)dst;
        for (int i = 0; i < MEM_SIZE; i++) s[i] = (uint8_t)i;
        t0 = timer_us();
        for (int r = 0; r < 4; r++) {
            for (int i = 0; i < MEM_SIZE; i++) d[i] = s[i];
        }
        t1 = timer_us();
        uint64_t copy_us = t1 - t0;
        uint64_t bytes = MEM_SIZE * 4;
        if (copy_us > 0) {
            Print(L"    "); PrintInt(bytes / 1024); Print(L" KB copied in ");
            PrintInt(copy_us); Print(L" us (");
            PrintInt(bytes * 1000000ULL / copy_us / (1024*1024)); Print(L" MB/s)\r\n");
        }
        gBS->FreePool(src);
        gBS->FreePool(dst);
    } else {
        Print(L"    Memory allocation failed\r\n");
    }

    // 3. String operations
    Print(L"[3/5] String operations...\r\n");
    {
        wchar_t str_buf[512];
        for (int i = 0; i < 511; i++) str_buf[i] = L'A' + (i % 26);
        str_buf[511] = 0;
        t0 = timer_us();
        volatile int str_len = 0;
        for (int r = 0; r < 10000; r++) {
            str_len = wcslen(str_buf);
        }
        (void)str_len;
        t1 = timer_us();
        uint64_t str_us = t1 - t0;
        Print(L"    strlen x10000: "); PrintInt(str_us); Print(L" us");

        wchar_t copy_buf[512];
        t0 = timer_us();
        for (int r = 0; r < 5000; r++) {
            wcscpy(copy_buf, str_buf);
        }
        t1 = timer_us();
        uint64_t cpy_us = t1 - t0;
        Print(L"  copy x5000: "); PrintInt(cpy_us); Print(L" us\r\n");
    }

    // 4. Sorting benchmark
    Print(L"[4/5] Sorting (quicksort 5000 integers)...\r\n");
    {
        #define SORT_N 5000
        int* arr = nullptr;
        es = gBS->AllocatePool(4, SORT_N * sizeof(int), (void**)&arr);
        if (es == EFI_SUCCESS && arr) {
            for (int i = 0; i < SORT_N; i++) arr[i] = (i * 982451653) % 2147483647;
            t0 = timer_us();
            // Quicksort
            int stack[64];
            int sp = 0;
            int l = 0, r = SORT_N - 1;
            stack[sp++] = l; stack[sp++] = r;
            while (sp > 0) {
                r = stack[--sp]; l = stack[--sp];
                int i = l, j = r;
                int pivot = arr[(l + r) / 2];
                while (i <= j) {
                    while (arr[i] < pivot) i++;
                    while (arr[j] > pivot) j--;
                    if (i <= j) {
                        int tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
                        i++; j--;
                    }
                }
                if (l < j) { stack[sp++] = l; stack[sp++] = j; }
                if (i < r) { stack[sp++] = i; stack[sp++] = r; }
            }
            t1 = timer_us();
            uint64_t sort_us = t1 - t0;
            Print(L"    "); PrintInt(sort_us); Print(L" us");

            // Verify
            volatile int sorted = 1;
            for (int i = 1; i < SORT_N; i++) { if (arr[i-1] > arr[i]) { sorted = 0; break; } }
            Print(L" (sorted: "); Print(sorted ? L"yes" : L"no"); Print(L")\r\n");
            gBS->FreePool(arr);
        }
    }

    // 5. SHA-1 like hash workload
    Print(L"[5/5] Hash workload...\r\n");
    {
        #define HASH_SIZE 4096
        uint8_t* hbuf = nullptr;
        es = gBS->AllocatePool(4, HASH_SIZE, (void**)&hbuf);
        if (es == EFI_SUCCESS && hbuf) {
            for (int i = 0; i < HASH_SIZE; i++) hbuf[i] = (uint8_t)(i * 101);
            t0 = timer_us();
            volatile uint32_t hash = 0;
            for (int r = 0; r < 50000; r++) {
                hash = 0x67452301;
                for (int i = 0; i < HASH_SIZE; i++) {
                    hash ^= (uint32_t)hbuf[i] << ((i % 4) * 8);
                    hash = (hash << 3) | (hash >> 29);
                    hash += ~(hash ^ 0x5A827999);
                }
            }
            (void)hash;
            t1 = timer_us();
            uint64_t hash_us = t1 - t0;
            Print(L"    "); PrintInt(hash_us); Print(L" us (");
            uint64_t hscore = hash_us ? 50000ULL * 1000000ULL / hash_us : 0;
            PrintInt(hscore); Print(L" ops/s)\r\n");
            gBS->FreePool(hbuf);
        }
    }

    Print(L"\r\nBenchmark complete.\r\n");
}

static void cmd_pager(const wchar_t* a) {
    while (*a == L' ') ++a;
    if (wcscmp(a, L"on") || wcscmp(a, L"1") || wcscmp(a, L"yes")) { pager_enabled = true; Print(L"Pager on\r\n"); }
    else if (wcscmp(a, L"off") || wcscmp(a, L"0") || wcscmp(a, L"no") || *a == L'\0') { pager_enabled = false; Print(L"Pager off\r\n"); }
    else { Print(L"Usage: pager [on|off]\r\n"); }
}

// ── Command dispatch ────────────────────────────────────────────

static void handle_command(const wchar_t* cmd) {
    while (*cmd == L' ') ++cmd;
    if (*cmd == L'\0') return;
    pager_lines = 0;

    if      (wcscmp(cmd, L"help") || wcscmp(cmd, L"h"))       cmd_help();
    else if (wcscmp(cmd, L"clear") || wcscmp(cmd, L"cls"))   { ClearScreen(); print_prompt(); }
    else if (wcscmp(cmd, L"ver"))                              cmd_ver();
    else if (wcscmp(cmd, L"reboot")) {
        Print(L"Rebooting...\r\n");
        if (gST->RuntimeServices && gST->RuntimeServices->ResetSystem)
            gST->RuntimeServices->ResetSystem(0, EFI_SUCCESS, 0, nullptr);
    }
    else if (wcscmp(cmd, L"info"))                             cmd_info();
    else if (wcscmp(cmd, L"time"))                             cmd_time();
    else if (wcscmp(cmd, L"gop"))                              cmd_gop();
    else if (wcscmp(cmd, L"pwd"))                              cmd_pwd();
    else if (wcscmp(cmd, L"history"))                          cmd_history();
    else if (wcscmp(cmd, L"memmap"))                           cmd_memmap();
    else if (wcscmp(cmd, L"pci"))                              cmd_pci();
    else if (wcscmp(cmd, L"devices"))                          cmd_devices();
    else if (wcscmp(cmd, L"bench"))                            cmd_bench();
    else if (wcscmp(cmd, L"bench3d"))                          cmd_bench3d();
    else if (wcscmp(cmd, L"realbench"))                        cmd_realbench();
    else if (wcslen(cmd) >= 2 && cmd[0]==L'c' && cmd[1]==L'd' && (cmd[2]==L' '||cmd[2]==L'\0'))
                                                               cmd_cd(cmd+2);
    else if (wcslen(cmd) >= 2 && cmd[0]==L'l' && cmd[1]==L's' && (cmd[2]==L' '||cmd[2]==L'\0'))
                                                               cmd_ls(cmd+2);
    else if (wcslen(cmd) >= 3 && cmd[0]==L'c' && cmd[1]==L'a' && cmd[2]==L't' && (cmd[3]==L' '||cmd[3]==L'\0'))
                                                               cmd_cat(cmd+3);
    else if (wcslen(cmd) >= 4 && cmd[0]==L'e' && cmd[1]==L'c' && cmd[2]==L'h' && cmd[3]==L'o' && (cmd[4]==L' '||cmd[4]==L'\0'))
                                                               cmd_echo(cmd+4);
    else if (wcslen(cmd) >= 7 && cmd[0]==L'h' && cmd[1]==L'e' && cmd[2]==L'x' && cmd[3]==L'd' && cmd[4]==L'u' && cmd[5]==L'm' && cmd[6]==L'p' && (cmd[7]==L' '||cmd[7]==L'\0'))
                                                               cmd_hexdump(cmd+7);
    else if (wcslen(cmd) >= 5 && cmd[0]==L'm' && cmd[1]==L'k' && cmd[2]==L'd' && cmd[3]==L'i' && cmd[4]==L'r' && (cmd[5]==L' '||cmd[5]==L'\0'))
                                                               cmd_mkdir(cmd+5);
    else if (wcslen(cmd) >= 2 && cmd[0]==L'r' && cmd[1]==L'm' && (cmd[2]==L' '||cmd[2]==L'\0'))
                                                               cmd_rm(cmd+2);
    else if (wcslen(cmd) >= 5 && cmd[0]==L'c' && cmd[1]==L'o' && cmd[2]==L'l' && cmd[3]==L'o' && cmd[4]==L'r' && (cmd[5]==L' '||cmd[5]==L'\0'))
                                                               cmd_color(cmd+5);
    else if (wcslen(cmd) >= 5 && cmd[0]==L'p' && cmd[1]==L'a' && cmd[2]==L'g' && cmd[3]==L'e' && cmd[4]==L'r' && (cmd[5]==L' '||cmd[5]==L'\0'))
                                                               cmd_pager(cmd+5);
    else { Print(L"Unknown: "); Print(cmd); Print(L"\r\n"); }
}

// ── Main ────────────────────────────────────────────────────────

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE* system_table) {
    InitializeLib(image, system_table);
    ClearScreen(); SetAttribute(text_attr);

    Print(L"VorteX UEFI Shell v0.3\r\n");
    Print(L"==========================\r\n");
    Print(L"Type 'help' for commands\r\n");
    Print(L"Firmware: "); Print((const wchar_t*)gST->FirmwareVendor);
    uint32_t rev = gST->FirmwareRevision;
    Print(L" rev "); PrintInt((rev>>16)&0xFFFF); Print(L"."); PrintInt(rev&0xFFFF); Print(L"\r\n");

    wchar_t line[256]; int pos = 0, len = 0; line[0] = L'\0';
    hist_display = 0;
    bool tab_pressed = false;

    Print(L"\r\n\\ > ");
    while (true) {
        EFI_INPUT_KEY key;
        if (!ReadKey(&key)) continue;

        if (key.UnicodeChar == L'\r' || key.UnicodeChar == L'\n') {
            tab_pressed = false;
            Print(L"\r\n"); line[len] = L'\0';
            hist_add(line); handle_command(line);
            pos = 0; len = 0; line[0] = L'\0';
            hist_display = hist_count;
            print_prompt();

        } else if (key.UnicodeChar == L'\b' || key.ScanCode == 0x17) {
            tab_pressed = false;
            if (pos > 0) {
                pos--; len--;
                for (int i = pos; i < len; i++) line[i] = line[i+1];
                line[len] = L'\0';
                Print(L"\b \b");
                if (pos < len) { Print(line+pos); for (int i=len; i>pos; i--) Print(L"\b"); }
            }

        } else if (key.ScanCode == 0x01) {
            tab_pressed = false; hist_up(line, &pos, &len);
        } else if (key.ScanCode == 0x02) {
            tab_pressed = false; hist_down(line, &pos, &len);
        } else if (key.UnicodeChar == L'\t') {
            do_tab(line, &pos, &len, &tab_pressed);
        } else if (key.UnicodeChar >= 0x20 && key.UnicodeChar <= 0x7E) {
            tab_pressed = false;
            if (len < 254) {
                for (int i = len; i > pos; i--) line[i] = line[i-1];
                line[pos] = key.UnicodeChar; pos++; len++; line[len] = L'\0';
                Print(line+pos-1);
                for (int i = pos; i < len; i++) Print(L"\b");
            }
        }
    }
    return EFI_SUCCESS;
}
