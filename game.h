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

struct Entity {
    uint16_t position;  // +0 : probablement une coordonnée ou un index
    uint8_t width;      // +2 : largeur
    uint8_t height;     // +3 : hauteur
};

struct EntityParams {
    uint8_t param0;     // +0 : poids ? score de base ?
    uint8_t param1;     // +1 : offset additionnel
    uint8_t unused;     // +2 : pas utilisé ici
    uint8_t param3;     // +3 : bonus/malus final
};

struct TileMapping {
    uint16_t tileCode;
    uint16_t param;
    uint8_t  symbol;
};

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

constexpr int ROWS = 20;
constexpr int COLS = 30;
constexpr int MAX_CHANGES = ROWS * COLS;

inline LevelChange changeList[MAX_CHANGES];     // 600 entrées max
inline uint16_t gameGrid[ROWS][COLS]; // Base: 0x127E

static UINT_PTR g_timerId = 0;
static bool g_timerActive = false;
char g_levelFilePath[MAX_PATH];
char displayTextBuffer[512];
char g_levelName[512];
char g_levelHint[512];
int allowedAttributes = 0x0FFFF;
int globalCompatFlags = 0x4000;
constexpr int kMaxEntityLines = 256;
extern uint8_t EntityTable[kMaxEntityLines];

int currentLevelIndex = 1;
int totalLevelCount = 1;
int matchingEntryCount = 0;
int hasPendingDialogBox = 0;
bool hasEntryList = false;
int remainingLives = 3;
int levelLoadState = 1;
int levelTransitionFlag = 0;
int selectedObjectIndex = -1;

char *loadedLevelName[256];
char *loadedLevelHint[256];

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

uint16_t currentLevelStateVersion = 0;
int levelChangeTable[256];
int playerRow = 0;
int playerCol = 0;

uint16_t randomSeedLow = 0x4E35;   // Valeur d'initialisation utilisée dans le jeu
uint16_t randomSeedHigh = 0x015A;  // Pareil
int previousCol = 0;
int previousRow = 0;
int row = 0;
int col = 0;
int mouseCaptureFlag = 0;
int someGlobalCondition = 0;
extern char** environmentList; 
int eventCounter = 0;
int frameCounter = 0;

const char* MSG_CANNOT_OPEN_FILE = "Cannot open file: ";

struct EntityParams {
    int type;
    int row;
    int col;
    int timer;
};

class GameState {
public:
    static constexpr int GRID_ROWS = 40;
    static constexpr int GRID_COLS = 40;
    static constexpr int NUM_ENTITIES = 256;

    std::vector<EntityParams> entities;  // 0x172E, 0x1730, 0x1732, 0x1734 combinés

    int entityMap[GRID_ROWS][GRID_COLS];         // 0x1280
    int rightEntityMap[GRID_ROWS][GRID_COLS];    // 0x127C
    int leftEntityMap[GRID_ROWS][GRID_COLS];     // 0x122E
    int topEntityMap[GRID_ROWS][GRID_COLS];      // 0x1256
    int bottomEntityMap[GRID_ROWS][GRID_COLS];   // 0x12A6

    std::vector<int> bottomLeftEntityTable;      // 0x12CE (guess)

    GameState() {
        entities.resize(NUM_ENTITIES, {0, 0, 0, 0});
        bottomLeftEntityTable.resize(NUM_ENTITIES, -1);

        std::memset(entityMap, -1, sizeof(entityMap));
        std::memset(rightEntityMap, -1, sizeof(rightEntityMap));
        std::memset(leftEntityMap, -1, sizeof(leftEntityMap));
        std::memset(topEntityMap, -1, sizeof(topEntityMap));
        std::memset(bottomEntityMap, -1, sizeof(bottomEntityMap));
    }
};

GameState g_gameState;


void resetLevelStateMemory();
uint32_t computeAdjustedTime(Entity* entity, const EntityParams* params);
void startPollingTimer();

using WriteCallback = void(*)(char*& writePtr, const char* src, int length);

#endif // GAME_H