#ifndef ERROR_H
#define ERROR_H

#include <cstdint>

uint16_t g_lastMappedDosError = 0;
uint16_t g_dosErrorRemapTable[64];

int handleDosErrorAndReturn(uint16_t err);
void handleDosError(uint16_t errCode);

#endif // ERROR_H