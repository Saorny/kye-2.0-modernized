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

    if (!g_environmentStringPtrs)
        return nullptr;

    for (char** entry = g_environmentStringPtrs; *entry; ++entry) {
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

static inline std::uint16_t divide64_unsigned(std::uint64_t value)
{
    return static_cast<std::uint16_t>(value / 0x8000ull);
}

static int cStringLen(const char* s)
{
    int n = 0;
    while (s && s[n] != '\0') ++n;
    return n;
}

static inline u8 divmodSigned_u8(i64 numer, i64 denom, i64& outQuot)
{
    // x86 idiv: quotient trunc toward 0, remainder same sign as numer
    const i64 q = (denom != 0) ? (numer / denom) : 0;
    const i64 r = (denom != 0) ? (numer % denom) : 0;
    outQuot = q;
    return static_cast<u8>(r & 0xFF);
}

static inline u8 divmodUnsigned_u8(u64 numer, u64 denom, u64& outQuot)
{
    const u64 q = (denom != 0) ? (numer / denom) : 0;
    const u64 r = (denom != 0) ? (numer % denom) : 0;
    outQuot = q;
    return static_cast<u8>(r & 0xFF);
}

static inline bool geUnsigned64(u64 a, u64 b) { return a >= b; }

static void copySuffixFromFirstDotIfNoWildcards(char* dst, const char* src)
{
    if (!dst || !src) return;

    // 1) trouver le premier '.'
    const char* dot = src;
    while (*dot != '\0' && *dot != '.') {
        ++dot;
    }
    if (*dot == '\0') {
        return; // pas de '.'
    }

    // 2) refuser si wildcard dans le suffixe
    if (std::strchr(dot, '*') != nullptr) return;
    if (std::strchr(dot, '?') != nullptr) return;

    // 3) copier le suffixe (inclut '.' et '\0')
    std::strcpy(dst, dot);
}

static void splitPathDirAndName(char* dirOut, char* nameOut, const char* fullPath)
{
    if (!dirOut || !nameOut) return;
    if (!fullPath) {
        dirOut[0] = '\0';
        nameOut[0] = '\0';
        return;
    }

    const std::size_t len = std::strlen(fullPath);
    const char* begin = fullPath;
    const char* p = fullPath + len; // pointe sur '\0'

    // Scan arrière pour trouver ':' ou '\\'
    // (ASM utilise ANSIPREV pour DBCS; ici version simple 1-byte)
    while (p > begin) {
        --p;
        if (*p == ':' || *p == '\\') break;
    }

    // Si rien trouvé
    if (!(p >= begin && (*p == ':' || *p == '\\'))) {
        std::strcpy(nameOut, fullPath);
        dirOut[0] = '\0';
        return;
    }

    // nameOut = après séparateur
    std::strcpy(nameOut, p + 1);

    // dirOut = fullPath tronqué juste après le séparateur
    const std::size_t cut = static_cast<std::size_t>((p - begin) + 1);

    // Copie entière puis tronque (comme l'ASM)
    std::strcpy(dirOut, fullPath);
    dirOut[cut] = '\0';
}

static void appendDefaultExtensionIfMissing(char* filenameBuf, const char* defaultExt)
{
    if (!filenameBuf || !defaultExt) return;

    // 1) Cherche un '.' dans le nom
    for (char* p = filenameBuf; *p != '\0'; ++p) {
        if (*p == '.') {
            return; // extension déjà présente -> no-op
        }
    }

    // 2) Pas de '.' => concatène defaultExt à la fin
    std::strcat(filenameBuf, defaultExt);
}

static bool containsWildcard(std::string_view s) {
    return s.find('*') != std::string_view::npos || s.find('?') != std::string_view::npos;
}

static void ensureDefaultMask(OpenDialogState& st) {
    st.selectedMask = "*.kye";
}

static void appendDefaultExtensionIfMissing(std::string& filename, std::string_view defaultExt) {
    if (filename.find('.') != std::string::npos) return;
    if (!defaultExt.empty() && defaultExt[0] != '.') {
        filename.push_back('.');
    }
    filename.append(defaultExt);
}

static std::string joinPath(std::string_view dir, std::string_view name) {
    if (dir.empty()) return std::string(name);
    std::string out(dir);
    if (out.back() != '\\' && out.back() != '/') out.push_back('\\');
    out.append(name);
    return out;
}
static void copyMemory(uint16_t dstOff, uint16_t srcOff, int sizeBytes) {
    uint8_t* dst = getPointerFromSegmentOffset(/*ds*/0, dstOff);
    uint8_t* src = getPointerFromSegmentOffset(/*ds*/0, srcOff);
    if (!dst || !src || sizeBytes <= 0) return;
    std::memmove(dst, src, (size_t)sizeBytes);
}
