#ifndef GAME_H
#define GAME_H

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

static UINT_PTR g_timerId = 0;
static bool g_timerActive = false;
char g_levelFilePath[MAX_PATH];
int allowedAttributes = 0x0FFFF;
int globalCompatFlags = 0x4000; 

#endif // GAME_H