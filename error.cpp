#include "error.h"

int handleDosErrorAndReturn(uint16_t err) {
    handleDosError(err);
    return err;
}

void handleDosError(uint16_t errCode) {}