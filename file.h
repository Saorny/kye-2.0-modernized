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
    uint16_t bytesRead;      // 0x00
    uint16_t flags;          // 0x02
    uint8_t  fd;             // 0x04
    uint8_t  pad;            // 0x05
    uint16_t bufferSize;     // 0x06
    char    *buffer;         // 0x08
    char    *bufferStart;    // 0x0A
    // Total size = 0x10
};

struct OpenFileEntry {
    FILE* handle;
    uint16_t flags;
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

#endif // FILE_H