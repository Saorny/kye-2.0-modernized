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

std::pair<uint16_t, uint16_t> multiply32x32to64(uint16_t op1_low, uint16_t op1_high,
                                                uint16_t op2_low, uint16_t op2_high);


static inline std::uint16_t divide64_unsigned(std::uint64_t value);
static int cStringLen(const char* s);

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i64 = int64_t;

struct SlotState
{
    u16 base;   // [si+0]
    u8  value;  // [si+2]
    u8  index;  // [si+3]
};

struct OutDigits
{
    u8 a;       // [di+0]
    u8 b;       // [di+1]
    u8 zero;    // [di+2] = 0
    u8 c;       // [di+3]
};

extern u16 speedMultiplierLow;
extern u16 speedMultiplierHigh;
extern u8  speedFallbackUsed;

static inline u8 divmodSigned_u8(i64 numer, i64 denom, i64& outQuot);
static inline u8 divmodUnsigned_u8(u64 numer, u64 denom, u64& outQuot);
static inline bool geUnsigned64(u64 a, u64 b) { return a >= b; }

static void copySuffixFromFirstDotIfNoWildcards(char* dst, const char* src);
static void splitPathDirAndName(char* dirOut, char* nameOut, const char* fullPath);
static void appendDefaultExtensionIfMissing(char* filenameBuf, const char* defaultExt);
static std::string joinPath(std::string_view dir, std::string_view name);
static void ensureDefaultMask(OpenDialogState& st);
static bool containsWildcard(std::string_view s);
int findIndexInTable(uint16_t value);
static void copyMemory(uint16_t dstOff, uint16_t srcOff, int sizeBytes);
bool isDiggerKeyword(const char* str);

#endif // UTIL_H