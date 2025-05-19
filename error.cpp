#include "error.h"

int handleDosErrorAndReturn(uint16_t err) {
    handleDosError(err);
    return err;
}

void handleDosError(uint16_t errCode) {
    int16_t si = static_cast<int16_t>(errCode);  // Sign-extend 16-bit

    if (si < 0) {
        si = -si;
        if (si > 0x23) {        // max negative index
            si = 0x57;
        }
        g_lastMappedDosError = 0xFFFF;
    } else {
        if (si > 0x58) {        // max positive index
            si = 0x57;
        }
        // Remap via table
        uint8_t mapped = g_dosErrorRemapTable[si];
        si = static_cast<int16_t>(mapped);  // signed conversion (via CBW in asm)
        g_lastMappedDosError = si;
    }
}