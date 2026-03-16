#ifndef UTIL_H
#define UTIL_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <random>

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
extern uint32_t randomSeed;
using FlushCallback = int(*)(void* ctx, const uint8_t* data, uint16_t length);

bool flushBuffer(uint8_t* bufferStart, uint8_t*& writePtr, int& bufferRemaining,
                 int& totalWritten, bool& flushFailed,
                 void* ctx, FlushCallback flushFunc);

std::pair<uint16_t, uint16_t> multiply32x32to64(uint16_t op1_low, uint16_t op1_high,
                                                uint16_t op2_low, uint16_t op2_high);

struct SlotState
{
    uint16_t base;   // [si+0]
    uint8_t  value;  // [si+2]
    uint8_t  index;  // [si+3]
};

struct OutDigits
{
    uint8_t a;       // [di+0]
    uint8_t b;       // [di+1]
    uint8_t zero;    // [di+2] = 0
    uint8_t c;       // [di+3]
};

extern uint16_t speedMultiplierLow;
extern uint16_t speedMultiplierHigh;
int findIndexInTable(uint16_t value);
char* copyMemory(char* dest, const char* src, uint16_t size);
bool isDiggerKeyword(const char* str);

struct FarPtr {
    uint16_t seg;
    uint16_t off;
};

void copyFarBytes(FarPtr src, FarPtr dst, uint16_t count);
inline bool isDigit(char c);
uint16_t generateChangeSpeed();
bool isInsideGrid(int row, int col);
std::uint32_t getCellId(int row, int col);
uint16_t divide64_unsigned(uint64_t value);

#endif // UTIL_H