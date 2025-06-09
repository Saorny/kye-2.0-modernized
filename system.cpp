


#include <cstddef>
#include <ctime>
#include <system_error>
#include "system.h"
#include "file.h"
#include "error.h"
#include "game.h"

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

uint16_t encodeDosTime(const DosTime& t) {
    return (t.hour << 11) | (t.minute << 5) | (t.second / 2);
}

void getCurrentDate(uint16_t* out) {
    if (!out) return;
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);

    DosDate d {
        .year  = static_cast<uint16_t>(now->tm_year + 1900),
        .month = static_cast<uint8_t>(now->tm_mon + 1),
        .day   = static_cast<uint8_t>(now->tm_mday)
    };

    *out = encodeDosDate(d);
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