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
    cout << "Starting" << endl;
    std::cout << "Available video drivers:" << std::endl;

    for (int i = 0; i < SDL_GetNumVideoDrivers(); ++i)
    {
        std::cout << SDL_GetVideoDriver(i) << std::endl;
    }
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        std::cout << "SDL_Init failed: " << SDL_GetError() << std::endl;
        return 1;
    }
    if (!SDL_CreateWindowAndRenderer("Kye (Modern)", 1280, 720, 0, &g_window, &g_renderer))
    {
        cout << "SDL window error: " << SDL_GetError() << endl;
        SDL_Quit();
        return 255;
    }
    cout << "SDL Window Created!" << endl;
    initTickCounter();
    cout << "runLegacyCallbackQueue" << endl;
    runLegacyCallbackQueue();
    cout << "initializeGameWindow" << endl;
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
        handlePaintOrRenderRequest();
        SDL_Delay(16);
    }

    configureFileMode();
    invokeExternalCallback();
    cleanupAndExit(exitCode);
}