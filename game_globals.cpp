// game_globals.cpp
#include "game.h"

// --------------------------------------------------
// Interaction / mode
// --------------------------------------------------
GameInteractionMode g_interactionMode = GameInteractionMode::NormalPlay;

// --------------------------------------------------
// Globals:: namespace
// --------------------------------------------------
namespace Globals {
    const char* cmdLine = nullptr;
    int showMode = 0;

    uint32_t bootTickCount = 0;
    uint16_t osVersion = 0;

    int timerResolutionTicks = 0;
    bool gameSetMode = false;

    uint8_t legacyStateBlob[0x2EEC - 0x127C] = {};
}

// --------------------------------------------------
// Level / UI strings
// --------------------------------------------------
char g_levelHintTextBuffer[512] = {};
char g_levelName[256] = {};
char g_secondaryText[256] = {};
char g_statusLineTextBuffer[512] = {};
char g_selectedFilePath[0x100] = {};

// --------------------------------------------------
// File / dialog
// --------------------------------------------------
bool g_openFileDialogAccepted = false;
char g_openFileDialogPath[0x100] = {};
char g_defaultOpenSuffix[0x40] = {};
uint8_t fileAccessEnabled = 0;

// --------------------------------------------------
// Cursor / selection / state
// --------------------------------------------------
uint16_t cursorRow = 0;
uint16_t cursorCol = 0;
uint16_t spawnRow = 0;
uint16_t spawnCol = 0;

uint16_t selectedTileValue = 0;
uint16_t selectionState = 0;
uint16_t selectedEntityIndex = 0;

// --------------------------------------------------
// Exit / tile sentinels
// --------------------------------------------------
uint16_t exitCoordLeft  = 0xFFF9;
uint16_t exitCoordRight = 0xFFF9;
uint16_t exitState      = 0xFFFB;

// --------------------------------------------------
// Entity / grid
// --------------------------------------------------
int playerRow = 0;
int playerCol = 0;

int previousRow = 0;
int previousCol = 0;

int g_isMouseCaptured = 0;
int someGlobalCondition = 0;

int g_pendingEventCount = 0;
int frameCounter = 0;

// --------------------------------------------------
// Random
// --------------------------------------------------
uint16_t randomSeedLow  = 0x4E35;
uint16_t randomSeedHigh = 0x015A;

// --------------------------------------------------
// Name table
// --------------------------------------------------
std::vector<std::string> g_nameTable;
int g_currentNameIndex = -1;
std::string g_currentName;

// --------------------------------------------------
// Level changes / grids
// --------------------------------------------------
LevelChange changeList[MAX_CHANGES] = {};
uint16_t gameGrid[GRID_ROWS][GRID_COLS] = {};

int16_t g_gridMain[GRID_ROWS][GRID_COLS] = {};
int16_t g_gridAuxA[GRID_ROWS][GRID_COLS] = {};
int16_t g_gridAuxB[GRID_ROWS][GRID_COLS] = {};

int16_t g_entityIndexGrid[GRID_ROWS][GRID_COLS] = {};

// --------------------------------------------------
// Cell click flags / entity tables
// --------------------------------------------------
int16_t g_cellClickFlags[GRID_ROWS * GRID_COLS] = {};
uint8_t EntityTable[kMaxEntityLines] = {};

// --------------------------------------------------
// Status line
// --------------------------------------------------
char g_statusLineBuffer[512] = {};
int16_t currentEntryIndex = -1;

// --------------------------------------------------
// Callback / events
// --------------------------------------------------
CallbackFn g_callbackTable[256] = {};
EventHandler g_eventHandlers[255] = {};

static void noopCallback() {}
VoidCallback g_validateInternalBufferCallback = &noopCallback;
VoidCallback g_configureFileMode = nullptr;
VoidCallback g_invokeExternalCallbackFn = &noopCallback;
int g_levelIndex = 1;

// --------------------------------------------------
// Graphics legacy
// --------------------------------------------------
LegacyGfxBackend::WindowHandle g_hdc2 = nullptr;
void* bitmap_kye = nullptr;

std::int16_t srcRow = 0;
std::int16_t srcCol = 0;
std::int16_t cellWidth = 0;
std::int16_t cellHeight = 0;

int g_newRow = -1;
int g_newCol = -1;

// --------------------------------------------------
// Game state
// --------------------------------------------------
GameState g_gameState;

// --------------------------------------------------
// Environment
// --------------------------------------------------
char** g_environmentStringPtrs = nullptr;

// --------------------------------------------------
// Misc
// --------------------------------------------------
int levelChangeTable[256] = {};
char* loadedLevelName[256] = {};
char* loadedLevelHint[256] = {};
char* filepath = nullptr;

int matchedEntryCount = 0;

// --------------------------------------------------
// Constants
// --------------------------------------------------
const char* MSG_CANNOT_OPEN_FILE = "Cannot open file: ";
