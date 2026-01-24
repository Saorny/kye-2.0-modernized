#ifndef FILE_H
#define FILE_H

#include <iostream>
#include <cstdint>
#include <fstream>
#include <vector>
#include <unordered_map>

#define FILE_FLAG_WRITEONLY  0x0001
#define FILE_FLAG_READONLY   0x0002
#define FILE_FLAG_IO_ERROR   0x0010
#define FILE_FLAG_EOF        0x0020
#define FILE_FLAG_REPOSITION 0x0200
#define FILE_FLAG_ACTIVEBUF  0x2000
#define fdFlags ((uint16_t*)0x0FB8)

struct FileLike {
    int16_t   bytesRead;     // 0x00
    uint16_t  flags;         // 0x02
    uint8_t   fd;            // 0x04
    uint8_t   pad;           // 0x05
    uint16_t  bufferSize;    // 0x06
    char*     buffer;        // 0x08
    char*     bufferCursor;  // 0x0A
    uint16_t  _pad0C;        // 0x0C (inconnu / align)
    FileLike* self;          // 0x0E  <-- REQUIRED by asm
};
struct OpenFileEntry {
    FILE* handle;
    uint16_t flags;
};

enum FileOpenMode {
    Invalid = 0,
    Read = 1,
    Write = 2,
    ReadWrite = 3,
    Append = 4,
    BinaryModeFlag  = 0x100
};

std::unordered_map<int, OpenFileEntry> g_openFileHandles;

extern uint8_t g_pendingChar;
std::unordered_map<int, uint16_t> g_fileFlags;
constexpr int maxFileCount = 20;
FileLike fileTable[maxFileCount];
int g_nextHandle = 3;
extern int err;

extern uint16_t globalCompatFlags;
extern std::unordered_map<int, uint16_t> g_fileFlags;
extern uint16_t allowedAttributes;

extern int (*g_customReadHandler)(void* dest, uint16_t size);
extern int (*g_customWriteHandler)(const void* buffer, uint16_t size);
extern void (*setupFileMode)();
char g_buffer1044[256];
bool hasValidatedE86FileOnce = false;
bool hasValidatedFile_E76 = false;
using FileCallback = void(*)();

FileCallback validateBuffer;
int fileAccessEnabled = 100;
int totalBlockCount = 1;
char lastWrittenChar = 0;

int cleanFile(FileLike* file);
int readStructuredBlock(FileLike* file, char* outBuf);
int readLineToBuffer(FileLike *file, char *outBuf, int maxLen);
int resetAndSeekFile(FileLike *file, int mode);
void generateFileFromMappedData();
int findMatchingLineInFile(const std::string& target);
FileLike* openAndPrepareFileFromSlot(const char* filepath, const char* mode);

struct SegmentOffsetEntry {
    uint16_t segment;
    uint16_t offset;
    uint8_t value;
};

static constexpr int kEOF = -1;

static constexpr uint16_t kFlagReadEnabled     = 0x0001;
static constexpr uint16_t kFlagErrorOrEOF      = 0x0010;
static constexpr uint16_t kFlagLineMode        = 0x0040;  // influence CR handling (test 0x40)
static constexpr uint16_t kFlagBufferedActive  = 0x0080;  // set avant fillTextBuffer
static constexpr uint16_t kFlagNoRefillMask    = 0x0110;  // test 0x110 (bloque refill)
static constexpr uint16_t kFlagDirtyAutoFlush  = 0x0200;  // si set => flushAllDirtyTextBuffers()
static constexpr uint16_t kFlagSomeMode20      = 0x0020;  // set sur un cas moveFilePointer==1
static constexpr uint16_t kFlagClearMask_FE7F  = 0xFE7F;  // and, puis or 0x20

SegmentOffsetEntry mappedTable[64];

#endif // FILE_H