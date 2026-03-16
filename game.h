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

// static constexpr std::int16_t CELL_FLAG_EMPTY    = static_cast<std::int16_t>(0xFFFF);
// static constexpr std::int16_t CELL_FLAG_SENTINEL = static_cast<std::int16_t>(0xFFFE);

extern std::int16_t srcRow;
extern std::int16_t srcCol;


enum class EntityType : std::uint16_t
{
    // --- Empty / Player
    EMPTY = 00000,
    // --- Empty / Player
    EMPTY_CELL = 0xFFFF,

    // --- Walls
    BOTTOM_LEFT_ROUND_WALL = 0xFFFD,
    BOTTOM_ROUNDED_WALL = 0xFFFC,
    BOTTOM_RIGHT_ROUND_WALL = 0xFFFB,
    LEFT_ROUNDED_WALL = 0xFFFA,
    SQUARE_WALL = 0xFFF9,
    RIGHT_ROUNDED_WALL = 0xFFF8,
    TOP_LEFT_ROUNDED_WALL = 0xFFF7,
    TOP_ROUNDED_WALL = 0xFFF6,
    TOP_RIGHT_ROUNDED_WALL = 0xFFF5,

    BREAKABLE_BRICK = 0xFFF4,
    DIAMOND = 0xFFF3,
    ONE_WAY_LEFT_TO_RIGHT_PORTAL = 0xFFF2,
    ONE_WAY_RIGHT_TO_LEFT_PORTAL = 0xFFF1,
    ONE_WAY_TOP_TO_BOTTOM = 0xFFF0,
    ONE_WAY_BOTTOM_TO_TOP = 0xFFEF,

    // --- Mobiles
    PushableBrick = 0x0000,
    ARROW_UP = 0x0001,
    ARROW_DOWN = 0x0002,
    ArrowLeft = 0x0003,
    ArrowRight = 0x0004,

    MagnetVertical = 0x0005,
    MagnetHorizontal = 0x0006,

    PUSHER_UP = 0x0007,
    PUSHER_DOWN = 0x0008,
    PUSHER_LEFT = 0x0009,
    PUSHER_RIGHT = 0x000A,

    CURVED_ARROW_UP = 0x000B,
    CURVED_ARROW_DOWN = 0x000C,
    CURVED_ARROW_LEFT = 0x000D,
    CURVED_ARROW_RIGHT = 0x000E,

    EnemyPropeller = 0x000F,
    EnemyJaw = 0x0010,
    EnemyPurple = 0x0011,
    EnemySnake = 0x0012,
    EnemyPropellerRound = 0x0013,

    DEFLECTOR_LEFT = 0x0014,
    DEFLECTOR_RIGHT = 0x0015,
    RoundedPushableBrick = 0x0016,

    UnknownA1 = 0x0017,
    UnknownA2 = 0x0018,
    UnknownA3 = 0x0019,
    UnknownA4 = 0x001A,

    DISPENSER1 = 0x001B,
    DISPENSER2 = 0x001C,
    DISPENSER3 = 0x001D,
    DISPENSER4 = 0x001E,

    Lava = 0x001F,
    Lava2 = 0x0020,

    COUNTDOWN = 0x003B,
    UnknownX = 0x003A,
    UnknownY = 0x0039,
    UnknownZ = 0x0038,
    UnknownLeftBrace = 0x0037,
    UnknownPipe = 0x0036,
    UNKNOWN = 0x0032,
    KYE_LOCATION = 0xFFFE, 
};

struct EntityInfo {
    EntityType entityType;// [172Eh]
    uint16_t row;       // [1730h]
    uint16_t col;       // [1732h]
    uint16_t animFrame;     // [1734h]
};

// =====================
// GameState
// =====================
class GameState {
public:
    static constexpr int MAX_NUM_ENTITIES = 256;

    std::array<EntityInfo,256> entities; // 172E

    EntityType tileMap[GRID_ROWS][GRID_COLS]; // 127E
    int16_t rightEntityMap[GRID_ROWS][GRID_COLS]; // 127C
    int16_t leftEntityMap[GRID_ROWS][GRID_COLS]; // 1280
    int16_t topEntityMap[GRID_ROWS][GRID_COLS];
    int16_t bottomEntityMap[GRID_ROWS][GRID_COLS]; // 12A6
    int16_t auxTopRightEntityMap[GRID_ROWS][GRID_COLS]; // 12A4
    int16_t auxBottomRightEntityMap[GRID_ROWS][GRID_COLS]; // 12A8
    int16_t auxMap1282[GRID_ROWS][GRID_COLS];
    int16_t auxMap127A[GRID_ROWS][GRID_COLS];
    int16_t auxMap12CE[GRID_ROWS][GRID_COLS];

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

struct EntityMappingEntry {
    EntityClass category;
    uint8_t     unk;
    EntityType param;
    char        asciiChar;
};

// 02C0
constexpr std::array<EntityMappingEntry, 56> ENTITY_MAPPINGS = {{

    { EntityClass::Empty,  0x00, EntityType::EMPTY_CELL, ' ' },
    { EntityClass::Player, 0x00, EntityType::EMPTY_CELL, 'K' },

    // Walls
    { EntityClass::Fixed, 0x00, EntityType::BOTTOM_LEFT_ROUND_WALL, '1' },
    { EntityClass::Fixed, 0x00, EntityType::BOTTOM_ROUNDED_WALL, '2' },
    { EntityClass::Fixed, 0x00, EntityType::BOTTOM_RIGHT_ROUND_WALL, '3' },
    { EntityClass::Fixed, 0x00, EntityType::LEFT_ROUNDED_WALL, '4' },
    { EntityClass::Fixed, 0x00, EntityType::SQUARE_WALL, '5' },
    { EntityClass::Fixed, 0x00, EntityType::RIGHT_ROUNDED_WALL, '6' },
    { EntityClass::Fixed, 0x00, EntityType::TOP_LEFT_ROUNDED_WALL, '7' },
    { EntityClass::Fixed, 0x00, EntityType::TOP_ROUNDED_WALL, '8' },
    { EntityClass::Fixed, 0x00, EntityType::TOP_RIGHT_ROUNDED_WALL, '9' },

    { EntityClass::Fixed, 0x00, EntityType::BREAKABLE_BRICK, 'e' },
    { EntityClass::Fixed, 0x00, EntityType::DIAMOND, '*' },
    { EntityClass::Fixed, 0x00, EntityType::ONE_WAY_LEFT_TO_RIGHT_PORTAL, 'f' },
    { EntityClass::Fixed, 0x00, EntityType::ONE_WAY_RIGHT_TO_LEFT_PORTAL, 'g' },
    { EntityClass::Fixed, 0x00, EntityType::ONE_WAY_TOP_TO_BOTTOM, 'h' },
    { EntityClass::Fixed, 0x00, EntityType::ONE_WAY_BOTTOM_TO_TOP, 'i' },

    { EntityClass::Mobile, 0x00, EntityType::PushableBrick, 'b' },
    { EntityClass::Mobile, 0x00, EntityType::ARROW_UP, 'u' },
    { EntityClass::Mobile, 0x00, EntityType::ARROW_DOWN, 'd' },
    { EntityClass::Mobile, 0x00, EntityType::ArrowLeft, 'l' },
    { EntityClass::Mobile, 0x00, EntityType::ArrowRight, 'r' },

    { EntityClass::Mobile, 0x00, EntityType::MagnetVertical, 's' },
    { EntityClass::Mobile, 0x00, EntityType::MagnetHorizontal, 'S' },

    { EntityClass::Mobile, 0x00, EntityType::PUSHER_UP, 'U' },
    { EntityClass::Mobile, 0x00, EntityType::PUSHER_DOWN, 'D' },
    { EntityClass::Mobile, 0x00, EntityType::PUSHER_LEFT, 'L' },
    { EntityClass::Mobile, 0x00, EntityType::PUSHER_RIGHT, 'R' },

    { EntityClass::Mobile, 0x00, EntityType::CURVED_ARROW_UP, '^' },
    { EntityClass::Mobile, 0x00, EntityType::CURVED_ARROW_DOWN, 'v' },
    { EntityClass::Mobile, 0x00, EntityType::CURVED_ARROW_LEFT, '<' },
    { EntityClass::Mobile, 0x00, EntityType::CURVED_ARROW_RIGHT, '>' },

    { EntityClass::Mobile, 0x00, EntityType::EnemyPropeller, 'T' },
    { EntityClass::Mobile, 0x00, EntityType::EnemyJaw, 'E' },
    { EntityClass::Mobile, 0x00, EntityType::EnemyPurple, 'C' },
    { EntityClass::Mobile, 0x00, EntityType::EnemySnake, '~' },
    { EntityClass::Mobile, 0x00, EntityType::EnemyPropellerRound, '[' },

    { EntityClass::Mobile, 0x00, EntityType::DEFLECTOR_LEFT, 'a' },
    { EntityClass::Mobile, 0x00, EntityType::DEFLECTOR_RIGHT, 'c' },
    { EntityClass::Mobile, 0x00, EntityType::RoundedPushableBrick, 'B' },

    { EntityClass::Mobile, 0x00, EntityType::UnknownA1, 'A' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownA2, 'A' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownA3, 'A' },
    { EntityClass::Mobile, 0x00, EntityType::UnknownA4, 'A' },

    { EntityClass::Mobile, 0x00, EntityType::DISPENSER1, 'F' },
    { EntityClass::Mobile, 0x00, EntityType::DISPENSER2, 'F' },
    { EntityClass::Mobile, 0x00, EntityType::DISPENSER3, 'F' },
    { EntityClass::Mobile, 0x00, EntityType::DISPENSER4, 'F' },

    { EntityClass::Mobile, 0x00, EntityType::Lava, 'H' },
    { EntityClass::Mobile, 0x00, EntityType::Lava2, 'H' },

    { EntityClass::Mobile, 0x00, EntityType::COUNTDOWN, 'w' },
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
extern uint16_t kyeRow, kyeCol;
extern EntityType selectedTileValue;
extern EntityType selectedEntityIndex;
extern EntityType selectionState;

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
void renderAllObjects();
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

extern std::uint16_t g_activeSpawnerCount;

extern EntityType exitCoordLeft;
extern EntityType exitCoordRight;
extern EntityType exitState;

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
int executeCurrentEntryAction(int16_t actionType,
                              EntityType tileId,
                              int16_t row,
                              int16_t col);
int handleStandardCellClick(int row,int col);
void handleSpecialSentinelClick();
void renderWallTile(int row, int col, EntityType tileValue);
void spawnAtIfEmpty(int targetRow, int targetCol, EntityType type, uint16_t& spawnDelayCounter);
void animateMonsters();
void invalidateWindow();
void updateWindow();
void gameMainLoopTick();
int tickLevelFlow();
void animateDiamonds();
void handlePaintOrRenderRequest();

extern std::string g_levelHintText;
extern std::string g_levelPassword;
extern std::string g_levelVictoryText;


#endif // GAME_H
