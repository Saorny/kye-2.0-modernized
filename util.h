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
extern uint8_t  speedFallbackUsed;

static inline uint8_t divmodSigned_u8(int64_t numer, int64_t denom, int64_t& outQuot);
static inline uint8_t divmodUnsigned_u8(uint64_t numer, uint64_t denom, uint64_t& outQuot);
static inline bool geUnsigned64(uint64_t a, uint64_t b) { return a >= b; }

static void copySuffixFromFirstDotIfNoWildcards(char* dst, const char* src);
static void splitPathDirAndName(char* dirOut, char* nameOut, const char* fullPath);
static void appendDefaultExtensionIfMissing(char* filenameBuf, const char* defaultExt);
static std::string joinPath(std::string_view dir, std::string_view name);
static void ensureDefaultMask(OpenDialogState& st);
static bool containsWildcard(std::string_view s);
int findIndexInTable(uint16_t value);
char* copyMemory(char* dest, const char* src, uint16_t size);
bool isDiggerKeyword(const char* str);

#endif // UTIL_H