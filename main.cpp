#include <iostream>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <dos.h>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <string>
#include <system_error>
#include <cstddef>
#include <windows.h>
#include <tuple>
#include <stdexcept>
#include "file.h"
#include "error.h"
#include "util.h"
#include "time.h"


#define fdFlags reinterpret_cast<uint16_t*>(0x0FB8);g_windowHandleuint32_t)(segment) << 4) + (offset)))


std::unordered_map<int, std::ofstream> openFiles;
int nextFd = 0;

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
