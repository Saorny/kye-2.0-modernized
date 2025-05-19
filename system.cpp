


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

int ioctl(int fd, uint8_t subfunction, uint16_t dxValue, uint16_t cxValue) {
    bool success = false;
    uint16_t result = 0;

    // Simule ici quelques sous-fonctions IOCTL connues
    switch (subfunction) {
        case 0x00: { // GET DEVICE INFO
            // On renvoie ici un flag simulé pour le device (bit 0x80 = is device)
            result = (g_fileFlags[fd] & 0x0080) ? 0x0080 : 0x0000;
            success = true;
            break;
        }
        case 0x01: // AUTRE OPÉRATION IOCTL → à implémenter si besoin
            success = true;
            break;
        default:
            handleDosError(1); // fonction inconnue
            return -1;
    }

    if (!success) {
        handleDosError(1); // par défaut
        return -1;
    }

    // Si c'était une sous-fonction 0x00 (lecture d'information), retourne result
    if (subfunction == 0x00) {
        return result;
    }

    return 0; // pour toutes les autres, pas de valeur à retourner
}