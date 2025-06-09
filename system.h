#ifndef TIME_H
#define TIME_H

#include <cstddef>
#include <cstdint>
#include <unordered_map>

struct DosDate {
    uint16_t year;
    uint8_t month;
    uint8_t day;
};

struct DosTime {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t hundredths;
};

struct DosDateRaw {
    uint16_t year;        // CX
    uint16_t month_day;   // DH:DL → (month << 8) | day
};

struct DosTimeRaw {
    uint16_t hour_min;     // CX
    uint16_t sec_centi;    // DX
};

enum class IoctlResult {
    Success,
    Error
};

extern std::unordered_map<int, bool> g_isDeviceHandle;
bool getAdviceInfo(int fd);

int ioctl(int fd, uint8_t subfunction, uint16_t dxValue, uint16_t cxValue);
void getCurrentDate(uint16_t* out);
void getCurrentTime(uint16_t* out);
void seedGameRNG(uint16_t seed);

#endif // TIME_H