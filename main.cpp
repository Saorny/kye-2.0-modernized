#include <iostream>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <string>
#include <system_error>
#include <cstddef>
#include <tuple>
#include <stdexcept>
#include "graph.h"
#include "game.h"
#include "file.h"
#include "error.h"
#include "util.h"
#include "time.h"
#include "system.h"
#include "memory.h"

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        return 255;
    }

    if (SDL_CreateWindowAndRenderer("Kye (Modern)", 1280, 720, 0, &g_window, &g_renderer) != 0)
    {
        SDL_Quit();
        return 255;
    }

    initTickCounter();
    runLegacyCallbackQueue(nullptr, nullptr);

    initializeGameWindow(1, 0, 0, 0, nullptr);

    drainPendingEvents();
    processCallbackQueueFromEngineEvent();
    g_validateInternalBufferCallback();

    bool running = true;
    int exitCode = 0;

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            handleEvent(e);
            if (e.type == SDL_EVENT_QUIT) {
                running = false;
                exitCode = 0;
            }
        }

        processCallbackQueue(nullptr, nullptr);
        SDL_SetRenderDrawColor(g_renderer, 0,0,0,255);
        SDL_RenderClear(g_renderer);
        SDL_RenderPresent(g_renderer);
        SDL_Delay(16);
    }

    configureFileMode();
    invokeExternalCallback();
    cleanupAndExit(exitCode);
}