


#include <cstddef>
#include <ctime>
#include <system_error>
#include "system.h"
#include "file.h"
#include "error.h"

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

void getCurrentDate(DosDate* out) {
    if (!out) return;

    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);

    out->year  = now->tm_year + 1900;
    out->month = now->tm_mon + 1;
    out->day   = now->tm_mday;
}

void getCurrentTime(DosTime* out) {
    if (!out) return;

    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);

    out->hour       = now->tm_hour;
    out->minute     = now->tm_min;
    out->second     = now->tm_sec;
    out->hundredths = 0; // approximation, car std::tm ne donne pas mieux
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
