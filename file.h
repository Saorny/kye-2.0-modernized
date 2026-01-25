#ifndef FILE_H
#define FILE_H

#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>

// Flags (legacy semantics)
static constexpr uint16_t kFlagOwnsHeapBuffer            = 0x0004; // bit 2
static constexpr uint16_t kFlagTextModeLine              = 0x0008; // bit 3
static constexpr uint16_t kFlagsClearMaskFFF3            = 0xFFF3; // clear bits 0x0004 and 0x0008 (et autres selon mas
static constexpr uint16_t FILE_FLAG_WRITEONLY       = 0x0001;
static constexpr uint16_t FILE_FLAG_READONLY        = 0x0002;
static constexpr uint16_t FILE_FLAG_IO_ERROR        = 0x0010;
static constexpr uint16_t FILE_FLAG_EOF             = 0x0020;
static constexpr uint16_t FILE_FLAG_MODE            = 0x0040;
static constexpr uint16_t FILE_FLAG_BUFFERED_ACTIVE = 0x0080;
static constexpr uint16_t FILE_FLAG_NO_REFILL       = 0x0110;
static constexpr uint16_t FILE_FLAG_REPOSITION      = 0x0200;
static constexpr uint16_t FILE_FLAG_ACTIVEBUF       = 0x2000;
static constexpr uint16_t FILE_FLAG_CLEAR_MASK      = 0xFE7F;

// masks used by asm
static constexpr uint16_t FILE_MASK_CLEAR_FFDF     = 0xFFDF;
static constexpr uint16_t FILE_MASK_CLEAR_FE7F     = 0xFE7F;
static constexpr uint16_t FILE_MASK_NO_REFILL      = 0x0110;

static constexpr int maxFileCount = 20;
static constexpr int kEOF = -1;

extern uint16_t g_fdFlags[256];
inline uint16_t& fdFlagsRef(int fd) { return g_fdFlags[static_cast<uint8_t>(fd)]; }

struct FileLike {
    int16_t   bytesRead;     // 0x00
    uint16_t  flags;         // 0x02
    uint8_t   fd;            // 0x04
    uint8_t   pad;           // 0x05
    uint16_t  bufferSize;    // 0x06
    char*     buffer;        // 0x08
    char*     bufferCursor;  // 0x0A
    uint16_t  _pad0C;        // 0x0C
    FileLike* self;          // 0x0E
};

struct OpenFileEntry {
    FILE*    handle;
    uint16_t flags;
};

struct SegmentOffsetEntry {
    uint16_t segment;
    uint16_t offset;
    uint8_t  value;
};

enum FileOpenMode {
    Invalid = 0,
    Read = 1,
    Write = 2,
    ReadWrite = 3,
    Append = 4,
    BinaryModeFlag = 0x100
};

// Globals
extern std::unordered_map<int, OpenFileEntry> g_openFileHandles;

extern FileLike fileTable[maxFileCount];
extern SegmentOffsetEntry mappedTable[64];

extern uint8_t  g_pendingChar;
extern int      g_nextHandle;
extern int      err;

extern uint16_t globalCompatFlags;
extern uint16_t allowedAttributes;

extern int (*g_customReadHandler)(void* dest, uint16_t size);
extern int (*g_customWriteHandler)(const void* buffer, uint16_t size);
extern void (*setupFileMode)();

extern char g_buffer1044[256];
extern bool hasValidatedE86FileOnce;
extern bool hasValidatedFile_E76;

using FileCallback = void(*)();
extern FileCallback validateBuffer;

extern int  fileAccessEnabled;
extern int  totalBlockCount;
extern char lastWrittenChar;

// String resources (ids -> string)
const char* getStringById(uint16_t id);

// “DS buffers” equivalents
extern char g_secondaryTextStr[256];
extern char g_hintTextStr[512];
extern char g_selectedFilePath[260]; //DS:01A0

// Functions
int cleanFile(FileLike* file);
int readStructuredBlock(FileLike* file, char* outBuf);
int readLineToBuffer(FileLike* file, char* outBuf, int maxLen);

int resetAndSeekFile(FileLike* file, int mode);               // legacy
int seekFile(FileLike* file, int32_t offset, int origin);     // modern core

void generateFileFromMappedData();
int findMatchingLineInFile(const std::string& target);

FileLike* openAndPrepareFileFromSlot(const char* filepath, const char* modeStr);
FileLike* openAndPrepareFileFromSlot(uint16_t filepathId, uint16_t modeId);

#endif
