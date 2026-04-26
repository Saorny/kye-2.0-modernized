#include "file.h"

#include <array>
#include <cstring>

// --------------------------------------------------
// Open file tracking
// --------------------------------------------------
std::unordered_map<int, OpenFileEntry> g_openFileHandles;

// --------------------------------------------------
// Legacy DOS fd flags table (0x0FB8 in asm world)
// --------------------------------------------------
uint16_t g_fdFlags[256] = {};

// --------------------------------------------------
// File table (legacy slots)
// --------------------------------------------------
FileLike fileTable[maxFileCount] = {};

// --------------------------------------------------
// Segment-offset mapped table
// --------------------------------------------------
SegmentOffsetEntry mappedTable[64] = {};

// --------------------------------------------------
// File state / legacy globals
// --------------------------------------------------
uint8_t g_pendingChar = 0;
int     g_nextHandle  = 3;   // legacy: 0,1,2 reserved (stdin/out/err)
int     err           = 0;

// --------------------------------------------------
// Compatibility / attributes
// --------------------------------------------------
uint16_t globalCompatFlags  = 0x4000;
uint16_t allowedAttributes  = 0xFFFF;

// --------------------------------------------------
// Custom handlers (optional hooks)
// --------------------------------------------------
int (*g_customReadHandler)(void* dest, uint16_t size) = nullptr;
int (*g_customWriteHandler)(const void* buffer, uint16_t size) = nullptr;
void (*setupFileMode)() = nullptr;

// --------------------------------------------------
// Buffers / validation flags
// --------------------------------------------------
char g_buffer1044[256] = {};
bool hasValidatedE86FileOnce = false;
bool hasValidatedFile_E76    = false;

// --------------------------------------------------
// Validation callback
// --------------------------------------------------
FileCallback validateBuffer = nullptr;

// --------------------------------------------------
// File access state
// --------------------------------------------------
int  fileAccessEnabled = 0x64;
char lastWrittenChar   = 0;

// --------------------------------------------------
// “DS buffers” equivalents (mutable)
// --------------------------------------------------
char g_secondaryTextStr[256]  = {};
char g_hintTextStr[512]       = {};
char g_selectedFilePath[260]  = {};

// --------------------------------------------------
// String resources (IDs -> strings)
// --------------------------------------------------
static constexpr struct {
    uint16_t id;
    const char* s;
} kStringTable[] = {
    {0x48D, "w"},
    {0x48F, "KYE20"},
    {0x491, "Cannot open file"},
    {0x4A4, "Cannot write file"},
};

const char* getStringById(uint16_t id)
{
    for (const auto& e : kStringTable) {
        if (e.id == id) return e.s;
    }
    // Fallback safe (never return nullptr unless you want to mimic crashy legacy)
    return "";
}

// --------------------------------------------------
// Optional: one-time init helper (call from your boot/init code)
// --------------------------------------------------
static void initFileTableOnce()
{
    static bool s_inited = false;
    if (s_inited) return;
    s_inited = true;

    // Make legacy invariant true: file->self == file
    for (int i = 0; i < maxFileCount; ++i) {
        fileTable[i].self = &fileTable[i];
        // optional: mark as “unused”
        fileTable[i].fd = -1;
        fileTable[i].buffer = nullptr;
        fileTable[i].bufferCursor = nullptr;
        fileTable[i].bufferSize = 0;
        fileTable[i].bytesRead = 0;
        fileTable[i].flags = 0;
    }
}

// If you want it to run automatically at startup:
struct FileGlobalsAutoInit {
    FileGlobalsAutoInit() { initFileTableOnce(); }
};

static FileGlobalsAutoInit g_autoInit;
