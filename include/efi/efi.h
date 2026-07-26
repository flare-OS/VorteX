#pragma once

#include <stdint.h>
#include <stdbool.h>

#define EFI_SUCCESS        0
#define EFI_ERROR(s)       ((int64_t)(s) < 0)
#define EFIAPI             __attribute__((ms_abi))

typedef uint64_t EFI_STATUS;
typedef void*   EFI_HANDLE;
typedef void*   EFI_EVENT;

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} EFI_GUID;

typedef struct {
    uint64_t Signature;
    uint32_t Revision;
    uint32_t HeaderSize;
    uint32_t CRC32;
    uint32_t Reserved;
} EFI_TABLE_HEADER;

typedef struct {
    uint16_t Year;
    uint8_t  Month;
    uint8_t  Day;
    uint8_t  Hour;
    uint8_t  Minute;
    uint8_t  Second;
    uint8_t  Pad1;
    uint32_t Nanosecond;
    int16_t  TimeZone;
    uint8_t  Daylight;
    uint8_t  Pad2;
} EFI_TIME;

typedef struct {
    uint16_t ScanCode;
    wchar_t  UnicodeChar;
} EFI_INPUT_KEY;

// ── Simple Text Output Protocol ─────────────────────────────────

typedef struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI* Reset)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, bool);
    EFI_STATUS (EFIAPI* OutputString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, const wchar_t*);
    EFI_STATUS (EFIAPI* TestString)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, const wchar_t*);
    EFI_STATUS (EFIAPI* QueryMode)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, uint64_t, uint64_t*, uint64_t*);
    EFI_STATUS (EFIAPI* SetMode)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, uint64_t);
    EFI_STATUS (EFIAPI* SetAttribute)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, uint64_t);
    EFI_STATUS (EFIAPI* ClearScreen)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*);
    EFI_STATUS (EFIAPI* SetCursorPosition)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, uint64_t, uint64_t);
    EFI_STATUS (EFIAPI* EnableCursor)(struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL*, bool);
    void* Mode;
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

// ── Simple Text Input Protocol ──────────────────────────────────

typedef struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
    EFI_STATUS (EFIAPI* Reset)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL*, bool);
    EFI_STATUS (EFIAPI* ReadKeyStroke)(struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL*, EFI_INPUT_KEY*);
    EFI_EVENT WaitForKey;
} EFI_SIMPLE_TEXT_INPUT_PROTOCOL;

// ── Memory Types ────────────────────────────────────────────────

typedef uint64_t EFI_PHYSICAL_ADDRESS;
typedef uint64_t EFI_VIRTUAL_ADDRESS;

#define EFI_MEMORY_TYPE_RESERVED         0
#define EFI_MEMORY_TYPE_LOADER_CODE      1
#define EFI_MEMORY_TYPE_LOADER_DATA      2
#define EFI_MEMORY_TYPE_BS_CODE          3
#define EFI_MEMORY_TYPE_BS_DATA          4
#define EFI_MEMORY_TYPE_RT_CODE          5
#define EFI_MEMORY_TYPE_RT_DATA          6
#define EFI_MEMORY_TYPE_CONVENTIONAL     7
#define EFI_MEMORY_TYPE_UNUSABLE         8
#define EFI_MEMORY_TYPE_ACPI_RECLAIM     9
#define EFI_MEMORY_TYPE_ACPI_NVS        10
#define EFI_MEMORY_TYPE_MMIO            11
#define EFI_MEMORY_TYPE_MMIO_PORT       12
#define EFI_MEMORY_TYPE_PAL_CODE        13
#define EFI_MEMORY_TYPE_PERSISTENT      14

typedef struct {
    uint32_t                Type;
    EFI_PHYSICAL_ADDRESS    PhysicalStart;
    EFI_VIRTUAL_ADDRESS     VirtualStart;
    uint64_t                NumberOfPages;
    uint64_t                Attribute;
} EFI_MEMORY_DESCRIPTOR;

// ── PCI I/O Protocol ────────────────────────────────────────────

typedef struct EFI_PCI_IO_PROTOCOL {
    void* PollMem;
    void* PollIo;
    void* Mem;
    void* Io;
    struct {
        EFI_STATUS (EFIAPI* Read)(struct EFI_PCI_IO_PROTOCOL*, uint64_t, uint32_t, uint64_t, void*);
        EFI_STATUS (EFIAPI* Write)(struct EFI_PCI_IO_PROTOCOL*, uint64_t, uint32_t, uint64_t, void*);
    } Pci;
    void* CopyMem;
    void* Map;
    void* Unmap;
    void* AllocateBuffer;
    void* FreeBuffer;
    void* Flush;
    EFI_STATUS (EFIAPI* GetLocation)(struct EFI_PCI_IO_PROTOCOL*, uint64_t*, uint64_t*, uint64_t*, uint64_t*);
    void* Attributes;
    void* GetBarAttributes;
    uint64_t RomSize;
    void* RomBase;
} EFI_PCI_IO_PROTOCOL;

#define EFI_PCI_WIDTH_UINT8  0
#define EFI_PCI_WIDTH_UINT16 1
#define EFI_PCI_WIDTH_UINT32 2

// ── File Protocols ──────────────────────────────────────────────

typedef struct EFI_FILE_PROTOCOL EFI_FILE_PROTOCOL;

typedef struct EFI_FILE_PROTOCOL {
    uint64_t Revision;
    EFI_STATUS (EFIAPI* Open)(EFI_FILE_PROTOCOL*, EFI_FILE_PROTOCOL**, const wchar_t*, uint64_t, uint64_t);
    EFI_STATUS (EFIAPI* Close)(EFI_FILE_PROTOCOL*);
    EFI_STATUS (EFIAPI* Delete)(EFI_FILE_PROTOCOL*);
    EFI_STATUS (EFIAPI* Read)(EFI_FILE_PROTOCOL*, uint64_t*, void*);
    EFI_STATUS (EFIAPI* Write)(EFI_FILE_PROTOCOL*, uint64_t*, void*);
    EFI_STATUS (EFIAPI* GetPosition)(EFI_FILE_PROTOCOL*, uint64_t*);
    EFI_STATUS (EFIAPI* SetPosition)(EFI_FILE_PROTOCOL*, uint64_t);
    EFI_STATUS (EFIAPI* GetInfo)(EFI_FILE_PROTOCOL*, EFI_GUID*, uint64_t*, void*);
    EFI_STATUS (EFIAPI* SetInfo)(EFI_FILE_PROTOCOL*, EFI_GUID*, uint64_t, void*);
    EFI_STATUS (EFIAPI* Flush)(EFI_FILE_PROTOCOL*);
} EFI_FILE_PROTOCOL;

typedef struct {
    uint64_t Revision;
    EFI_STATUS (EFIAPI* OpenVolume)(void*, EFI_FILE_PROTOCOL**);
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;

// ── Loaded Image Protocol ───────────────────────────────────────

typedef struct {
    uint32_t Revision;
    EFI_HANDLE ParentHandle;
    void* SystemTable;
    EFI_HANDLE DeviceHandle;
    void* FilePath;
    void* Reserved;
    uint32_t LoadOptionsSize;
    void* LoadOptions;
    void* ImageBase;
    uint64_t ImageSize;
    uint64_t ImageCodeType;
    uint64_t ImageDataType;
} EFI_LOADED_IMAGE_PROTOCOL;

// ── Graphics Output Protocol ────────────────────────────────────

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    void* Info;
    uint64_t SizeOfInfo;
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelFormat;
    uint32_t RedMask;
    uint32_t GreenMask;
    uint32_t BlueMask;
    uint32_t ReservedMask;
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;

typedef struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
    EFI_STATUS (EFIAPI* QueryMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL*, uint32_t, uint64_t*, void**);
    EFI_STATUS (EFIAPI* SetMode)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL*, uint32_t);
    EFI_STATUS (EFIAPI* Blt)(struct EFI_GRAPHICS_OUTPUT_PROTOCOL*, void*, uint32_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

// ── Boot Services ───────────────────────────────────────────────

typedef EFI_STATUS (EFIAPI* EFI_WAIT_FOR_EVENT)(uint64_t, EFI_EVENT*, uint64_t*);
typedef EFI_STATUS (EFIAPI* EFI_ALLOCATE_POOL)(uint64_t, uint64_t, void**);
typedef EFI_STATUS (EFIAPI* EFI_FREE_POOL)(void*);
typedef EFI_STATUS (EFIAPI* EFI_GET_MEMORY_MAP)(uint64_t*, void*, uint64_t*, uint64_t*, uint32_t*);
typedef EFI_STATUS (EFIAPI* EFI_LOCATE_HANDLE_BUFFER)(uint64_t, EFI_GUID*, void*, uint64_t*, EFI_HANDLE**);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    void* RaiseTPL;
    void* RestoreTPL;
    void* AllocatePages;
    void* FreePages;
    EFI_GET_MEMORY_MAP GetMemoryMap;
    EFI_ALLOCATE_POOL AllocatePool;
    EFI_FREE_POOL FreePool;
    void* CreateEvent;
    void* SetTimer;
    EFI_WAIT_FOR_EVENT WaitForEvent;
    void* SignalEvent;
    void* CloseEvent;
    void* CheckEvent;
    void* InstallProtocolInterface;
    void* ReinstallProtocolInterface;
    void* UninstallProtocolInterface;
    EFI_STATUS (EFIAPI* HandleProtocol)(EFI_HANDLE, EFI_GUID*, void**);
    void* Reserved;
    void* RegisterProtocolNotify;
    void* LocateHandle;
    void* LocateDevicePath;
    void* InstallConfigurationTable;
    void* LoadImage;
    void* StartImage;
    void* Exit;
    void* UnloadImage;
    void* ExitBootServices;
    void* GetNextMonotonicCount;
    void* Stall;
    void* SetWatchdogTimer;
    void* ConnectController;
    void* DisconnectController;
    EFI_STATUS (EFIAPI* OpenProtocol)(EFI_HANDLE, EFI_GUID*, void**, EFI_HANDLE, void*, uint32_t);
    void* CloseProtocol;
    void* OpenProtocolInformation;
    void* ProtocolsPerHandle;
    EFI_LOCATE_HANDLE_BUFFER LocateHandleBuffer;
    EFI_STATUS (EFIAPI* LocateProtocol)(EFI_GUID*, void*, void**);
    void* InstallMultipleProtocolInterfaces;
    void* UninstallMultipleProtocolInterfaces;
    void* CalculateCrc32;
    void* CopyMem;
    void* SetMem;
    void* CreateEventEx;
} EFI_BOOT_SERVICES;

// ── Runtime Services ────────────────────────────────────────────

typedef EFI_STATUS (EFIAPI* EFI_GET_TIME)(EFI_TIME*, void*);
typedef void (EFIAPI* EFI_RESET_SYSTEM)(uint64_t, EFI_STATUS, uint64_t, void*);

typedef struct {
    EFI_TABLE_HEADER Hdr;
    EFI_GET_TIME GetTime;
    void* SetTime;
    void* GetWakeupTime;
    void* SetWakeupTime;
    void* SetVirtualAddressMap;
    void* ConvertPointer;
    void* GetVariable;
    void* GetNextVariableName;
    void* SetVariable;
    void* GetNextHighMonotonicCount;
    EFI_RESET_SYSTEM ResetSystem;
} EFI_RUNTIME_SERVICES;

// ── System Table ────────────────────────────────────────────────

typedef struct EFI_SYSTEM_TABLE {
    EFI_TABLE_HEADER Hdr;
    void*   FirmwareVendor;
    uint32_t FirmwareRevision;
    uint32_t _pad0;
    EFI_HANDLE ConsoleInHandle;
    struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn;
    EFI_HANDLE ConsoleOutHandle;
    struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
    EFI_HANDLE StandardErrorHandle;
    void* StdErr;
    EFI_RUNTIME_SERVICES* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;
    uint64_t NumberOfTableEntries;
    void* ConfigurationTable;
} EFI_SYSTEM_TABLE;

// ── GUIDs ───────────────────────────────────────────────────────

#define EFI_GUID(a,b,c,d0,d1,d2,d3,d4,d5,d6,d7) \
    { (uint32_t)(a), (uint16_t)(b), (uint16_t)(c), \
      { (uint8_t)(d0), (uint8_t)(d1), (uint8_t)(d2), (uint8_t)(d3), \
        (uint8_t)(d4), (uint8_t)(d5), (uint8_t)(d6), (uint8_t)(d7) } }

#define EFI_LOADED_IMAGE_PROTOCOL_GUID \
    EFI_GUID(0x5B1B31A1, 0x9562, 0x11d2, 0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B)

#define EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID \
    EFI_GUID(0x0964e5b22, 0x6459, 0x11d2, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)

#define EFI_FILE_INFO_GUID \
    EFI_GUID(0x09576e92, 0x6d3f, 0x11d2, 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b)

#define EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID \
    EFI_GUID(0x9042a9de, 0x23dc, 0x4a38, 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a)

#define EFI_PCI_IO_PROTOCOL_GUID \
    EFI_GUID(0x4CF5B200, 0x68B8, 0x4CA5, 0x9E, 0xEC, 0xB2, 0x3E, 0x3F, 0x50, 0x02, 0x9A)

#define BY_PROTOCOL_GUID        2

// ── EFI_FILE_INFO ───────────────────────────────────────────────

typedef struct {
    uint64_t Size;
    uint64_t FileSize;
    uint64_t PhysicalSize;
    EFI_TIME CreateTime;
    EFI_TIME LastAccessTime;
    EFI_TIME ModificationTime;
    uint64_t Attribute;
    wchar_t  FileName[1];
} EFI_FILE_INFO;

#define EFI_FILE_MODE_READ   0x0000000000000001ULL
#define EFI_FILE_MODE_WRITE  0x0000000000000002ULL
#define EFI_FILE_MODE_CREATE 0x8000000000000000ULL
#define EFI_FILE_READ_ONLY   0x0000000000000001ULL

// ── Externs ─────────────────────────────────────────────────────

extern EFI_SYSTEM_TABLE* gST;
extern EFI_BOOT_SERVICES* gBS;
extern EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* gConOut;
extern EFI_SIMPLE_TEXT_INPUT_PROTOCOL* gConIn;
