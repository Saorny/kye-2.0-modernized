#include <cstdint>
#include <dos.h>
#include <vector>
#include <system_error>
#include "util.h"

std::size_t strlen16(const char* str) {
    const uint8_t* s = reinterpret_cast<const uint8_t*>(str);
    std::size_t len = 0;

    while (len < 0xFFFF && s[len] != 0)
        ++len;

    return len;
}

char* strncpyPad(char* dest, const char* src, std::size_t maxLen) {
    std::size_t i = 0;

    // Copie jusqu’à '\0' ou maxLen
    while (i < maxLen && src[i] != '\0') {
        dest[i] = src[i];
        ++i;
    }

    // Pad avec '\0' le reste
    while (i < maxLen) {
        dest[i++] = '\0';
    }

    return dest;
}

const char* strrchr16(const char* str, char ch) {
    std::size_t len = strlen(str);
    const char* end = str + len;

    while (end != str) {
        --end;
        if (*end == ch)
            return end;
    }

    return nullptr;
}

char* strupr16(char* str) {
    if (!str) return nullptr;
    
    char* p = str;
    while (*p) {
        if (*p >= 'a' && *p <= 'z') {
            *p = *p - 'a' + 'A';
        }
        ++p;
    }
    return str;
}

int strlen16_di(const char* str) {
    int len = 0;
    while (*str++) ++len;
    return len;
}
std::pair<uint16_t, uint16_t> divide64(
    uint16_t num_lo, uint16_t num_hi,
    uint16_t den_lo, uint16_t den_hi,
    uint16_t flags
) {
    using u64 = uint64_t;
    using i64 = int64_t;

    u64 numerator = (static_cast<u64>(num_hi) << 16) | num_lo;
    u64 denominator = (static_cast<u64>(den_hi) << 16) | den_lo;

    bool isSigned       = flags & 0x1;
    bool returnRemainder = flags & 0x2;
    bool resultSigned    = flags & 0x4;

    if (denominator == 0)
        throw std::runtime_error("Divide by zero");

    i64 result = 0;
    if (isSigned) {
        auto snum = static_cast<i64>(static_cast<int32_t>(numerator));
        auto sden = static_cast<i64>(static_cast<int32_t>(denominator));
        result = returnRemainder ? (snum % sden) : (snum / sden);
    } else {
        result = returnRemainder ? (numerator % denominator) : (numerator / denominator);
    }

    if (resultSigned) {
        result = static_cast<int32_t>(result);
    }

    return {
        static_cast<uint16_t>(result & 0xFFFF),
        static_cast<uint16_t>((result >> 16) & 0xFFFF)
    };
}