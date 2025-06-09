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
#include "system.h"
#include "memory.h"

#define fdFlags reinterpret_cast<uint16_t*>(0x0FB8);g_windowHandleuint32_t)(segment) << 4) + (offset)))

std::unordered_map<int, std::ofstream> openFiles;
int nextFd = 0;
uint16_t g_appSegment = 0;
uint16_t g_hInstance = 0;
uint16_t g_hPrevInstance = 0;
const char* g_lpCmdLine = nullptr;
uint16_t g_nCmdShow = 0;

uint16_t g_biosClockLow = 0;
uint16_t g_biosClockHigh = 0;

int main(int argc, char** argv) {
    if (!initTask()) {
        initOrHandleEvent(0xFF);
        quitWithExitCode(0xFF);
    }

    // Stocke les arguments WinMain-style
    g_appSegment     = getSegmentRegister(); // ds en assembleur
    g_hInstance      = getBX();
    g_hPrevInstance  = getSI();
    g_lpCmdLine      = getCommandLine();
    g_nCmdShow       = getDX();

    lockSegment(0xFFFF);

    // Reset la mémoire de 0x127C à 0x2EEC
    clearMemory(reinterpret_cast<void*>(0x127C), 0x2EEC - 0x127C);

    waitForEvent(0);

    if (!initApp(g_lpCmdLine)) {
        initOrHandleEvent(0xFF);
        quitWithExitCode(0xFF);
    }

    // Lecture de l'horloge BIOS
    std::tie(g_biosClockHigh, g_biosClockLow) = readBiosClock();

    // Si al == 0, ne rien faire ; sinon set es:70h = 1
    if (isClockUpdated()) {
        writeToSegment(0x40, 0x70, 1);
    }

    uint16_t dosVersion = getDosVersion();
    uint16_t winFlags   = getWinFlags();

    copyMemory(reinterpret_cast<void*>(0x127C), reinterpret_cast<void*>(0x125E), 0x100);

    // Création de la fenêtre de jeu
    initializeGameWindow(
        g_hInstance,
        g_appSegment,
        g_hPrevInstance,
        g_lpCmdLine,
        g_nCmdShow
    );

    // Lance l’init événementielle
    initOrHandleEvent(0);

    // Boucle sur la table de callbacks (à 0x127C) via sub_128
    scanAndCallCallbacks(reinterpret_cast<uint8_t*>(0x127C), reinterpret_cast<uint8_t*>(0x2EEC));

    return 0;
}

