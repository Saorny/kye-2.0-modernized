#ifndef UTIL_H
#define UTIL_H

#include <cstddef>

std::size_t strlen16(const char* str);
char* strncpyPad(char* dest, const char* src, std::size_t maxLen);
const char* strrchr16(const char* str, char ch);
char* strupr16(char* str);
int strlen16_di(const char* str);
void* allocateLocalMemory(uint16_t sizeInBytes);
int32_t parseSignedDecimalString(const char* str);
uint16_t pseudoRandomUpdate();
char* copyString(char* dst, const char* src);

extern uint8_t charFlags[256]; 
uint32_t randomSeed = 0x00000001;
using FlushCallback = int(*)(void* ctx, const uint8_t* data, uint16_t length);

bool flushBuffer(uint8_t* bufferStart, uint8_t*& writePtr, int& bufferRemaining,
                 int& totalWritten, bool& flushFailed,
                 void* ctx, FlushCallback flushFunc);

#endif // UTIL_H