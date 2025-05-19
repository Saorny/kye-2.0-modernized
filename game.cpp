#include <iostream>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <dos.h>
#include <cstdio>
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
#include "error.h"
#include "dialog.h"
#include "game.h"

void startPollingTimer() {
    // Démarre un timer de 100ms sans fenêtre ni fonction de rappel
    g_timerId = SetTimer(
        /* hWnd        */ nullptr,
        /* nIDEvent    */ 0,
        /* uElapse     */ 100,  // 100ms
        /* lpTimerFunc */ nullptr
    );

    if (g_timerId != 0) {
        g_timerActive = true;
    } else {
        // Gestion d'erreur éventuelle
        MessageBox(nullptr, L"Échec du démarrage du timer", L"Erreur", MB_ICONERROR);
    }
}