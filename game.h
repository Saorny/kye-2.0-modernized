#ifndef GAME_H
#define GAME_H

// =====================
// Includes minimaux
// =====================
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdlib>      // std::getenv
#include <cstring>
#include <string_view>
#include <optional>
#include <SDL3/SDL.h>
#include <unordered_map>
#include <iostream>

#include "file.h"


// =====================
// Enums
// =====================
enum class GameInteractionMode : int16_t {
    NormalPlay   = 0,
    PendingBlock = 1,
};

extern GameInteractionMode g_interactionMode;

// =====================
// Forward declarations
// =====================
using CallbackFn = void (*)();
struct CallbackQueueEntry;

struct EntityActionStruct {
    uint16_t entityType;
    uint16_t row;
    uint16_t col;
    uint16_t timer;
};

#pragma pack(push, 1)
struct CallbackQueueEntry {
    uint8_t state;
    uint8_t priority;
    uint16_t offset;
    uint16_t segment;
};
#pragma pack(pop)

// =====================
// Constants
// =====================
static constexpr int GRID_ROWS = 20;
static constexpr int GRID_COLS = 30;
static constexpr int MAX_CHANGES = GRID_ROWS * GRID_COLS;
static constexpr int kMaxEntityLines = 256;

static constexpr std::int16_t CELL_FLAG_EMPTY    = static_cast<std::int16_t>(0xFFFF);
static constexpr std::int16_t CELL_FLAG_SENTINEL = static_cast<std::int16_t>(0xFFFE);

extern std::int16_t srcRow;
extern std::int16_t srcCol;

// =====================
// GameState
// =====================
class GameState {
public:
    static constexpr int MAX_NUM_ENTITIES = 256;

    std::vector<EntityActionStruct> entities;

    int entityMap[GRID_ROWS][GRID_COLS];
    int rightEntityMap[GRID_ROWS][GRID_COLS]; // 127C
    int leftEntityMap[GRID_ROWS][GRID_COLS]; // 1280
    int topEntityMap[GRID_ROWS][GRID_COLS];
    int bottomEntityMap[GRID_ROWS][GRID_COLS]; // 12A6

    int auxTopRightEntityMap[GRID_ROWS][GRID_COLS]; // 12A4
    int auxBottomRightEntityMap[GRID_ROWS][GRID_COLS]; // 12A8

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

enum class NewLevelDialogResult : uint8_t {
    None,
    Accepted,
    Cancelled
};

enum class EntityType : std::uint16_t
{
    // --- Empty / Player
    None = 0x0000,

    // --- Walls
    Wall1 = 0xFFFD,
    Wall2 = 0xFFFC,
    Wall3 = 0xFFFB,
    Wall4 = 0xFFFA,
    Wall5 = 0xFFF9,
    Wall6 = 0xFFF8,
    Wall7 = 0xFFF7,
    Wall8 = 0xFFF6,
    Wall9 = 0xFFF5,

    YellowBrick = 0xFFF4,
    Diamond = 0xFFF3,
    OneWayLeftToRight = 0xFFF2,
    OneWayRightToLeft = 0xFFF1,
    OneWayTopToBottom = 0xFFF0,
    OneWayBottomToTop = 0xFFEF,

    // --- Mobiles
    PushableBrick = 0x0000,
    ArrowUp = 0x0001,
    ArrowDown = 0x0002,
    ArrowLeft = 0x0003,
    ArrowRight = 0x0004,

    MagnetVertical = 0x0005,
    MagnetHorizontal = 0x0006,

    PusherUp = 0x0007,
    PusherDown = 0x0008,
    PusherLeft = 0x0009,
    PusherRight = 0x000A,

    CurvedArrowUp = 0x000B,
    CurvedArrowDown = 0x000C,
    CurvedArrowLeft = 0x000D,
    CurvedArrowRight = 0x000E,

    EnemyPropeller = 0x000F,
    EnemyJaw = 0x0010,
    EnemyPurple = 0x0011,
    EnemySnake = 0x0012,
    EnemyPropellerRound = 0x0013,

    DeflectorLeft = 0x0014,
    DeflectorRight = 0x0015,
    RoundedPushableBrick = 0x0016,

    UnknownA1 = 0x0017,
    UnknownA2 = 0x0018,
    UnknownA3 = 0x0019,
    UnknownA4 = 0x001A,

    Dispenser1 = 0x001B,
    Dispenser2 = 0x001C,
    Dispenser3 = 0x001D,
    Dispenser4 = 0x001E,

    Lava = 0x001F,
    Lava2 = 0x0020,

    Countdown = 0x003B,
    UnknownX = 0x003A,
    UnknownY = 0x0039,
    UnknownZ = 0x0038,
    UnknownLeftBrace = 0x0037,
    UnknownPipe = 0x0036
};

struct EntityMappingEntry {
    EntityClass category;
    uint8_t     unk;
    EntityType param;
    char        asciiChar;
};

constexpr std::array<EntityMappingEntry, 56> ENTITY_MAPPINGS = {{

    { EntityClass::Empty,  0x00, EntityType::None, ' ' },
    { EntityClass::Player, 0x00, EntityType::None, 'K' },

    // Walls
    { EntityClass::Fixed, 0x00, EntityType::Wall1, '1' },
    { EntityClass::Fixed, 0x00, EntityType::Wall2, '2' },
    { EntityClass::Fixed, 0x00, EntityType::Wall3, '3' },
    { EntityClass::Fixed, 0x00, EntityType::Wall4, '4' },
    { EntityClass::Fixed, 0x00, EntityType::Wall5, '5' },
    { EntityClass::Fixed, 0x00, EntityType::Wall6, '6' },
    { EntityClass::Fixed, 0x00, EntityType::Wall7, '7' },
    { EntityClass::Fixed, 0x00, EntityType::Wall8, '8' },
    { EntityClass::Fixed, 0x00, EntityType::Wall9, '9' },

    { EntityClass::Fixed, 0x00, EntityType::YellowBrick, 'e' },
    { EntityClass::Fixed, 0x00, EntityType::Diamond, '*' },
    { EntityClass::Fixed, 0x00, EntityType::OneWayLeftToRight, 'f' },
    { EntityClass::Fixed, 0x00, EntityType::OneWayRightToLeft, 'g' },
    { EntityClass::Fixed, 0x00, EntityType::OneWayTopToBottom, 'h' },
    { EntityClass::Fixed, 0x00, EntityType::OneWayBottomToTop, 'i' },

    { EntityClass::Mobile, 0x00, EntityType::PushableBrick, 'b' },
    { EntityClass::Mobile, 0x00, EntityType::ArrowUp, 'u' },
    { EntityClass::Mobile, 0x00, EntityType::ArrowDown, 'd' },
    { EntityClass::Mobile, 0x00, EntityType::ArrowLeft, 'l' },
    { EntityClass::Mobile, 0x00, EntityType::ArrowRight, 'r' },

    { EntityClass::Mobile, 0x00, EntityType::MagnetVertical, 's' },
    { EntityClass::Mobile, 0x00, EntityType::MagnetHorizontal, 'S' },

    { EntityClass::Mobile, 0x00, EntityType::PusherUp, 'U' },
    { EntityClass::Mobile, 0x00, EntityType::PusherDown, 'D' },
    { EntityClass::Mobile, 0x00, EntityType::PusherLeft, 'L' },
    { EntityClass::Mobile, 0x00, EntityType::PusherRight, 'R' },

    { EntityClass::Mobile, 0x00, EntityType::CurvedArrowUp, '^' },
    { EntityClass::Mobile, 0x00, EntityType::CurvedArrowDown, 'v' },
    { EntityClass::Mobile, 0x00, EntityType::CurvedArrowLeft, '<' },
    { EntityClass::Mobile, 0x00, EntityType::CurvedArrowRight, '>' },

    { EntityClass::Mobile, 0x00, EntityType::EnemyPropeller, 'T' },
    { EntityClass::Mobile, 0x00, EntityType::EnemyJaw, 'E' },
    { EntityClass::Mobile, 0x00, EntityType::EnemyPurple, 'C' },
    { EntityClass::Mobile, 0x00, EntityType::EnemySnake, '~' },
    { EntityClass::Mobile, 0x00, EntityType::EnemyPropellerRound, '[' },

    { EntityClass::Mobile, 0x00, EntityType::DeflectorLeft, 'a' },
    { EntityClass::Mobile, 0x00, EntityType::DeflectorRight, 'c' },
    { EntityClass::Mobile, 0x00, EntityType::RoundedPushableBrick, 'B' },

    { EntityClass::Mobile, 0x00, EntityType::UnknownA1, 'A' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownA2, 'A' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownA3, 'A' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownA4, 'A' },

    { EntityClass::Mobile, 0x00, EntityType::Dispenser1, 'F' },
    { EntityClass::Mobile, 0x00, EntityType::Dispenser2, 'F' },
    { EntityClass::Mobile, 0x00, EntityType::Dispenser3, 'F' },
    { EntityClass::Mobile, 0x00, EntityType::Dispenser4, 'F' },

    { EntityClass::Mobile, 0x00, EntityType::Lava, 'H' },
    { EntityClass::Mobile, 0x00, EntityType::Lava2, 'H' },

    { EntityClass::Mobile, 0x00, EntityType::Countdown, 'w' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownX, 'x' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownY, 'y' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownZ, 'z' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownLeftBrace, '{' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownPipe, '|' }
}};

// =====================
// Globals (DECLARATIONS ONLY)
// =====================
extern GameState g_gameState;

extern uint16_t cursorRow, cursorCol;
extern uint16_t spawnRow, spawnCol;
extern uint16_t selectedTileValue, selectionState, selectedEntityIndex;

extern int playerRow, playerCol;
extern int previousRow, previousCol;

extern int g_isMouseCaptured;
extern int g_pendingEventCount;
extern int frameCounter;

extern uint16_t randomSeedLow, randomSeedHigh;

extern char g_levelHintTextBuffer[512]; // 0x2A4A
extern char g_secondaryText[256]; 
extern char g_levelName[256]; // 0x2A9A
extern char g_statusLineBuffer[512]; // 0x29FA
extern int g_levelIndex; 

extern bool g_openFileDialogAccepted;
extern char g_openFileDialogPath[0x100];
extern char g_defaultOpenSuffix[0x40];

extern uint8_t   EntityTable[kMaxEntityLines];
extern std::int16_t  g_cellClickFlags[GRID_ROWS * GRID_COLS];
extern std::int16_t  g_entityIndexGrid[GRID_ROWS][GRID_COLS];

extern uint16_t gameGrid[GRID_ROWS][GRID_COLS]; // 127E

extern std::int16_t g_gridMain[GRID_ROWS][GRID_COLS];
extern std::int16_t g_gridAuxA[GRID_ROWS][GRID_COLS];
extern std::int16_t g_gridAuxB[GRID_ROWS][GRID_COLS];

extern CallbackFn g_callbackTable[256];
extern CallbackFn g_validateInternalBufferCallback;
extern CallbackFn g_configureFileMode;
extern CallbackFn g_invokeExternalCallbackFn;

extern uint16_t g_notificationHandlerSlotWord[]; // 0,1, or legacy function pointer value
extern uint8_t  g_notificationHandlerParam[];    // per-slot byte parameter

extern int matchedEntryCount;

// =====================
// Functions
// =====================

void resetLevelStateMemory();
void startPollingTimer();

void runLegacyCallbackQueue();

void processCallbackQueue(CallbackQueueEntry* start,
                          CallbackQueueEntry* end);
void processCallbackQueueFromEngineEvent();

void configureFileMode();
void invokeExternalCallback();
void advanceToNextLevelOrBlock();
void drainPendingEvents();
bool canPlaceEntityAtPosition(uint16_t pos, uint16_t level, uint16_t value, uint8_t tie);
int loadLevelByIndex(int level);
void updateNextLevelMenuItem();
int clearStatusLine(const char* str);
void handleEvent(const SDL_Event& e);
int renderLivesAndLevelInfo();
void renderFrameByInteractionMode();
void handleDialogClose(NewLevelDialogResult result);

using namespace std;

// =====================
// Tables externes
// =====================
extern const uint16_t g_table10C8[];
extern const uint16_t g_table10C6[];
extern const uint8_t  entityTable_10BC[];
extern const int8_t g_thresholdTable[];

extern std::int16_t baseX;
extern std::int16_t baseY;

using EventHandler = void(*)();
using VoidCallback = void (*)();

extern int g_currentNameIndex;

static constexpr std::uint16_t TILE_WALL5 = 0xFFF9;
static constexpr std::uint16_t TILE_EMPTY = 0xFFFF;

extern std::uint16_t g_activeSpawnerCount;

extern std::uint16_t exitCoordLeft, exitCoordRight, exitState;

// Ces 3 “defaults” remplacent tes copies ds:045A, ds:0460, ds:0468.
// Mets-y les contenus que tu as déjà dans tes tables/strings.
extern const char* kDefaultLevelName;
extern const char* kDefaultHintText;
extern const char* kDefaultStatusLine;

#pragma pack(push, 1)
struct DecodeTileEntry {
    std::uint16_t a;      // offset +0 (asm: [si+2C0])
    std::uint16_t b;      // offset +2 (asm: [si+2C2])
    std::uint8_t  symbol; // offset +4 (asm: [si+2C4])
};
#pragma pack(pop)

extern const DecodeTileEntry g_decodeTileTable[];

struct Spawner {
    EntityType  type;   // [172Eh]
    int row;            // [1730h]
    int col;            // [1732h]
    int animFrame;      // [1734h]
};

extern std::vector<Spawner> g_spawners;

struct SpriteSheet16 {
    // dessine un sprite 16x16 depuis (srcX,srcY) vers (dstX,dstY)
    void blit16(int dstX, int dstY, int srcX, int srcY);
};

// ta sheet "bitmap_block"
extern SpriteSheet16 g_blockSheet;
extern NewLevelDialogResult g_newLevelDialogResult;

extern std::string g_levelInput;
extern SDL_FRect g_panel;
extern SDL_FRect g_okBtn;
extern SDL_FRect g_cancelBtn;
extern bool g_newLevelDialogOpen;

extern char g_speedPrefix3[4];
extern char g_speedTag3[4];

extern const char* kEnvKey;   // dseg02:10EC "TZ\0"
extern const char* kDefaultPrefix;  // dseg02:10EF "EST\0"
extern const char* kDefaultTag;  // dseg02:10F3 "EDT\0"

extern bool     speedFallbackUsed;
extern uint16_t speedMultiplierHigh;
extern uint16_t speedMultiplierLow; // fallback legacy

extern char** g_environmentStringPtrs;

extern int pendingRow;
extern int pendingCol;

struct LevelChange
{
    uint16_t tileId;  // +0
    uint16_t row;     // +2
    uint16_t col;     // +4
    uint16_t speed;   // +6
};

extern LevelChange changeList[MAX_CHANGES];
extern uint16_t changeCount;

using EntityHandler = void(*)(int entityIndex);

extern const std::unordered_map<int,const char*> exceptionMessages;

struct PackedMessage
{
    const char* message;
    uint16_t eventCode;
};

constexpr PackedMessage kFloatingPointMessage{
    "Floating Point: Square Root of Negative Number",
    3
};

extern int remainingDiamondCount;

struct NotificationEntry
{
    const char* message;
    int eventCode;
};

enum NotificationCode
{
    NOTIFY_FATAL = 0,
    NOTIFY_ABORT = 1,
    NOTIFY_FILE_ERROR = 2,
    NOTIFY_MEMORY_ERROR = 3,
    NOTIFY_DATA_ERROR = 4,
    NOTIFY_STATE_ERROR = 5
};

extern const std::unordered_map<int, NotificationEntry> notifications;
extern SDL_Cursor* g_cursorArrow;

typedef void (*VoidCallback)();

#pragma pack(push,1)
struct CallbackEntry
{
    uint8_t state;
    uint8_t priority;
    VoidCallback callback;
};
#pragma pack(pop)

constexpr int MAX_CALLBACKS = 32;

extern std::array<CallbackEntry, MAX_CALLBACKS> g_callbackQueue;

enum class MenuCommand
{
    NewGame,
    Restart,
    GotoLevel,
    OpenFile,
    ToggleOption,
    Help,
    About,
    What,
    Quit,
    EnterEditMode,
    ExitEditMode
};

extern bool g_nextLevelEnabled;
extern bool g_hasDeviceContext;
extern SDL_Window* g_windowHandle2;

extern SDL_Window* g_toolboxWindow;
extern SDL_Renderer* g_toolboxRenderer;
extern bool toolboxCreated;

extern std::int16_t cellWidth;
extern std::int16_t cellHeight;

void moveAndRedrawEntity(int entityIndex, int row, int col);
void markEntryInactive(int index);
void finalizeLevelVisuals();
void updateLivesDisplay();
void loadLevelRow(int col, const char* data);
int postLoadLevel();
void processKyeCollision(int row, int col);
void handleEngineEvent(uint16_t, uint16_t, uint16_t);
void cleanupAndTerminate(int code);
void handleUnknownEntityType(int entityIndex);
void setStatusText(const std::string& text);
void handleClickOnGridCell(uint32_t);
void updateGridCell(int row, int col);
void executeCurrentEntryAction(std::int16_t actionType,
                               std::int16_t tileId,
                               std::int16_t row,
                               std::int16_t col);
int handleStandardCellClick(int row,int col);
void handleSpecialSentinelClick();
void renderWallTile(int row, int col, uint16_t tileValue);
void spawnAtIfEmpty(int,int,int,uint16_t&);
void animateMonsters();
void invalidateWindow();
void updateWindow();
void gameMainLoopTick();
int tickLevelFlow();
void animateDiamonds();

#endif // GAME_H
