#ifndef ERROR_H
#define ERROR_H

#include <cstdint>

int handleDosErrorAndReturn(uint16_t err);
void handleDosError(uint16_t errCode);

#endif // ERROR_H