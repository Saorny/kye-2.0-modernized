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

// --------------------------------------------------
// File / dialog
// --------------------------------------------------
bool g_openFileDialogAccepted = false;
char g_openFileDialogPath[0x100] = {};
char g_defaultOpenSuffix[0x40] = {};

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

int g_pendingEventCount = 0;
int frameCounter = 0;

// --------------------------------------------------
// Random
// --------------------------------------------------
uint16_t randomSeedLow  = 0x4E35;
uint16_t randomSeedHigh = 0x015A;

int g_currentNameIndex = -1;
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

// --------------------------------------------------
// Callback / events
// --------------------------------------------------
CallbackFn g_callbackTable[256] = {};

void noopCallback() {}
VoidCallback g_validateInternalBufferCallback = &noopCallback;
VoidCallback g_configureFileMode = nullptr;
VoidCallback g_invokeExternalCallbackFn = &noopCallback;
int g_levelIndex = 1;

std::int16_t srcRow = 0;
std::int16_t srcCol = 0;
std::int16_t cellWidth = 16;
std::int16_t cellHeight = 16;

// --------------------------------------------------
// Game state
// --------------------------------------------------
GameState g_gameState{};
GameState::GameState() {};

// --------------------------------------------------
// Environment
// --------------------------------------------------
char** g_environmentStringPtrs = nullptr;
int matchedEntryCount = 0;

NewLevelDialogResult g_newLevelDialogResult = NewLevelDialogResult::None;

SDL_FRect g_panel{};
SDL_FRect g_okBtn{};
SDL_FRect g_cancelBtn{};
bool g_newLevelDialogOpen = false;

char g_speedPrefix3[4] = {};
char g_speedTag3[4]    = {};

const char* kEnvKey        = "TZ";   // dseg02:10EC "TZ\0"
const char* kDefaultPrefix = "EST";  // dseg02:10EF "EST\0"
const char* kDefaultTag    = "EDT";  // dseg02:10F3 "EDT\0"

bool     speedFallbackUsed = true;
uint16_t speedMultiplierHigh = 0;
uint16_t speedMultiplierLow  = 0x4650; // fallback legacy

int pendingRow = 0;
int pendingCol = 0;
int remainingDiamondCount = 0;

const std::unordered_map<int,const char*> exceptionMessages =
{
    {129,"Invalid"},
    {130,"DeNormal"},
    {131,"Divide by Zero"},
    {132,"Overflow"},
    {133,"Underflow"},
    {134,"Inexact"},
    {135,"Unemulated"},
    {138,"Stack Overflow"},
    {139,"Stack Underflow"},
    {140,"Exception Raised"}
};

const std::unordered_map<int, NotificationEntry> notifications =
{
    {NOTIFY_FATAL, NotificationEntry{"Fatal error",3}},
    {NOTIFY_FILE_ERROR, NotificationEntry{"File error",1}},
    {NOTIFY_MEMORY_ERROR, NotificationEntry{"Memory error",1}},
    {NOTIFY_DATA_ERROR, NotificationEntry{"Data error",1}},
    {NOTIFY_STATE_ERROR, NotificationEntry{"Invalid state",1}}
};

bool g_hasDeviceContext = false;
bool g_nextLevelEnabled = true;
SDL_Window* g_windowHandle2 = nullptr;

SDL_Window* g_toolboxWindow = nullptr;
SDL_Renderer* g_toolboxRenderer = nullptr;
bool toolboxCreated = false;
std::array<CallbackEntry, MAX_CALLBACKS> g_callbackQueue;

struct InitCallbackQueue {
    InitCallbackQueue() {
        for (auto& e : g_callbackQueue) {
            e.state = 0xFF;
            e.priority = 0;
            e.callback = nullptr;
        }
    }
} initCallbackQueue;


SpriteSheet16 g_blockSheet{};

const char* hudMessageText = "";
int uiRightX = 0;

std::string g_levelInput;

SDL_FRect g_mainRect{};

std::vector<Spawner> g_spawners;

const uint16_t g_table10C6[] = {0};
const uint16_t g_table10C8[] = {0};

const DecodeTileEntry g_decodeTileTable[] = {
    {0,0,0}
};

const char* kDefaultStatusLine = "";
const char* kDefaultHintText = "";
const char* kDefaultLevelName = "";

std::string g_levelDisplayText;
std::string g_levelRawText;
std::string g_levelHintText;

uint8_t g_notificationHandlerParam[1] = {0};
uint16_t g_notificationHandlerSlotWord[1] = {0};

int stringBufferCapacity = 512;

int g_entryRowTable[1] = {0};
int g_entryColTable[1] = {0};
int g_entryEnabledTable[1] = {0};
