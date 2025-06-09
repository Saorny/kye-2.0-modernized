#include <cstdint>
#include <dos.h>
#include <vector>
#include <system_error>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include "util.h"

int findIndexInTable(uint16_t value) {
    constexpr size_t kMaxEntries = 6;
    extern uint8_t table112E[];  // supposée déclarée ailleurs (à 0x112E)

    for (size_t i = 0; i < kMaxEntries; ++i) {
        if (static_cast<uint16_t>(table112E[i]) == value) {
            return static_cast<int>(i);
        }
    }

    return -1;
}

bool isDiggerKeyword(const char* str) {
    return str[0] == 'd' &&
           str[1] == 'i' &&
           str[2] == 'g' &&
           str[3] == 'g' &&
           str[4] == 'e' &&
           str[5] == 'r';
}

void loadSpeedSettingFromEnvVar(char* shortCodeBuffer, char* speedDigitsBuffer) {
    const char* envValue = getEnvVar("GAME_SPEED"); // 0x10EC
    if (!envValue || strlen(envValue) < 4) {
        // Valeur absente ou invalide → fallback
        setDefaultSpeedSetting(shortCodeBuffer, speedDigitsBuffer);
        return;
    }

    // Validation simple : les 3 premiers doivent être alphanumériques, suivi d’un +, -, ou chiffre
    if (!isalpha(envValue[0]) || !isalpha(envValue[1]) || !isalpha(envValue[2]) ||
        !(isdigit(envValue[3]) || envValue[3] == '+' || envValue[3] == '-')) {
        setDefaultSpeedSetting(shortCodeBuffer, speedDigitsBuffer);
        return;
    }

    // Copie le shortCode (ex : "PE1") dans le buffer (max 3 lettres)
    strncpy(shortCodeBuffer, envValue, 3);
    shortCodeBuffer[3] = '\0';

    // Récupère le chiffre (ex : "+20", "-5", "42")
    int multiplier = atoi(envValue + 3);

    // Multiplie ce chiffre par une constante (3600 ? vitesse de base)
    int result = multiplier * 3600;
    setSpeedMultiplier(result);  // Met à jour word_93F6, word_93F8, etc.
}

const char* getEnvVar(const char* key) {
    if (!key || *key == '\0')
        return nullptr;

    char keyFirst = *key;
    size_t keyLen = std::strlen(key);

    if (!environmentList)
        return nullptr;

    for (char** entry = environmentList; *entry; ++entry) {
        const char* envEntry = *entry;
        if (!envEntry || *envEntry == '\0')
            continue;

        if (*envEntry != keyFirst)
            continue;

        // Check if entry starts with key and is followed by '='
        if (std::strncmp(envEntry, key, keyLen) == 0 && envEntry[keyLen] == '=') {
            return envEntry + keyLen + 1; // Return pointer to value (after '=')
        }
    }

    return nullptr;
}

void* fillMemory(void* dest, uint8_t value, uint16_t length) {
    memsetOptimized(dest, length, value);
    return dest;
}

void memsetOptimized(void* dest, uint16_t length, uint8_t value) {
    uint8_t* ptr = static_cast<uint8_t*>(dest);

    // Align to word boundary if needed
    if (reinterpret_cast<uintptr_t>(ptr) & 1) {
        if (length == 0) return;
        *ptr++ = value;
        --length;
    }

    // Fill with 16-bit words
    uint16_t wordVal = static_cast<uint16_t>(value) | (static_cast<uint16_t>(value) << 8);
    uint16_t* wordPtr = reinterpret_cast<uint16_t*>(ptr);
    size_t wordCount = length / 2;
    for (size_t i = 0; i < wordCount; ++i) {
        wordPtr[i] = wordVal;
    }

    ptr = reinterpret_cast<uint8_t*>(wordPtr + wordCount);

    // Fill remaining byte if length is odd
    if (length & 1) {
        *ptr = value;
    }
}

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

void* allocateLocalMemory(uint16_t sizeInBytes) {
    return LOCALALLOC(0, sizeInBytes);  // 0 = fixed allocation (non-moveable)
}

int32_t parseSignedDecimalString(const char* str) {
    // Sauter les caractères non imprimables/filtrés (via table 0xD6F ?)
    while (*str && (charFlags[(unsigned char)*str] & 0x01)) {
        ++str;
    }

    bool negative = false;
    if (*str == '-') {
        negative = true;
        ++str;
    } else if (*str == '+') {
        ++str;
    }

    uint32_t result = 0;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        ++str;
    }

    if (negative) {
        return -static_cast<int32_t>(result);
    }

    return static_cast<int32_t>(result);
}

uint16_t pseudoRandomUpdate() {
    // Multiplieur fixé par le jeu (0x015A4E35)
    constexpr uint32_t multiplier = 0x015A4E35;
    constexpr uint32_t increment  = 1;

    // Linear Congruential Generator : new_seed = (a * seed + c) mod 2^32
    randomSeed = randomSeed * multiplier + increment;

    // On retourne les 15 bits de poids fort (comme le fait l'assembleur avec cwd + and ax, 7FFFh)
    return static_cast<uint16_t>((randomSeed >> 16) & 0x7FFF);
}

std::pair<uint16_t, uint16_t> multiply32x32to64(uint16_t op1_low, uint16_t op1_high,
                                                uint16_t op2_low, uint16_t op2_high) {
    uint32_t result = 0;

    // Compose 32-bit operands
    uint32_t lhs = (static_cast<uint32_t>(op1_high) << 16) | op1_low;
    uint32_t rhs = (static_cast<uint32_t>(op2_high) << 16) | op2_low;

    // Perform multiplication
    uint64_t product = static_cast<uint64_t>(lhs) * static_cast<uint64_t>(rhs);

    // Return as 16-bit low and high (DX:AX style)
    return {
        static_cast<uint16_t>(product & 0xFFFF),        // AX
        static_cast<uint16_t>((product >> 16) & 0xFFFF) // DX
    };
}

// Copies a null-terminated string from src to dst
char* copyString(char* dst, const char* src) {
    std::size_t length = std::strlen(src) + 1;  // Include null terminator
    std::memcpy(dst, src, length);
    return dst;
}

bool flushBuffer(uint8_t* bufferStart, uint8_t*& writePtr, int& bufferRemaining,
                 int& totalWritten, bool& flushFailed,
                 void* ctx, FlushCallback flushFunc) {
    const uint16_t bytesToFlush = writePtr - bufferStart;

    if (flushFunc && bytesToFlush > 0) {
        int result = flushFunc(ctx, bufferStart, bytesToFlush);
        if (result == 0) {
            flushFailed = true;
        }
    }

    bufferRemaining = 0x50;
    totalWritten += bytesToFlush;
    writePtr = bufferStart;

    return !flushFailed;
}