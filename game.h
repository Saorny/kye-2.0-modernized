#ifndef GAME_H
#define GAME_H

// =====================
// Includes minimaux
// =====================
#include <cstdint>
#include <string>
#include <vector>
#include <array>

#include <windows.h>
#include <SDL3/SDL.h>

#include "file.h"


// =====================
// Enums
// =====================
enum class GameInteractionMode : int16_t {
    NormalPlay   = 0,
    PendingBlock = 1,
};

// =====================
// Legacy GDI backend
// =====================
struct LegacyGfxBackend {
    using WindowHandle = void*;
    using DcHandle = void*;
    using BitmapHandle = void*;

    static DcHandle createCompatibleDC(WindowHandle wnd);
    static BitmapHandle createCompatibleBitmap(WindowHandle wnd, i16 w, i16 h);
    static void selectObject(DcHandle dc, void* gdiObject);
    static void bitBlt(
        DcHandle dcDst, i16 xDst, i16 yDst, i16 w, i16 h,
        DcHandle dcSrc, i16 xSrc, i16 ySrc, uint32_t rop
    );
    static void setPixel(DcHandle dc, i16 x, i16 y, uint32_t colorRef);
    static void deleteDC(DcHandle dc);
    static void deleteObject(void* gdiObject);
    static void blitToWindow(
        WindowHandle wnd,
        i16 xDst, i16 yDst, i16 w, i16 h,
        DcHandle dcSrc, i16 xSrc, i16 ySrc, uint32_t rop
    );
};

// =====================
// Forward declarations
// =====================
using CallbackFn = void (*)();
struct CallbackQueueEntry;

// =====================
// Structures
// =====================
struct Entity {
    uint16_t position;
    uint8_t  width;
    uint8_t  height;
};

struct TileMapping {
    uint16_t tileCode;
    uint16_t param;
    uint8_t  symbol;
};

struct HudTextures {
    SDL_Texture* wall;
    SDL_Texture* block;
    SDL_Texture* kye;
};

struct LevelChange {
    uint16_t tileId;
    uint16_t row;
    uint16_t col;
    uint16_t extra;
};

struct FormatFlags {
    bool leftAlign;
    bool forceSign;
    bool padWithZero;
    bool upperCase;
};

struct FormatState {
    int width;
    int precision;
    int argIndex;
    int base;
};

struct Buffer {
    char* start;
    char* writePtr;
};

struct EntityActionStruct {
    int actionCode;
    int row;
    int col;
    int timer;
};

struct CallbackQueueEntry {
    uint8_t state;
    uint8_t priority;
    CallbackFn fn;
};

// =====================
// Constants
// =====================
static constexpr int GRID_ROWS = 20;
static constexpr int GRID_COLS = 30;
static constexpr int MAX_CHANGES = GRID_ROWS * GRID_COLS;
static constexpr int kMaxEntityLines = 256;

static constexpr i16 CELL_FLAG_EMPTY    = static_cast<i16>(0xFFFF);
static constexpr i16 CELL_FLAG_SENTINEL = static_cast<i16>(0xFFFE);

// =====================
// GameState
// =====================
class GameState {
public:
    static constexpr int MAX_NUM_ENTITIES = 256;

    std::vector<EntityActionStruct> entities;

    int entityMap[GRID_ROWS][GRID_COLS];
    int rightEntityMap[GRID_ROWS][GRID_COLS];
    int leftEntityMap[GRID_ROWS][GRID_COLS];
    int topEntityMap[GRID_ROWS][GRID_COLS];
    int bottomEntityMap[GRID_ROWS][GRID_COLS];

    int auxTopRightEntityMap[GRID_ROWS][GRID_COLS];
    int auxBottomRightEntityMap[GRID_ROWS][GRID_COLS];

    int auxMap1282[GRID_ROWS][GRID_COLS];
    int auxMap127A[GRID_ROWS][GRID_COLS];
    int auxMap12CE[GRID_ROWS][GRID_COLS];

    std::vector<int> bottomLeftEntityTable;

    GameState();
};

// =====================
// Entity mappings
// =====================
enum class EntityClass : uint8_t {
    Empty  = 0,
    Fixed  = 1,
    Mobile = 2,
    Player = 3
};

struct EntityMappingEntry {
    EntityClass category;
    uint8_t          unk;
    i16         param;
    char        asciiChar;
};

extern const std::array<EntityMappingEntry, 56> ENTITY_MAPPINGS;

// =====================
// Globals (DECLARATIONS ONLY)
// =====================
extern GameInteractionMode g_interactionMode;
extern GameState g_gameState;

extern uint16_t cursorRow, cursorCol;
extern uint16_t spawnRow, spawnCol;
extern uint16_t selectedTileValue, selectionState, selectedEntityIndex;

extern uint16_t exitCoordLeft, exitCoordRight, exitState;

extern int playerRow, playerCol;
extern int previousRow, previousCol;

extern int g_isMouseCaptured;
extern int someGlobalCondition;
extern int g_pendingEventCount;
extern int frameCounter;

extern uint16_t randomSeedLow, randomSeedHigh;

extern char g_levelHintTextBuffer[512]; // 0x2A4A
extern char g_secondaryText[256]; 
extern char g_levelName[256]; // 0x2A9A
extern char g_statusLineBuffer[512]; // 0x29FA
extern char g_selectedFilePath[0x100];
extern int g_levelIndex; 

extern bool g_openFileDialogAccepted;
extern char g_openFileDialogPath[0x100];
extern char g_defaultOpenSuffix[0x40];
extern uint8_t   fileAccessEnabled;

extern i16  currentEntryIndex;

extern uint8_t   EntityTable[kMaxEntityLines];
extern i16  g_cellClickFlags[GRID_ROWS * GRID_COLS];
extern i16  g_entityIndexGrid[GRID_ROWS][GRID_COLS];

extern LevelChange changeList[MAX_CHANGES];
extern uint16_t gameGrid[GRID_ROWS][GRID_COLS];

extern i16 g_gridMain[GRID_ROWS][GRID_COLS];
extern i16 g_gridAuxA[GRID_ROWS][GRID_COLS];
extern i16 g_gridAuxB[GRID_ROWS][GRID_COLS];

extern CallbackFn g_callbackTable[256];
extern CallbackFn g_validateInternalBufferCallback;
extern CallbackFn g_configureFileMode;
extern CallbackFn g_invokeExternalCallbackFn;

int matchedEntryCount;

// =====================
// Functions
// =====================
LRESULT CALLBACK ToolboxWndProc(HWND, UINT, WPARAM, LPARAM);

void resetLevelStateMemory();
void startPollingTimer();

void runLegacyCallbackQueue(CallbackQueueEntry* begin,
                            CallbackQueueEntry* end);

void processCallbackQueue(uint16_t* start, uint16_t* end);
void processCallbackQueueFromEngineEvent();

void configureFileMode();
void invokeExternalCallback();
void advanceToNextLevelOrBlock();
void drainPendingEvents();
bool canPlaceEntityAtPosition(uint16_t pos, uint16_t level, uint16_t value, uint8_t tie);
int loadLevelByIndex(int level);
void updateNextLevelMenuItem();
void clearStatusLine(const char* str);

// =====================
// Tables externes
// =====================
extern const uint16_t g_table10C8[];
extern const uint16_t g_table10C6[];
extern const uint8_t  entityTable_10BC[];
extern const int8_t g_thresholdTable[];


using EventHandler = void(*)();
using VoidCallback = void (*)();

#endif // GAME_H
