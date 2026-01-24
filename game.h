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
#include <array>
#include <vector>
#include <cstring>
#include <SDL3/SDL.h>
#include "file.h"


using i16 = std::int16_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;


enum class GameInteractionMode : int16_t {
    NormalPlay   = 0, // ax==0 -> loc_177
    PendingBlock = 1, // ax==1 -> loc_19C
};

struct LegacyGfxBackend {
  using WindowHandle = void*;
  using DcHandle = void*;
  using BitmapHandle = void*;

  static DcHandle createCompatibleDC(WindowHandle wnd);
  static BitmapHandle createCompatibleBitmap(WindowHandle wnd, i16 w, i16 h);
  static void selectObject(DcHandle dc, void* gdiObject);
  static void bitBlt(
    DcHandle dcDst, i16 xDst, i16 yDst, i16 w, i16 h,
    DcHandle dcSrc, i16 xSrc, i16 ySrc, u32 rop
  );
  static void setPixel(DcHandle dc, i16 x, i16 y, u32 colorRef);
  static void deleteDC(DcHandle dc);
  static void deleteObject(void* gdiObject);

  // ⭐ Nouveau helper pour blitter vers la fenêtre principale
  static void blitToWindow(WindowHandle wnd,
                           i16 xDst, i16 yDst, i16 w, i16 h,
                           DcHandle dcSrc, i16 xSrc, i16 ySrc, u32 rop);
};

extern GameInteractionMode g_interactionMode;

struct Entity {
    uint16_t position;  // +0 : probablement une coordonnée ou un index
    uint8_t width;      // +2 : largeur
    uint8_t height;     // +3 : hauteur
};

struct TileMapping {
    uint16_t tileCode;
    uint16_t param;
    uint8_t  symbol;
};

struct HudTextures {
    SDL_Texture* wall = nullptr;
    SDL_Texture* block = nullptr;
    SDL_Texture* kye = nullptr;
};

extern const uint16_t g_table10C8[]; // word ptr [?? + 0x10C8]
extern const uint16_t g_table10C6[]; // word ptr [?? + 0x10C6]

namespace Globals {
  inline const char* cmdLine = nullptr;
  inline int showMode = 0;

  inline uint32_t bootTickCount = 0;
  inline uint16_t osVersion = 0;

  inline int timerResolutionTicks = 0;
  inline bool gameSetMode = false;

  inline uint8_t legacyStateBlob[0x2EEC - 0x127C]{};
}

struct LevelChange {
    uint16_t tileId;
    uint16_t row;
    uint16_t col;
    uint16_t extra; // toujours à 0 ici
};

struct CallbackEntry {
    uint8_t active;       // 0 = libre, FF = pris
    uint8_t priority;     // priorité ou timestamp ?
    void (*func)(); // 4 octets far pointer
};

struct FormatFlags {
    bool leftAlign = false;    // flag '-'
    bool forceSign = false;    // flag '+'
    bool padWithZero = false;  // flag '0'
    bool upperCase = false;    // %X
};

struct FormatState {
    int width = 0;
    int precision = -1;
    int argIndex = 0;
    int base = 10; // 10 = décimal, 16 = hex, etc.
};

struct Buffer {
    char* start;
    char* writePtr;
};

static constexpr int GRID_ROWS = 20;
static constexpr int GRID_COLS = 30;
constexpr int MAX_CHANGES = GRID_ROWS * GRID_COLS;

extern i16 baseX;
extern i16 baseY;

inline std::vector<std::string> g_nameTable;
extern int g_currentNameIndex;
extern std::string g_currentName;

LRESULT CALLBACK ToolboxWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);


inline UINT_PTR g_timerId = 0;
inline bool g_timerActive = false;
inline char g_levelFilePath[MAX_PATH]{};
inline char g_levelHint[512]{}; // 29FAh

extern char g_levelName[];        // 0x2A9A
extern char g_secondaryText[];    // 0x2A4A (à confirmer)
extern char g_hintText[];         // 0x29FA
extern char g_selectedFilePath[]; // 0x1A0
extern uint8_t fileAccessEnabled;
char g_levelHint[512];
inline int allowedAttributes = 0x0FFFF;
inline int globalCompatFlags = 0x4000;
inline int g_oneWayAnimPhase = 0;
constexpr int kMaxEntityLines = 256;
extern uint8_t EntityTable[kMaxEntityLines];
extern int16_t g_cellClickFlags[GRID_ROWS * GRID_COLS];
char *loadedLevelName[256];
char *loadedLevelHint[256];
char *filepath;

extern bool  g_openFileDialogAccepted;
extern char  g_openFileDialogPath[0x100]; // 58Ch
extern char  g_defaultOpenSuffix[0x40]; //50Ch

constexpr i16 CELL_FLAG_EMPTY    = static_cast<i16>(0xFFFF); // -1
constexpr i16 CELL_FLAG_SENTINEL = static_cast<i16>(0xFFFE); // -2

uint16_t exitCoordLeft = 0xFFF9;
uint16_t exitCoordRight = 0xFFF9;
uint16_t exitState = 0xFFFB;
uint16_t cursorRow         = 0;
uint16_t cursorCol         = 0;
uint16_t spawnCol          = 0;
uint16_t spawnRow          = 0;
uint16_t selectedTileValue = 0;
uint16_t selectionState    = 0;
uint16_t selectedEntityIndex = 0;
extern TileMapping tileMap[];

extern char g_statusLineBuffer[];         // byte_2C30
extern int  g_statusLineCapacity;

extern int16_t currentEntryIndex;

int levelChangeTable[256];
int playerRow = 0;
int playerCol = 0;

uint16_t randomSeedLow = 0x4E35;   // Valeur d'initialisation utilisée dans le jeu
uint16_t randomSeedHigh = 0x015A;  // Pareil
int previousCol = 0;
int previousRow = 0;
int g_isMouseCaptured = 0;
int someGlobalCondition = 0;
extern char** g_environmentStringPtrs; 
int g_pendingEventCount = 0;
int frameCounter = 0;

const char* MSG_CANNOT_OPEN_FILE = "Cannot open file: ";

struct EntityActionStruct {
    int actionCode;
    int row;
    int col;
    int timer;
};


class GameState {
public:
    static constexpr int GRID_ROWS = 20;
    static constexpr int GRID_COLS = 30;
    static constexpr int MAX_NUM_ENTITIES = 256;

    std::vector<EntityActionStruct> entities;  // 0x172E, 0x1730, 0x1732, 0x1734 combinés

    int entityMap[GRID_ROWS][GRID_COLS]        = { 0 }; // 0x1280
    int rightEntityMap[GRID_ROWS][GRID_COLS]   = { 0 }; // 0x127C
    int leftEntityMap[GRID_ROWS][GRID_COLS]    = { 0 }; // 0x122E
    int topEntityMap[GRID_ROWS][GRID_COLS]     = { 0 }; // 0x1256
    int bottomEntityMap[GRID_ROWS][GRID_COLS]  = { 0 }; // 0x12A6

    int auxTopRightEntityMap[GRID_ROWS][GRID_COLS]    = { 0 }; // [base+1258h]
    int auxBottomRightEntityMap[GRID_ROWS][GRID_COLS] = { 0 }; // [base+12A8h]

    int auxMap1282[GRID_ROWS][GRID_COLS] = { 0 }; // 0x1282
    int auxMap127A[GRID_ROWS][GRID_COLS] = { 0 }; // 0x127A
    int auxMap12CE[GRID_ROWS][GRID_COLS] = { 0 }; // 0x12CE
    std::vector<int> bottomLeftEntityTable;        // 0x12CE (guess)

    GameState() {
        entities.resize(MAX_NUM_ENTITIES, {0, 0, 0, 0});
        bottomLeftEntityTable.resize(MAX_NUM_ENTITIES, -1);

        std::memset(entityMap, -1, sizeof(entityMap));
        std::memset(rightEntityMap, -1, sizeof(rightEntityMap));
        std::memset(leftEntityMap, -1, sizeof(leftEntityMap));
        std::memset(topEntityMap, -1, sizeof(topEntityMap));
        std::memset(bottomEntityMap, -1, sizeof(bottomEntityMap));
        std::memset(auxTopRightEntityMap,    0xFF, sizeof(auxTopRightEntityMap));    // -1
        std::memset(auxBottomRightEntityMap, 0xFF, sizeof(auxBottomRightEntityMap)); // -1
        std::memset(auxMap1282,              0xFF, sizeof(auxMap1282));
        std::memset(auxMap127A,              0xFF, sizeof(auxMap127A));
        std::memset(auxMap12CE,              0xFF, sizeof(auxMap12CE));
    }
};

GameState g_gameState;

extern LegacyGfxBackend::WindowHandle g_hdc2;
extern void* bitmap_kye;

extern i16 srcRow;
extern i16 srcCol;
extern i16 cellWidth;
extern i16 cellHeight;

inline int g_newRow = -1;
inline int g_newCol = -1;

void resetLevelStateMemory();
void startPollingTimer();

using WriteCallback = void(*)(char*& writePtr, const char* src, int length);

enum class EntityClass : uint8_t {
    Empty  = 0,
    Fixed  = 1,
    Mobile = 2,
    Player = 3
};

struct LevelEntry {
    i16 mode;      // [6B8h] : 1, 2 ou 3 (type d’action dans executeCurrentEntryAction)
    i16 tileId;    // [6BAh] : valeur à écrire dans gameGrid (0xFFF5..0xFFFD, 0xFFFE, etc.)
    i16 isActive;  // [6BCh] : 0 = inactif, !=0 = actif
   };

struct EntityMappingEntry {
    EntityClass category;  // octet 0 : 0,1,2,3
    uint8_t     unk;       // octet 1 : toujours 0 ici
    int16_t     param;     // octets 2–3 : ex. 0xFFFD, 0x0001, ...
    char        asciiChar; // octet 4 : ' ', 'K', '1', ...
};

struct SpawnerSoA {
    uint16_t* spawnerPhaseBase;        // base = 0x172E
    uint16_t* spawnerBaseRowBase;      // base = 0x1730
    uint16_t* spawnerBaseColBase;      // base = 0x1732
    uint16_t* spawnDelayCounterBase;   // base = 0x1734
};

constexpr std::array<EntityMappingEntry, 56> ENTITY_MAPPINGS = {{
    // idx  class         unk   param     char   // commentaire
    { EntityClass::Empty,  0x00, 0x0000,  ' ' }, // 0  - Empty
    { EntityClass::Player, 0x00, 0x0000,  'K' }, // 1  - Kye

    // Murs / blocs fixes (param = 0xFFFD .. 0xFFF4)
    { EntityClass::Fixed,  0x00, 0xFFFD,  '1' }, // 2  - Wall1
    { EntityClass::Fixed,  0x00, 0xFFFC,  '2' }, // 3  - Wall2
    { EntityClass::Fixed,  0x00, 0xFFFB,  '3' }, // 4  - Wall3
    { EntityClass::Fixed,  0x00, 0xFFFA,  '4' }, // 5  - Wall4
    { EntityClass::Fixed,  0x00, 0xFFF9,  '5' }, // 6  - Wall5 (mur standard)
    { EntityClass::Fixed,  0x00, 0xFFF8,  '6' }, // 7  - Wall6
    { EntityClass::Fixed,  0x00, 0xFFF7,  '7' }, // 8  - Wall7
    { EntityClass::Fixed,  0x00, 0xFFF6,  '8' }, // 9  - Wall8
    { EntityClass::Fixed,  0x00, 0xFFF5,  '9' }, // 10 - Wall9

    // Spéciaux fixes
    { EntityClass::Fixed,  0x00, 0xFFF4,  'e' }, // 11 - Yellow/destructible brick
    { EntityClass::Fixed,  0x00, 0xFFF3,  '*' }, // 12 - Diamond
    { EntityClass::Fixed,  0x00, 0xFFF2,  'f' }, // 13 - OneWayLeftToRight
    { EntityClass::Fixed,  0x00, 0xFFF1,  'g' }, // 14 - OneWayRightToLeft
    { EntityClass::Fixed,  0x00, 0xFFF0,  'h' }, // 15 - OneWayTopToBottom
    { EntityClass::Fixed,  0x00, 0xFFEF,  'i' }, // 16 - OneWayBottomToTop

    // Mobiles “simples”
    { EntityClass::Mobile, 0x00, 0x0000,  'b' }, // 17 - PushableBrick
    { EntityClass::Mobile, 0x00, 0x0001,  'u' }, // 18 - ArrowUp (block)
    { EntityClass::Mobile, 0x00, 0x0002,  'd' }, // 19 - ArrowDown (block)
    { EntityClass::Mobile, 0x00, 0x0003,  'l' }, // 20 - ArrowLeft (block)
    { EntityClass::Mobile, 0x00, 0x0004,  'r' }, // 21 - ArrowRight (block)
    { EntityClass::Mobile, 0x00, 0x0005,  's' }, // 22 - MagnetVertical
    { EntityClass::Mobile, 0x00, 0x0006,  'S' }, // 23 - MagnetHorizontal
    { EntityClass::Mobile, 0x00, 0x0007,  'U' }, // 24 - PusherUp
    { EntityClass::Mobile, 0x00, 0x0008,  'D' }, // 25 - PusherDown
    { EntityClass::Mobile, 0x00, 0x0009,  'L' }, // 26 - PusherLeft
    { EntityClass::Mobile, 0x00, 0x000A,  'R' }, // 27 - PusherRight
    { EntityClass::Mobile, 0x00, 0x000B,  '^' }, // 28 - CurvedArrowUp
    { EntityClass::Mobile, 0x00, 0x000C,  'v' }, // 29 - CurvedArrowDown
    { EntityClass::Mobile, 0x00, 0x000D,  '<' }, // 30 - CurvedArrowLeft
    { EntityClass::Mobile, 0x00, 0x000E,  '>' }, // 31 - CurvedArrowRight

    // Ennemis
    { EntityClass::Mobile, 0x00, 0x000F,  'T' }, // 32 - EnemyPropeller (hélice carrée)
    { EntityClass::Mobile, 0x00, 0x0010,  'E' }, // 33 - EnemyJaw (machoires)
    { EntityClass::Mobile, 0x00, 0x0011,  'C' }, // 34 - EnemyPurplePoop (caca violet)
    { EntityClass::Mobile, 0x00, 0x0012,  '~' }, // 35 - EnemySnake (serpent)
    { EntityClass::Mobile, 0x00, 0x0013,  '[' }, // 36 - EnemyPropeller2 (hélice ronde)

    // Déflecteurs / blocs arrondis
    { EntityClass::Mobile, 0x00, 0x0014,  'a' }, // 37 - DeflectorRight
    { EntityClass::Mobile, 0x00, 0x0015,  'c' }, // 38 - DeflectorLeft
    { EntityClass::Mobile, 0x00, 0x0016,  'B' }, // 39 - RoundedPushableBrick

    // 40–43 : A (non encore identifiés précisément)
    { EntityClass::Mobile, 0x00, 0x0017,  'A' }, // 40 - UnknownEntityA_1
    { EntityClass::Mobile, 0x00, 0x0018,  'A' }, // 41 - UnknownEntityA_2
    { EntityClass::Mobile, 0x00, 0x0019,  'A' }, // 42 - UnknownEntityA_3
    { EntityClass::Mobile, 0x00, 0x001A,  'A' }, // 43 - UnknownEntityA_4

    // Distributeurs (F)
    { EntityClass::Mobile, 0x00, 0x001B,  'F' }, // 44 - ArrowDispenser1
    { EntityClass::Mobile, 0x00, 0x001C,  'F' }, // 45 - ArrowDispenser2
    { EntityClass::Mobile, 0x00, 0x001D,  'F' }, // 46 - ArrowDispenser3
    { EntityClass::Mobile, 0x00, 0x001E,  'F' }, // 47 - ArrowDispenser4

    // Lave
    { EntityClass::Mobile, 0x00, 0x001F,  'H' }, // 48 - Lava
    { EntityClass::Mobile, 0x00, 0x0020,  'H' }, // 49 - Lava2

    // Spéciaux / compteurs etc. (param 0x003B … 0x0032)
    { EntityClass::Mobile, 0x00, 0x003B,  'w' }, // 50 - Countdown?
    { EntityClass::Mobile, 0x00, 0x003A,  'x' }, // 51 - UnknownEntity_x
    { EntityClass::Mobile, 0x00, 0x0039,  'y' }, // 52 - UnknownEntity_y
    { EntityClass::Mobile, 0x00, 0x0038,  'z' }, // 53 - UnknownEntity_z
    { EntityClass::Mobile, 0x00, 0x0037,  '{' }, // 54 - UnknownEntity_LeftBrace
    { EntityClass::Mobile, 0x00, 0x0036,  '|' }, // 55 - UnknownEntity_Pipe
}};

const char* getFormatStringFromType(uint8_t type);

const char* getEntitySymbol(uint8_t type);
const char* getEntityName(uint8_t type);
char        getEntityTypeChar(uint8_t type);
const char* getEntityParams(uint8_t type);

const char* getTableSymbol(uint8_t type);
const char* getTableName(uint8_t type);
char        getTableTypeChar(uint8_t type);
const char* getTableParams(uint8_t type);

int getAuxTopRightMapValue(int row, int col);    // [base+1258h]
int getAuxBottomRightMapValue(int row, int col); // [base+12A8h]

constexpr int CELL_EMPTY    = -1; // 0xFFFF
constexpr int CELL_SENTINEL = -2; // 0xFFFE

struct Delta { int dRow; int dCol; };
extern const int8_t g_thresholdTable[];

int initOrHandleEvent(int arg);
void processCallbackQueue(uint16_t* start, uint16_t* end);
void runLegacyCallbackQueue(CallbackQueueEntry* begin, CallbackQueueEntry* end);
static void drainPendingEvents();

using EventHandler = void(*)();

struct CallbackQueueEntry {
    uint8_t state;
    uint8_t priority;
    CallbackFn fn;
};

using CallbackFn = void (*)();

extern CallbackFn g_callbackTable[];

EventHandler g_eventHandlers[255] = {};

using VoidCallback = void (*)();

static void noopCallback() {}

VoidCallback g_validateInternalBufferCallback = &noopCallback;

using VoidCallback = void(*)();
VoidCallback g_configureFileMode = nullptr;

VoidCallback g_invokeExternalCallbackFn = &noopCallback;

static constexpr int16_t kTileSelectionSentinel = static_cast<int16_t>(0xFFF3);
static constexpr int16_t kTileSelectedSentinel  = static_cast<int16_t>(0xFFFE);
static constexpr int16_t kTileInactiveSentinel  = static_cast<int16_t>(0xFFF9);

static constexpr int16_t kIndexMin = static_cast<int16_t>(0xFFF5);
static constexpr int16_t kIndexMax = static_cast<int16_t>(0xFFFD);

extern int16_t selectionState;
extern int16_t selectedTileValue;
extern int16_t srcRow;
extern int16_t srcCol;
extern int16_t spawnRow;
extern int16_t spawnCol;

extern int16_t g_gridMain[GRID_ROWS][GRID_COLS];
extern int16_t g_gridAuxA[GRID_ROWS][GRID_COLS];
extern int16_t g_gridAuxB[GRID_ROWS][GRID_COLS];


inline LevelChange changeList[MAX_CHANGES];     // 600 entrées max
inline uint16_t gameGrid[GRID_ROWS][GRID_COLS]; // Base: 0x127E

void configureFileMode();
void invokeExternalCallback();
void advanceToNextLevelOrBlock();
bool canPlaceEntityAtPosition(uint16_t pos, uint16_t level, uint16_t value, uint8_t tie);
void processCallbackQueueFromEngineEvent();

extern int16_t g_entityIndexGrid[GRID_ROWS][GRID_COLS];

extern const uint8_t entityTable_10BC[];

#endif // GAME_H