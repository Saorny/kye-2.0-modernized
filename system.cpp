


#include <cstddef>
#include <ctime>
#include <system_error>
#include <span>
#include <chrono>
#include "system.h"
#include "file.h"
#include "error.h"
#include "game.h"
#include "util.h"

static constexpr const char* SPEED_ENV_KEY = "KYE_SPEED"; // <-- à ajuster selon ta vraie clé
static constexpr uint16_t DEFAULT_SPEED_LOW  = 0x4650;    // ASM
static constexpr uint16_t DEFAULT_SPEED_HIGH = 0x0000;    // ASM

const uint8_t entityTable_10BC[] = {0};
const std::vector<const char*> environmentList{};
std::unordered_map<int, bool> g_isDeviceHandle{};

char dst[4]  = {0};
char dest[4] = {0};

bool setCurrentTimeRaw(const DosTimeRaw* time) {
    if (!time) return false;

    uint8_t hour       = time->hour_min & 0xFF;
    uint8_t minute     = time->hour_min >> 8;
    uint8_t second     = time->sec_centi & 0xFF;
    uint8_t hundredths = time->sec_centi >> 8;

    // Validation
    if (hour > 23 || minute > 59 || second > 59 || hundredths > 99)
        return false; // AL = 0xFF dans le monde DOS

    // En DOS, cela modifierait l'horloge système.
    // Ici, on simule simplement.
    return true; // AL = 0
}

bool setCurrentDateRaw(const DosDateRaw* date) {
    if (!date) return false;

    uint16_t year = date->year;
    uint8_t month = date->month_day >> 8;
    uint8_t day   = date->month_day & 0xFF;

    // Simulation : on vérifie si c’est une date valide
    if (year < 1980 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false; // correspond à AL = 0xFF
    }

    // Dans un vrai DOS, ça modifierait le système. Ici on simule.
    return true; // AL = 0
}

uint16_t encodeDosTime(const DosTime& t) {
    return (t.hour << 11) | (t.minute << 5) | (t.second / 2);
}

void getCurrentTime(uint16_t* out) {
    if (!out) return;
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);

    DosTime d {
        .hour   = static_cast<uint8_t>(now->tm_hour),
        .minute = static_cast<uint8_t>(now->tm_min),
        .second = static_cast<uint8_t>(now->tm_sec)
    };

    *out = encodeDosTime(d);
}

uint16_t encodeDosDate(const DosDate& d) {
    return ((d.year - 1980) << 9) | (d.month << 5) | (d.day);
}

static DateParts getCurrentDate() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#endif

    return DateParts{
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday
    };
}

static TimeParts getCurrentTime() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);

    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &t);
#endif

    const auto ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

    return TimeParts{
        local.tm_hour,
        local.tm_min,
        local.tm_sec,
        static_cast<int>(ms / 10)
    };
}

bool getAdviceInfo(int fd) {
    // Simule : int 21h / IOCTL / GET DEVICE INFO
    // Retourne true si le handle est un "device" (bit 0x80 actif dans DX)

    auto it = g_isDeviceHandle.find(fd);
    if (it == g_isDeviceHandle.end()) {
        return false; // Par défaut : ce n'est pas un device
    }

    return it->second; // true si c'est un device (bit 0x80)
}

int ioctl(int fd, int subfunction, int val1, int val2) {
    uint16_t dx = static_cast<uint16_t>(val1);
    uint16_t cx = static_cast<uint16_t>(val2);
    uint16_t ax = 0;

    bool error = false;

    // Simuler l’appel système DOS
    // Remplace ce bloc par ton propre système ou couche d’émulation DOS si nécessaire
    if (/* simulate error, e.g., fd invalid */ false) {
        ax = 0x0001; // Code erreur fictif
        error = true;
    } else {
        if (subfunction == 0x00) {
            dx = 0x0080; // Ex. device info
        }
        ax = dx; // Si subfunction == 0, on renvoie dx dans ax
    }

    if (error) {
        handleDosError(ax); // Fonction à implémenter ailleurs
    }

    return (subfunction == 0) ? dx : 0;
}

void seedGameRNG(uint16_t seed) {
    randomSeedHigh = 0;
    randomSeedLow = seed;
}

const char* findEnvVarValue(const char* key) {
    if (!key || key[0] == '\0') return nullptr;

    const std::size_t keyLen = std::strlen(key);
    const char first = key[0];

    for (const char* entry : environmentList) {
        if (!entry || entry[0] == '\0') continue;
        if (entry[0] != first) continue;
        if (entry[keyLen] != '=') continue;
        if (std::strncmp(entry, key, keyLen) != 0) continue;
        return entry + keyLen + 1; // value
    }
    return nullptr;
}

static inline void setDefaults() {
    speedFallbackUsed   = 1;
    speedMultiplierHigh = DEFAULT_SPEED_HIGH;
    speedMultiplierLow  = DEFAULT_SPEED_LOW;

    std::strncpy(dst,  "000", 3); dst[3]  = '\0';
    std::strncpy(dest, "000", 3); dest[3] = '\0';
}

static inline bool parseSignedDecimal(std::string_view s, int32_t& out) {
    if (s.empty()) return false;
    int sign = 1;
    size_t i = 0;
    if (s[0] == '+') { sign = 1; i = 1; }
    else if (s[0] == '-') { sign = -1; i = 1; }
    else return false;

    if (i >= s.size() || !isDigit(s[i])) return false;

    int32_t val = 0;
    for (; i < s.size() && isDigit(s[i]); ++i) {
        val = val * 10 + (s[i] - '0');
    }
    out = sign * val;
    return true;
}

static inline void setSpeedMultiplierFromSignedInt(int32_t v) {
    int64_t prod = static_cast<int64_t>(v) * 0x0E10LL;
    uint32_t u   = static_cast<uint32_t>(prod);
    speedMultiplierHigh = static_cast<uint16_t>(u >> 16);
    speedMultiplierLow  = static_cast<uint16_t>(u & 0xFFFF);
}

void loadSpeedSettingFromEnvVar() {
    const char* raw = findEnvVarValue(SPEED_ENV_KEY);
    if (!raw) {
        setDefaults();
        return;
    }

    std::string_view s(raw);
    if (s.size() < 4) {
        setDefaults();
        return;
    }

    if (!isDigit(s[0]) || !isDigit(s[1]) || !isDigit(s[2])) {
        setDefaults();
        return;
    }

    if (s[3] != '+' && s[3] != '-') {
        setDefaults();
        return;
    }

    dst[0] = s[0]; dst[1] = s[1]; dst[2] = s[2]; dst[3] = '\0';

    int32_t parsed = 0;
    if (!parseSignedDecimal(s.substr(3), parsed)) {
        setDefaults();
        return;
    }

    setSpeedMultiplierFromSignedInt(parsed);
    speedFallbackUsed = 0;

    for (size_t i = 3; i < s.size(); ++i) {
        if (!isDigit(s[i])) continue;

        std::string_view tail = s.substr(i);
        if (tail.size() < 3) break;

        if (isDigit(tail[1]) && isDigit(tail[2])) {
            dest[0] = tail[0];
            dest[1] = tail[1];
            dest[2] = tail[2];
            dest[3] = '\0';

            speedFallbackUsed = 1;
            break;
        }
    }
}

static inline uint64_t mul32(uint32_t a, uint32_t b) {
    return static_cast<uint64_t>(a) * static_cast<uint64_t>(b);
}

static uint32_t computeAdjustedTime(const DateParts& currentDate,
                                          const TimeParts& currentTime)
{
    loadSpeedSettingFromEnvVar();

    uint64_t acc = (static_cast<uint64_t>(speedMultiplierHigh) << 16)
                 | static_cast<uint64_t>(speedMultiplierLow);
    acc += 0x12CEA600ULL;

    const uint16_t base = static_cast<uint16_t>(currentDate.year + 0xF844);
    const int16_t shifted = static_cast<int16_t>(static_cast<int16_t>(base) >> 2);
    acc += mul32(static_cast<uint32_t>(static_cast<int32_t>(shifted)), 0x01F80786u);

    const uint16_t low2 = static_cast<uint16_t>(base & 0x0003u);
    acc += mul32(static_cast<uint32_t>(low2), 0x033801E1u);

    if ((base & 0x3u) != 0) {
        acc += 0x00015180ULL;
    }

    uint16_t total = 0;
    {
        int si = static_cast<int>(currentDate.month) - 1;
        while (si > 0) {
            --si;
            total = static_cast<uint16_t>(total + static_cast<uint16_t>(entityTable_10BC[si]));
        }
    }

    total = static_cast<uint16_t>(total + static_cast<uint16_t>(static_cast<int>(currentDate.day) - 1));

    if (currentDate.month > 2 && ((currentDate.year & 0x0003u) == 0)) {
        total = static_cast<uint16_t>(total + 1);
    }

    uint16_t siVal = static_cast<uint16_t>(total * 0x18u + static_cast<uint16_t>(currentTime.hour));

    if (speedFallbackUsed != 0) {
        const uint16_t pos = static_cast<uint16_t>(currentDate.year + 0xF84E);
        if (canPlaceEntityAtPosition(pos, 0, total, static_cast<uint16_t>(currentTime.hour)) != 0) {
            siVal = static_cast<uint16_t>(siVal - 1);
        }
    }
    acc += mul32(static_cast<uint32_t>(siVal), 0x0E10u);
    acc += mul32(static_cast<uint32_t>(currentTime.minute), 60u);
    acc += static_cast<uint32_t>(currentTime.second);
    return static_cast<uint32_t>(acc);
}

uint32_t computeTimestampNow(uint32_t* outTimestamp)
{
    const DateParts currentDate = getCurrentDate();
    const TimeParts currentTime = getCurrentTime();

    const uint32_t ts = computeAdjustedTime(currentDate, currentTime);

    if (outTimestamp) {
        *outTimestamp = ts;
    }
    return ts;
}
