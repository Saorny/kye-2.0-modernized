#include <iostream>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <cstddef>
#include <tuple>
#include <stdexcept>
#include <cstring>
#include "file.h"
#include "error.h"
#include "util.h"
#include "time.h"
#include "graph.h"
#include "game.h"
#include "tinyfiledialogs.h"

int levelIndex = 1;
int levelCount = 1;
int g_hasPendingModal = 0;
int remainingLives = 3;

int hasLevelTransition = 0;
int selectedObjectIndex = -1;
bool g_timerActive = false;
bool g_optionToggleFlag = false;
int g_oneWayAnimPhase = 1;
uint16_t g_activeSpawnerCount = 0;
std::string g_helpContextFile = "kyehelp.hlp";

using NotificationHandlerFn = void (*)(uint16_t eventCode, uint16_t handlerParam);
extern uintptr_t g_handlerOrStateTable[];   // base == 0x1122 (en words dans le binaire)
extern uint8_t  g_handlerParamTable[];     // base == 0x1134 (byte par index)

std::int16_t baseX = 0;
std::int16_t baseY = 0;

using EntityHandler = void(*)(int);

extern std::string g_levelPassword;
extern std::string g_levelVictoryText;
extern std::string g_levelHintText;

constexpr int kEditorEntryStride = 0x1A;
constexpr int kMaxLevelEntries = 32;

std::int16_t specialCellStateFlag = 0;

std::string g_nameInputBuffer;

std::string g_statusLineText;

static bool g_keyDownFlag = false;

extern std::string g_levelPassword;
extern std::string g_levelVictoryText;
extern std::string g_levelHintText;

// static constexpr int16_t kTargetRequiredState = 0x001F;
// static constexpr int16_t kTargetClearedState  = 0x0020;

static SDL_TimerID g_timerId = 0;

// static constexpr std::int16_t TILE_SPAWN        = (std::uint16_t)0xFFFE;
// static constexpr std::int16_t TILE_EXIT         = (std::uint16_t)0xFFF9;

static void placeTileAndSpawnEntityIfEmpty_Core(
    int row,
    int col,
    EntityType type,
    uint16_t* spawnCounterPtr
);

bool showGotoLevelDialog()
{
    const SDL_MessageBoxButtonData buttons[] =
    {
        { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Level 1" },
        { 0, 2, "Level 5" },
        { 0, 3, "Level 10" },
        { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Cancel" }
    };

    const SDL_MessageBoxData messageboxdata =
    {
        SDL_MESSAGEBOX_INFORMATION,
        nullptr,
        "Goto Level",
        "Choose a level",
        SDL_arraysize(buttons),
        buttons,
        nullptr
    };

    int buttonid;
    SDL_ShowMessageBox(&messageboxdata, &buttonid);

    if (buttonid > 0)
    {
        levelIndex = buttonid;
        loadLevelByIndex(levelIndex);
        isLeftMouseDragActive = 0;
        return true;
    }

    return false;
}

static int tileToEntityIndex(EntityType tile)
{
    const int16_t signedTile = static_cast<int16_t>(static_cast<uint16_t>(tile));

    if (signedTile < 0)
    {
        return -1;
    }

    if (signedTile >= static_cast<int>(g_activeSpawnerCount))
    {
        return -1;
    }

    return signedTile;
}

static int findEntityAt(int row, int col)
{
    for (int i = 0; i < static_cast<int>(g_activeSpawnerCount); ++i)
    {
        if (g_gameState.entities[i].entityType != EntityType::EMPTY_CELL &&
            g_gameState.entities[i].row == row &&
            g_gameState.entities[i].col == col)
        {
            return i;
        }
    }

    return -1;
}

static inline bool isFixedTile(EntityType type)
{
    return type >= EntityType::TOP_RIGHT_ROUND_WALL && type <= EntityType::BOTTOM_LEFT_ROUND_WALL;
}

static void onDiamondCollected()
{
    int diamondCount = 0;

    for (int row = 0; row < GRID_ROWS ; ++row)
    {
        for (int col = 0; col < GRID_COLS ; ++col)
        {
            if (g_gameState.tileMap[row][col] == EntityType::DIAMOND)
                ++diamondCount;
        }
    }

    if (diamondCount == 0)
        isLevelCompleted = true;
}

static Uint32 g_pollEventType = 0;

static constexpr Uint32 kPollingIntervalMs = 100; // 0x64

SDL_Cursor* g_cursorArrow = nullptr;

inline int cellIndex(std::int16_t row, std::int16_t col)
{
    return row * GRID_COLS + col;
}

static Uint32 SDLCALL PollTimerCallback(void* /*userdata*/, SDL_TimerID /*timerID*/, Uint32 interval)
{
    SDL_Event ev{};
    ev.type = g_pollEventType;
    // tu peux remplir ev.user.data1/data2 si besoin, ou un code
    SDL_PushEvent(&ev);

    return interval; // continuer avec le même interval
}

void startPollingTimer()
{
    if (g_pollEventType == 0) {
        g_pollEventType = SDL_RegisterEvents(1);
        if (g_pollEventType == 0) {
            g_timerActive = false;
            g_timerId = 0;
            return; // échec register
        }
    }

    // 2) Si déjà actif, on remplace proprement
    if (g_timerActive && g_timerId != 0) {
        SDL_RemoveTimer(g_timerId);
        g_timerId = 0;
        g_timerActive = false;
    }

    // 3) Créer le timer
    g_timerId = SDL_AddTimer(kPollingIntervalMs, PollTimerCallback, nullptr);
    g_timerActive = (g_timerId != 0);
}

void advanceToNextLevelOrBlock()
{
    if (g_hasDeviceContext == false) {
        return;
    }

    g_hasDeviceContext = false;
}

void updateCountdownEntities()
{
    if (frameCounter % 30 != 0)
        return;

    for (int i = 0; i < g_activeSpawnerCount; ++i)
    {
        auto& e = g_gameState.entities[i];

        if (e.entityType < EntityType::COUNTDOWN_0 || e.entityType > EntityType::COUNTDOWN_9)
            continue;

        int row = e.row;
        int col = e.col;

        if (e.entityType > EntityType::COUNTDOWN_0)
        {
            e.entityType = EntityType((int)e.entityType - 1);
            moveAndRedrawEntity(i, row, col);
        }
        else
        {
            drawRectangleFromGrid(row, col);
            markEntryInactive(i);
        }
    }

    finalizeLevelVisuals();
}

// void moveAndRedrawEntity(int entityIndex, int newRow, int newCol)
// {
//     auto& e = g_gameState.entities[entityIndex];

//     g_gameState.tileMap[e.row][e.col] = EntityType::EMPTY_CELL;
//     e.row = newRow;
//     e.col = newCol;
//     g_gameState.tileMap[newRow][newCol] = (EntityType)entityIndex;
//     renderEntity(entityIndex);
// }

void moveAndRedrawEntity(int entityIndex, int newRow, int newCol)
{
    auto& e = g_gameState.entities[entityIndex];

    drawRectangleFromGrid(e.row, e.col);

    e.row = newRow;
    e.col = newCol;

    g_gameState.tileMap[newRow][newCol] = (EntityType)entityIndex;

    renderEntity(entityIndex);
}

void showNewLevelDialog(SDL_Window* window)
{
    g_window = window;
    g_newLevelDialogOpen = true;
    g_newLevelDialogResult = NewLevelDialogResult::None;
    g_levelInput.clear();

    SDL_StartTextInput(g_window);
}

// bool tryMoveMagnet(int entityIndex)
// {
//     auto& s = g_gameState;

//     int row = s.entities[entityIndex].row;
//     int col = s.entities[entityIndex].col;

//     // LEFT → move RIGHT (type 5)
//     if (s.leftEntityMap[row][col] == -1)
//     {
//         int target = s.entityToLeft[row][col];

//         if (target >= 0 &&
//             target < g_activeSpawnerCount &&
//             s.entities[target].entityType == EntityType::MAGNET_VERTICAL)
//         {
//             moveAndRedrawEntity(entityIndex, row, col + 1);
//             return true;
//         }
//     }

//     // RIGHT → move LEFT (type 5)
//     if (s.rightEntityMap[row][col] == -1)
//     {
//         int target = s.entityToRight[row][col];

//         if (target >= 0 &&
//             target < g_activeSpawnerCount &&
//             s.entities[target].entityType == EntityType::MAGNET_VERTICAL)
//         {
//             moveAndRedrawEntity(entityIndex, row, col - 1);
//             return true;
//         }
//     }

//     // DOWN → move DOWN (type 6)
//     if (s.bottomEntityMap[row][col] == -1)
//     {
//         int target = s.entityBelow[row][col];

//         if (target >= 0 &&
//             target < g_activeSpawnerCount &&
//             s.entities[target].entityType == EntityType::MAGNET_HORIZONTAL)
//         {
//             moveAndRedrawEntity(entityIndex, row + 1, col);
//             return true;
//         }
//     }

//     // UP → move UP (type 6)
//     if (s.topEntityMap[row][col] == -1)
//     {
//         int target = s.entityAbove[row][col];

//         if (target >= 0 &&
//             target < g_activeSpawnerCount &&
//             s.entities[target].entityType == EntityType::MAGNET_HORIZONTAL)
//         {
//             moveAndRedrawEntity(entityIndex, row - 1, col);
//             return true;
//         }
//     }

//     return false;
// }

int tryApplyMagneticDisplacement(int entityIndex)
{
    auto& s = g_gameState;

    const int row = s.entities[entityIndex].row;
    const int col = s.entities[entityIndex].col;

    // Already held by a horizontal magnet
    {
        const int magnet = findEntityAt(row, col - 1);
        if (magnet >= 0 &&
            s.entities[magnet].entityType == EntityType::MAGNET_HORIZONTAL)
        {
            return 1;
        }
    }

    {
        const int magnet = findEntityAt(row, col + 1);
        if (magnet >= 0 &&
            s.entities[magnet].entityType == EntityType::MAGNET_HORIZONTAL)
        {
            return 1;
        }
    }

    // Already held by a vertical magnet
    {
        const int magnet = findEntityAt(row - 1, col);
        if (magnet >= 0 &&
            s.entities[magnet].entityType == EntityType::MAGNET_VERTICAL)
        {
            return 1;
        }
    }

    {
        const int magnet = findEntityAt(row + 1, col);
        if (magnet >= 0 &&
            s.entities[magnet].entityType == EntityType::MAGNET_VERTICAL)
        {
            return 1;
        }
    }

    return 0;
}

int canMagnetMoveEntity(int entityIndex)
{
    auto& s = g_gameState;

    const int row = s.entities[entityIndex].row;
    const int col = s.entities[entityIndex].col;

    // Horizontal magnet 2 cells left -> move left 1
    {
        const int magnet = findEntityAt(row, col - 2);
        if (magnet >= 0 &&
            s.entities[magnet].entityType == EntityType::MAGNET_HORIZONTAL &&
            findEntityAt(row, col - 1) == -1 &&
            s.tileMap[row][col - 1] == EntityType::EMPTY_CELL)
        {
            moveAndRedrawEntity(entityIndex, row, col - 1);
            return 1;
        }
    }

    // Horizontal magnet 2 cells right -> move right 1
    {
        const int magnet = findEntityAt(row, col + 2);
        if (magnet >= 0 &&
            s.entities[magnet].entityType == EntityType::MAGNET_HORIZONTAL &&
            findEntityAt(row, col + 1) == -1 &&
            s.tileMap[row][col + 1] == EntityType::EMPTY_CELL)
        {
            moveAndRedrawEntity(entityIndex, row, col + 1);
            return 1;
        }
    }

    // Vertical magnet 2 cells above -> move up 1
    {
        const int magnet = findEntityAt(row - 2, col);
        if (magnet >= 0 &&
            s.entities[magnet].entityType == EntityType::MAGNET_VERTICAL &&
            findEntityAt(row - 1, col) == -1 &&
            s.tileMap[row - 1][col] == EntityType::EMPTY_CELL)
        {
            moveAndRedrawEntity(entityIndex, row - 1, col);
            return 1;
        }
    }

    // Vertical magnet 2 cells below -> move down 1
    {
        const int magnet = findEntityAt(row + 2, col);
        if (magnet >= 0 &&
            s.entities[magnet].entityType == EntityType::MAGNET_VERTICAL &&
            findEntityAt(row + 1, col) == -1 &&
            s.tileMap[row + 1][col] == EntityType::EMPTY_CELL)
        {
            moveAndRedrawEntity(entityIndex, row + 1, col);
            return 1;
        }
    }

    return 0;
}

bool checkAndHandleDeathCondition(int index)
{
    auto& s = g_gameState;

    int row = s.entities[index].row;
    int col = s.entities[index].col;

    auto isKye = [&](int r, int c)
    {
        if (r < 0 || r >= GRID_ROWS || c < 0 || c >= GRID_COLS)
            return false;

        int tile = (int)s.tileMap[r][c];

        if (tile == (int)EntityType::KYE_LOCATION)
            return true;

        if (tile >= 0 && tile < g_activeSpawnerCount)
            return s.entities[tile].entityType == EntityType::KYE_LOCATION;

        return false;
    };

    bool death =
        isKye(row, col) ||
        isKye(row, col - 1) ||
        isKye(row - 1, col) ||
        isKye(row + 1, col);

    if (death)
    {
        --remainingLives;
        updateLivesDisplay();
        renderLivesAndLevelInfo();
        return true;
    }

    return false;
}

void updateLivesDisplay()
{
    hasLevelTransition = 1;

    previousRow = kyeRow;
    previousCol = kyeCol;

    if (g_gameState.tileMap[kyeRow][kyeCol] == EntityType::EMPTY_CELL)
    {
        return;
    }

    int scanIteration = 1;
    int scanLeftX = kyeCol - 1;
    int scanBottomY = kyeRow + 1;
    int scanLength = 2;
    int scanRightX = kyeCol + 1;
    int scanTopY = kyeRow - 1;

    while (scanIteration < 5)
    {
        previousRow = scanLeftX;
        previousCol = scanBottomY;

        for (int i = 0; i < scanLength; ++i)
        {
            if (g_gameState.tileMap[previousRow][previousCol] == EntityType::EMPTY_CELL)
            {
                return;
            }

            ++previousRow;
        }

        previousRow = scanRightX;
        previousCol = scanBottomY;

        for (int i = 0; i < scanLength; ++i)
        {
            if (g_gameState.tileMap[previousRow][previousCol] == EntityType::EMPTY_CELL)
            {
                return;
            }

            --previousCol;
        }

        previousRow = scanRightX;
        previousCol = scanTopY;

        for (int i = 0; i < scanLength; ++i)
        {
            if (g_gameState.tileMap[previousRow][previousCol] == EntityType::EMPTY_CELL)
            {
                return;
            }

            --previousRow;
        }

        previousRow = scanLeftX;
        previousCol = scanTopY;

        for (int i = 0; i < scanLength; ++i)
        {
            if (g_gameState.tileMap[previousRow][previousCol] == EntityType::EMPTY_CELL)
            {
                return;
            }

            ++previousCol;
        }

        --scanLeftX;
        ++scanBottomY;
        scanLength += 2;
        ++scanRightX;
        --scanTopY;
        ++scanIteration;
    }

    previousRow = currentRow;
    previousCol = currentCol;
}

int loadLevelByIndex(int level)
{
    std::cout << "Loading " << g_selectedFilePath << ", level " << level << std::endl;

    if (!fileAccessEnabled)
        return 0;

    FileLike* file = openAndPrepareFileFromSlot(g_selectedFilePath, "r");
    if (!file)
    {
        showMessage(g_selectedFilePath, "Cannot open file: ");
        resetLevelStateMemory();
        fileAccessEnabled = false;
        return 0;
    }

    resetAndSeekFile(file, 0);

    std::array<char, 0x4F + 1> outBuf{};
    readLineToBuffer(file, outBuf.data(), 0x4F);

    levelCount = parseSignedDecimalString(outBuf.data());
    cout << "Level count " << levelCount << endl;

    isLevelCompleted      = false;
    g_hasPendingModal     = false;
    g_levelJustLoadedFlag = true;
    hasLevelTransition    = false;
    remainingLives        = 3;
    selectedObjectIndex   = 0xFFFF;

    if (level < 1)
        level = 1;
    else if (level > levelCount)
        level = levelCount;

    std::array<char, 0x38C> levelBlock{};

    for (int i = 0; i < level; ++i)
    {
        if (loadLevelMetaData(file, levelBlock.data()) < 0)
        {
            cleanFile(file);
            return 0;
        }
    }

    g_activeSpawnerCount = 0;

    cout << "PASSWORD => " << g_levelPassword << endl;
    cout << "VICTORY => " << g_levelVictoryText << endl;
    cout << "HINT => " << g_levelHintText << endl;
    for (int row = 0; row < GRID_ROWS; ++row)
    {
        const char* linePtr = levelBlock.data() + (6 * 0x23 - 5) + row * 0x23;

        cout << "MAP LINE => [";
        cout.write(linePtr, 30);
        cout << "]" << endl;

        loadLevelRow(row, linePtr);
    }

    postLoadLevel();

    cleanFile(file);
    return 1;
}

void resetLevelStateMemory()
{
    g_activeSpawnerCount = 0;

    for (int row = 0; row < GRID_ROWS; ++row)
    {
        for (int col = 0; col < GRID_COLS; ++col)
        {
            g_gameState.tileMap[row][col] = EntityType::SQUARE_WALL;
        }
    }

    for (int row = 1; row < GRID_ROWS - 1; ++row)
    {
        for (int col = 1; col < GRID_COLS - 1; ++col)
        {
            g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;
        }
    }

    for (int row = 0; row < GRID_ROWS; ++row)
    {
        for (int col = 0; col < GRID_COLS; ++col)
        {
            g_gameState.leftEntityMap[row][col]   = -1;
            g_gameState.rightEntityMap[row][col]  = -1;
            g_gameState.topEntityMap[row][col]    = -1;
            g_gameState.bottomEntityMap[row][col] = -1;

            g_gameState.entityToLeft[row][col] = -1;
            g_gameState.entityToRight[row][col] = -1;
            g_gameState.entityBelow[row][col] = -1;
            g_gameState.entityAbove[row][col] = -1;
        }
    }

    exitCoordLeft  = EntityType::SQUARE_WALL;
    exitCoordRight = EntityType::SQUARE_WALL;
    exitState      = EntityType::BOTTOM_RIGHT_ROUND_WALL;

    currentRow = 3;
    currentCol = 3;

    kyeRow = 3;
    kyeCol = 3;

    selectedTileValue   = EntityType::KYE_LOCATION;
    selectionState      = EntityType::DIAMOND;
    selectedObjectIndex = -1;
}

int decodeTile(uint8_t* outType, EntityType* typeCode, char inputChar)
{
    for (const auto& e : ENTITY_MAPPINGS)
    {
        if (e.asciiChar == inputChar)
        {
            *outType = static_cast<uint8_t>(e.category);
            *typeCode = e.param;
            return 1;
        }
    }

    return 0;
}

std::int16_t addEntity(EntityType entityCode, std::int16_t row, std::int16_t col)
{
    const std::uint16_t index = g_activeSpawnerCount;

    if (g_activeSpawnerCount >= 0x258)
        return -1;
    g_gameState.entities[index].entityType = entityCode;
    g_gameState.entities[index].row = row;
    g_gameState.entities[index].col = col;
    g_gameState.entities[index].animFrame = 0;
    g_gameState.tileMap[row][col] = (EntityType)index;

    ++g_activeSpawnerCount;
    return index;
}

uint32_t pseudoRandomUpdate(uint32_t add)
{
    uint32_t seed =
        (static_cast<uint32_t>(randomSeedHigh) << 16) |
        randomSeedLow;

    seed = seed * 0x015A4E35u + 1u;
    seed += add;

    randomSeedHigh = static_cast<uint16_t>(seed >> 16);
    randomSeedLow  = static_cast<uint16_t>(seed);

    return seed;   // ← IMPORTANT
}

// void loadLevelRow(int row, const char* lineData)
// {
//     if (row >= GRID_ROWS) {
//         cout << "Row exeeding..." << row << endl;
//         return;
//     }
//     const char* linePtr = lineData;

//     for (int col = 0; col < GRID_COLS; ++col)
//         g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;
    
//     int col = 0;
//     while (*linePtr && col < GRID_COLS)
//     {
//         char inputChar = *linePtr;
//         EntityType entityCode = EntityType::EMPTY_CELL;
//         uint8_t entityType = 0;
//         int ok = decodeTile(&entityType, &entityCode, inputChar);

//         if (ok)
//         {
//             switch (entityType)
//             {
//                 case 0: // empty
//                     // cout << "Empty (" << row << ";" << col << ") =" << tileCode << endl;
//                     g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;
//                     break;

//                 case 1:
//                 {
//                     cout << "FIXED TILE (" << row << ";" << col << ") char="
//                         << inputChar << " code=" << (int)entityCode << endl;

//                     g_gameState.tileMap[row][col] = entityCode;
//                     break;
//                 }
//                 case 2: // entity
//                 {
//                     cout << "Mobile entity (" << row << ";" << col << ") =" << (int)entityCode << endl;
//                     int changeIndex =
//                         addEntity(entityCode, row, col);

//                     if (changeIndex >= 0)
//                     {
//                         uint16_t rnd = pseudoRandomUpdate(0x8000);

//                         uint16_t anim = divide64_unsigned(rnd);

//                         g_gameState.entities[changeIndex].animFrame = anim;
//                     }
//                     break;
//                 }

//                 case 3: // player spawn
//                 {
//                     g_gameState.tileMap[row][col] = EntityType::KYE_LOCATION;
//                     cout << "setting Kye (" << row << ";" << col << ") =" << (int)entityCode << endl;
//                     currentRow = row;
//                     currentCol = col;   

//                     kyeRow = row;
//                     kyeCol = col;
//                     break;
//                 }
//             }
//         }

//         ++linePtr;
//         ++col;
//     }
// }

void loadLevelRow(int row, const char* lineData)
{
    if (row >= GRID_ROWS)
        return;

    const char* linePtr = lineData;

    for (int col = 0; col < GRID_COLS; ++col)
        g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;

    int col = 0;

    while (*linePtr && col < GRID_COLS)
    {
        char inputChar = *linePtr;
        EntityType entityCode = EntityType::EMPTY_CELL;
        uint8_t entityType = 0;

        int ok = decodeTile(&entityType, &entityCode, inputChar);

        if (ok)
        {
            switch (entityType)
            {
                case 0:
                    g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;
                    break;

                case 1:
                    g_gameState.tileMap[row][col] = entityCode;
                    break;

                case 2:
                {
                    int changeIndex = addEntity(entityCode, row, col);

                    if (changeIndex >= 0)
                    {
                        uint16_t rnd = pseudoRandomUpdate(0x8000);
                        uint16_t anim = divide64_unsigned(rnd);
                        g_gameState.entities[changeIndex].animFrame = anim;
                    }
                    break;
                }

                case 3:
                {
                    g_gameState.tileMap[row][col] = EntityType::KYE_LOCATION;
                    currentRow = row;
                    currentCol = col;
                    kyeRow = row;
                    kyeCol = col;
                    break;
                }
            }
        }

        ++linePtr;
        ++col;
    }
}

static inline void invalidateCell(EntityType& cell)
{
    if (cell == EntityType::EMPTY)
        return;

    if (isFixedTile(cell))
        return;

    if (cell >= EntityType::EMPTY_CELL)
    {
        markEntryInactive((int)cell);
    }

    cell = EntityType::EMPTY;
}

int postLoadLevel()
{
    int foundDiamond = 0;

    for (int row = 0; row < GRID_ROWS && !foundDiamond; ++row)
    {
        for (int col = 0; col < GRID_COLS; ++col)
        {
            if (g_gameState.tileMap[row][col] == EntityType::DIAMOND)
            {
                foundDiamond = 1;
                break;
            }
        }
    }

    if (foundDiamond == 0)
    {
        selectionState = EntityType::DIAMOND;
    }

    int foundKye = 0;
    int foundKyeRow = 0;
    int foundKyeCol = 0;

    for (int row = 0; row < GRID_ROWS && !foundKye; ++row)
    {
        for (int col = 0; col < GRID_COLS; ++col)
        {
            if (g_gameState.tileMap[row][col] == EntityType::KYE_LOCATION)
            {
                foundKye = 1;
                foundKyeRow = row;
                foundKyeCol = col;
                break;
            }
        }
    }

    if (foundKye == 0 || foundKyeRow != currentRow || foundKyeCol != currentCol)
    {
        currentRow = 3;
        currentCol = 3;
        kyeCol = 3;
        kyeRow = 3;
        selectedTileValue = EntityType::KYE_LOCATION;
    }

    for (int row = 0; row < GRID_ROWS; ++row)
    {
        EntityType& cell = g_gameState.tileMap[row][0];

        if (!(cell >= EntityType::TOP_RIGHT_ROUND_WALL &&
              cell <= EntityType::BOTTOM_LEFT_ROUND_WALL))
        {
            if (static_cast<int16_t>(cell) >= 0)
            {
                markEntryInactive(static_cast<int>(cell));
            }

            cell = EntityType::SQUARE_WALL;
        }
    }

    for (int col = 0; col < GRID_COLS; ++col)
    {
        EntityType& cell = g_gameState.tileMap[0][col];

        if (!(cell >= EntityType::TOP_RIGHT_ROUND_WALL &&
              cell <= EntityType::BOTTOM_LEFT_ROUND_WALL))
        {
            if (static_cast<int16_t>(cell) >= 0)
            {
                markEntryInactive(static_cast<int>(cell));
            }

            cell = EntityType::SQUARE_WALL;
        }
    }

    finalizeLevelVisuals();
    return 0;
}

int destroyEntityIfFallsIntoLava(int index, int newRow, int newCol)
{
    int tile = (int)g_gameState.tileMap[newRow][newCol];

    if (tile < 0 || tile >= 256)
        return 0;

    auto& target = g_gameState.entities[tile];

    if (target.entityType != EntityType::Lava)
        return 0;

    auto& src = g_gameState.entities[index];

    int oldRow = src.row;
    int oldCol = src.col;

    target.entityType = EntityType::Lava2;
    target.animFrame = 0;

    moveAndRedrawEntity(tile, newRow, newCol);

    drawRectangleFromGrid(oldRow, oldCol);

    markEntryInactive(index);

    return 1;
}

void markEntryInactive(int index) {
    if (index >= 0 && index < MAX_CHANGES) {
        g_gameState.entities[index].entityType = EntityType::DESTROYED;
    }
}

void finalizeLevelVisuals()
{
    int i = 0;

    while (i < g_activeSpawnerCount)
    {
        if (g_gameState.entities[i].entityType == EntityType::EMPTY_CELL)
        {
            if (g_activeSpawnerCount >= 1)
            {
                --g_activeSpawnerCount;

                for (int j = i; j < g_activeSpawnerCount; ++j)
                {
                    g_gameState.entities[j] = g_gameState.entities[j + 1];
                }

                for (int row = 1; row < GRID_ROWS - 1; ++row) {
                    for (int col = 1; col < GRID_COLS - 1; ++col)
                    {
                        int16_t* tables[] = {
                            &g_gameState.entityAbove[row][col],
                            &g_gameState.entityBelow[row][col],
                            &g_gameState.entityToLeft[row][col],
                            &g_gameState.entityToRight[row][col],
                            &g_gameState.leftEntityMap[row][col],
                            &g_gameState.rightEntityMap[row][col],
                            &g_gameState.bottomEntityMap[row][col],
                            &g_gameState.auxBottomRightEntityMap[row][col],
                            &g_gameState.auxTopRightEntityMap[row][col]
                        };

                        for (auto* cell : tables)
                        {
                            if (*cell > i)
                                --(*cell);
                        }
                    }
                }
                continue;
            }
        }

        ++i;
    }
}

static inline int sgn(int v) noexcept { return (v > 0) - (v < 0); }

void handleKyeMovement()
{
    if (hasLevelTransition != 0)
    {
        runTileSparkleEffect(2);

        // ASM: push srcCol ; push srcRow ; call drawRectangleFromGrid
        // => si ta signature est drawRectangleFromGrid(row, col), ça correspond à (srcRow, srcCol) legacy.
        drawRectangleFromGrid(currentRow, currentCol);

        // Restore cursor position
        currentRow = previousRow;
        currentCol = previousCol;

        if (0 <= currentRow && currentRow < GRID_ROWS && 0 <= currentCol && currentCol < GRID_COLS)
            g_gameState.tileMap[currentRow][currentCol] = EntityType::KYE_LOCATION;

        runTileSparkleEffect(1);
        hasLevelTransition = 0;
        return;
    }

    if (previousRow == currentRow && previousCol == currentCol)
        return;

    const int dLegacyRow = previousRow - currentRow;
    const int dLegacyCol = previousCol - currentCol;

    const int stepLegacyRow = sgn(dLegacyRow); // ax dans ASM
    const int stepLegacyCol = sgn(dLegacyCol); // dx dans ASM
    processKyeCollision(stepLegacyRow, stepLegacyCol);
}

void handlePointClick(int x, int y)
{
    if (!isPointInRect(x, y))
        return;

    if (g_interactionMode != GameInteractionMode::PLAY_MODE)
        return;

    initializeRendererIfNeeded();
    advanceToNextLevelOrBlock();
}

int handleKyeMarkerBlock(int rowIndex, int colIndex)
{
    if (isPendingKyeMarkerDraw &&
        pendingRow == rowIndex &&
        pendingCol == colIndex)
    {
        return 0;
    }

    maybeDrawPendingRectangle();

    pendingRow = rowIndex;
    pendingCol = colIndex;

    if (std::abs(rowIndex - currentRow) <= 1 &&
        std::abs(colIndex - currentCol) <= 1)
    {
        return 0;
    }

    if (g_gameState.tileMap[pendingRow][pendingCol] == EntityType::EMPTY_CELL)
    {
        // drawPendingKyeMarker();
        isPendingKyeMarkerDraw = 1;
    }

    return 0;
}

int hasSpecialCell()
{
    for (int row = 0; row < GRID_ROWS; ++row)
    {
        for (int col = 0; col < GRID_COLS; ++col)
        {
            if (g_gameState.tileMap[row][col] == EntityType::KYE_LOCATION)
            {
                return 1;
            }
        }
    }

    return 0;
}

bool canPlaceEntityAtPosition(uint16_t pos, uint16_t level, uint16_t value, uint8_t tie)
{
    uint16_t si = 0;

    if (level == 0) {
        si = value;
        if (value >= 0x003B) {
            uint16_t tmp = static_cast<uint16_t>(pos + 0x0046);
            if ((tmp & 0x0003) == 0) {
                si = static_cast<uint16_t>(si - 1);
            }
        }
        level = 0;
        while (g_table10C8[level] <= si) {
            level = static_cast<uint16_t>(level + 1);
        }
    } else {
        if (level < 3) {
            value = static_cast<uint16_t>(value - 1);
        } else {
            uint16_t tmp = static_cast<uint16_t>(pos + 0x0046);
            if ((tmp & 0x0003) != 0) {
                value = static_cast<uint16_t>(value - 1);
            }
        }
        value = static_cast<uint16_t>(value + g_table10C8[level - 1]);
    }

    if (level < 4) {
        return false;
    }

    if (level != 4) {
        if (level > 10) {
            return false;
        }
        if (level != 10) {
            return true;
        }
    }

    uint16_t cx = 0;
    if (pos > 0x0010 && level == 4) {
        cx = static_cast<uint16_t>(g_table10C6[level] + 7);
    } else {
        cx = g_table10C8[level];
    }
    {
        uint16_t tmp = static_cast<uint16_t>(pos + 0x07B2);
        if ((tmp & 0x0003) != 0) {
            cx = static_cast<uint16_t>(cx - 1);
        }
    }
    uint16_t bx = static_cast<uint16_t>(((pos + 1) >> 2) + cx);
    uint32_t ax32 = static_cast<uint32_t>(0x016D) * static_cast<uint32_t>(pos);
    ax32 += static_cast<uint32_t>(bx);
    ax32 += 4u;

    uint16_t rem = static_cast<uint16_t>(ax32 % 7u);
    cx = static_cast<uint16_t>(cx - rem);

    if (level == 4) {
        if (value > cx) return true;
        if (value < cx) return false;
        return (tie >= 2);
    } else {
        if (value < cx) return true;
        if (value > cx) return false;
        return (tie <= 1);
    }
}

int initOrHandleEvent(int exitCode)
{
    handleEngineEvent(exitCode, 0, 0);
    return 0;
}

void handleEngineEvent(uint16_t exitCode,
                       uint16_t skipExit,
                       uint16_t skipPreDrain)
{
    if (skipPreDrain == 0)
    {
        while (g_pendingEventCount != 0)
        {
            g_pendingEventCount--;
            g_callbackTable[g_pendingEventCount]();
        }

        processCallbackQueueFromEngineEvent();
        g_validateInternalBufferCallback();
    }

    if (skipExit == 0)
    {
        if (skipPreDrain == 0)
        {
            configureFileMode();
            invokeExternalCallback();
        }

        cleanupAndTerminate(exitCode);
    }
}

void cleanupAndTerminate(int exitCode)
{
    if (g_renderer != nullptr) {
        SDL_DestroyRenderer(g_renderer);
        g_renderer = nullptr;
    }

    if (g_window != nullptr) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }

    SDL_Quit();
    std::exit(exitCode & 0xFF);
}

void processCallbackQueue(CallbackQueueEntry* begin,
                          CallbackQueueEntry* end)
{
    while (true)
    {
        CallbackQueueEntry* best = nullptr;
        uint8_t bestPriority = 0;

        for (CallbackQueueEntry* it = begin; it != end; ++it)
        {
            if (it->state == 0xFF)
                continue;

            if (it->priority >= bestPriority)
            {
                bestPriority = it->priority;
                best = it;
            }
        }

        if (!best)
            return;

        best->state = 0xFF;

        CallbackFn fn =
            reinterpret_cast<CallbackFn>(
                (uint32_t(best->segment) << 16) |
                best->offset
            );

        fn();
    }
}

void runLegacyCallbackQueue()
{
    while (true)
    {
        CallbackEntry* best = nullptr;
        uint8_t bestPriority = 0xFF;

        for (auto& entry : g_callbackQueue)
        {
            if (entry.state == 0xFF)
                continue;
            if (entry.priority <= bestPriority)
            {
                bestPriority = entry.priority;
                best = &entry;
            }
        }

        if (!best)
            return;
        uint8_t state = best->state;
        best->state = 0xFF;
        if (!best->callback)
            return;

        best->callback();
    }
}

void bufferCopyCallback(char** bufferPtrRef, const char* data, int length) {
    std::memcpy(*bufferPtrRef, data, length);
    *bufferPtrRef += length;
    **bufferPtrRef = '\0';
}


int prepareAndCallProcessMainLoop(char* outputBuffer, int stringId, int value)
{
    const char* format = nullptr;

    switch (stringId)
    {
        case 0x4DC: format = "Diamonds: %d"; break;
        case 0x4ED: format = "Editing level"; break;
        case 0x4B8: format = "Level %d"; break;
        case 0x4C6: format = "Diamonds left: %d"; break;
        default:    format = "%d"; break;
    }

    std::snprintf(outputBuffer, 0x6C, format, value);
    return 0;
}

void handleMagnetVertical(int entityIndex)
{
    auto& s = g_gameState;

    const int row = s.entities[entityIndex].row;
    const int col = s.entities[entityIndex].col;

    if (s.tileMap[row + 2][col] == EntityType::KYE_LOCATION &&
        findEntityAt(row + 1, col) == -1)
    {
        moveAndRedrawEntity(entityIndex, row + 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (s.tileMap[row - 2][col] == EntityType::KYE_LOCATION &&
        findEntityAt(row - 1, col) == -1)
    {
        moveAndRedrawEntity(entityIndex, row - 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    {
        const int below = findEntityAt(row + 2, col);
        if (below >= 0 &&
            s.entities[below].entityType == EntityType::MAGNET_HORIZONTAL &&
            findEntityAt(row + 1, col) == -1)
        {
            moveAndRedrawEntity(below, row + 1, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    {
        const int above = findEntityAt(row - 2, col);
        if (above >= 0 &&
            s.entities[above].entityType == EntityType::MAGNET_HORIZONTAL &&
            findEntityAt(row - 1, col) == -1)
        {
            moveAndRedrawEntity(above, row - 1, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleMagnetHorizontal(int entityIndex)
{
    auto& s = g_gameState;

    const int row = s.entities[entityIndex].row;
    const int col = s.entities[entityIndex].col;

    if (s.tileMap[row][col + 2] == EntityType::KYE_LOCATION &&
        findEntityAt(row, col + 1) == -1)
    {
        moveAndRedrawEntity(entityIndex, row, col + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (s.tileMap[row][col - 2] == EntityType::KYE_LOCATION &&
        findEntityAt(row, col - 1) == -1)
    {
        moveAndRedrawEntity(entityIndex, row, col - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    {
        const int right = findEntityAt(row, col + 2);
        if (right >= 0 &&
            s.entities[right].entityType == EntityType::MAGNET_VERTICAL &&
            findEntityAt(row, col + 1) == -1)
        {
            moveAndRedrawEntity(right, row, col + 1);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    {
        const int left = findEntityAt(row, col - 2);
        if (left >= 0 &&
            s.entities[left].entityType == EntityType::MAGNET_VERTICAL &&
            findEntityAt(row, col - 1) == -1)
        {
            moveAndRedrawEntity(left, row, col - 1);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void adjustMonsterTargetTowardPosition(int entityIndex,
                             int srcRow,
                             int srcCol,
                             int* targetRow,
                             int* targetCol)
{
    const int curRow = g_gameState.entities[entityIndex].row;
    const int curCol = g_gameState.entities[entityIndex].col;

    if (targetRow) *targetRow = curRow;
    if (targetCol) *targetCol = curCol;

    int dy = 0;
    if (srcRow > curRow) dy = 1;
    else if (srcRow < curRow) dy = -1;

    int dx = 0;
    if (srcCol > curCol) dx = 1;
    else if (srcCol < curCol) dx = -1;

    if (dy != 0 && dx != 0)
    {
        const bool verticalFree =
            g_gameState.tileMap[curRow + dy][curCol] == EntityType::EMPTY_CELL;

        const bool horizontalFree =
            g_gameState.tileMap[curRow][curCol + dx] == EntityType::EMPTY_CELL;

        if (verticalFree && horizontalFree)
        {
            if (std::abs(dy) > std::abs(dx))
                dx = 0;
            else
                dy = 0;
        }
        else if (verticalFree)
        {
            dx = 0;
        }
        else if (horizontalFree)
        {
            dy = 0;
        }
        else
        {
            dx = 0;
            dy = 0;
        }
    }
    else
    {
        if (dy != 0 &&
            g_gameState.tileMap[curRow + dy][curCol] != EntityType::EMPTY_CELL)
        {
            dy = 0;
        }

        if (dx != 0 &&
            g_gameState.tileMap[curRow][curCol + dx] != EntityType::EMPTY_CELL)
        {
            dx = 0;
        }
    }

    const int candidateRow = curRow + dy;
    const int candidateCol = curCol + dx;

    if (g_gameState.tileMap[candidateRow][candidateCol] == EntityType::EMPTY_CELL)
    {
        if (targetRow) *targetRow = candidateRow;
        if (targetCol) *targetCol = candidateCol;
    }
}

static inline std::int16_t tileRandomCoord16()
{
  const uint16_t r = pseudoRandomUpdate();
  return static_cast<std::int16_t>((r >> 11) & 0x000F);
}

int handleGameClick(int x, int y)
{
    if (!isPointInRect(x, y))
    {
        return 0;
    }

    const int interactionMode = static_cast<int>(g_interactionMode);

    if (interactionMode == 0)
    {
        tickLevelFlow();

        const int rowIndex = y / cellHeight;
        const int colIndex = x / cellWidth;

        handleKyeMarkerBlock(rowIndex, colIndex);

        previousRow = rowIndex;
        previousCol = colIndex;
        g_hasPendingModal = 1;
        isLeftMouseDragActive = 1;
        return 0;
    }

    if (interactionMode == 1)
    {
        const int rowIndex = y / cellHeight;
        const int colIndex = x / cellWidth;

        handleClickOnGridCell(rowIndex, colIndex);
        initializeRendererIfNeeded();
        updateGridCell(rowIndex, colIndex);
        renderLivesAndLevelInfo();
        advanceToNextLevelOrBlock();
        return 0;
    }

    return 0;
}

inline int countDiamondsInGrid()
{
    int count = 0;
    for (int row = 0; row < GRID_ROWS ; ++row) {
        for (int col = 0; col < GRID_COLS ; ++col) {
            if (g_gameState.tileMap[row][col] == EntityType::DIAMOND) {
                ++count;
            }
        }
    }
    return count;
}

int renderLivesAndLevelInfo()
{
    const int hudX = g_gridRectPx.x;
    const int hudY = g_gridRectPx.y + g_gridRectPx.h + 4;

    SDL_FRect hudRect{
        static_cast<float>(hudX),
        static_cast<float>(hudY),
        static_cast<float>(g_gridRectPx.w),
        20.0f
    };

    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(g_renderer, &hudRect);

    int xOffsetLives = 0;

    for (int i = 0; i < remainingLives; ++i)
    {
        SDL_FRect dst{
            static_cast<float>(hudX + xOffsetLives + 1),
            static_cast<float>(hudY + 1),
            16.0f,
            16.0f
        };

        SDL_FRect src{0.0f, 0.0f, 16.0f, 16.0f};

        SDL_RenderTexture(g_renderer, g_sheetKye.tex, &src, &dst);
        xOffsetLives += 0x14;
    }

    char buffer[128];

    snprintf(buffer, sizeof(buffer), "Level: %d", levelIndex);
    drawTextAt(hudX + 70, hudY + 2, buffer, strlen(buffer));

    const int diamondCount = countDiamondsInGrid();
    snprintf(buffer, sizeof(buffer), "Diamonds left: %d", diamondCount);
    drawTextAt(hudX + 160, hudY + 2, buffer, strlen(buffer));

    int separatorX = hudX + 300;
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);

    SDL_RenderLine(
        g_renderer,
        (float)separatorX,
        (float)hudY,
        (float)separatorX,
        (float)(hudY + 20)
    );

    if (!g_levelHintText.empty())
    {
        drawTextAt(hudX + 320, hudY + 2,
                   g_levelHintText.c_str(),
                   g_levelHintText.length());
    }

    return 1;
}

void setStatusText(const std::string& text) {
    clearStatusLine(text.c_str());
}

int handleClickOnGridCell(int row, int col)
{
    cout << "handleClickOnGridCell (" << row << ";" << col << ")" << endl;
    if (row < 0 || row >= GRID_ROWS || col < 0 || col >= GRID_COLS)
    {
        return 0;
    }

    if (g_gameState.tileMap[row][col] == EntityType::EMPTY_CELL)
    {
        const EditorEntry& entry = g_editorEntries[g_currentNameIndex];

        if (entry.enabled != 0)
        {
            executeCurrentEntryAction(
                entry.actionType,
                static_cast<EntityType>(entry.tileId),
                static_cast<std::int16_t>(row),
                static_cast<std::int16_t>(col)
            );
            return 0;
        }
    }

    handleStandardCellClick(row, col);
    return 0;
}

int handleStandardCellClick(int row, int col)
{
    EntityType cell = g_gameState.tileMap[row][col];

    if (static_cast<int16_t>(cell) > 0)
    {
        markEntryInactive(static_cast<int>(cell));
        g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;
    }
    else
    {
        g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;
    }

    finalizeLevelVisuals();

    if (cell == EntityType::KYE_LOCATION)
    {
        handleSpecialSentinelClick();
    }

    return 0;
}

void handleSpecialSentinelClick()
{
    if (hasSpecialCell())
        specialCellStateFlag = 0;
    else
        specialCellStateFlag = 1;

    SDL_FRect rect{};
    SDL_GetWindowSize(g_windowHandle2, (int*)&rect.w, (int*)&rect.h);

    SDL_RaiseWindow(g_windowHandle2);

    // force redraw
    SDL_Event ev{};
    ev.type = SDL_EVENT_WINDOW_EXPOSED;
    SDL_PushEvent(&ev);
}


int executeCurrentEntryAction(int16_t actionType,
                              EntityType type,
                              int16_t row,
                              int16_t col)
{
    int16_t dx;

    if (row <= 0 || row >= 0x1D || col <= 0 || col >= 0x13)
        dx = 1;
    else
        dx = 0;

    switch (actionType)
    {
        case 1:
        {
            if (type >= EntityType::TOP_RIGHT_ROUND_WALL && type <= EntityType::BOTTOM_LEFT_ROUND_WALL)
            {
                g_gameState.tileMap[row][col] = type;
                return 0;
            }

            if (dx == 0)
            {
                g_gameState.tileMap[row][col] = type;
                return 0;
            }

            break;
        }

        case 2:
        {
            if (dx == 0)
            {
                addEntity(type, row, col);
                return 0;
            }

            break;
        }

        case 3:
        {
            if (dx == 0)
            {
                g_gameState.tileMap[row][col] = EntityType::KYE_LOCATION;

                currentRow = row;
                currentCol = col;

                handleSpecialSentinelClick();
            }

            break;
        }
    }

    return 0;
}

void updateGridCell(int row, int col)
{
    if (!g_renderer)
        return;

    int16_t tile = (int16_t)g_gameState.tileMap[row][col];

    if (tile >= 0)
    {
        renderEntity(tile);
        return;
    }
    else if (tile == (int)EntityType::KYE_LOCATION)
    {
        SDL_FRect dst{
            float(gridOriginX + col*cellWidth),
            float(gridOriginY + row*cellHeight),
            float(cellWidth),
            float(cellHeight)
        };
        SDL_FRect src{
            0.0f,
            0.0f,
            16.0f,
            16.0f
        };
        SDL_RenderTexture(g_renderer, g_sheetKye.tex, &src, &dst);
    }
    else if (tile == (int)EntityType::EMPTY_CELL)
    {
        drawRectangleFromGrid(row, col); // case vide
    }
    else
    {
        renderStaticObjects(row, col, (EntityType)tile);
    }
}

void cancelPendingInteraction()
{
    if (g_isMouseCaptured)
    {
        SDL_CaptureMouse(false);
        g_isMouseCaptured = 0;
    }

    if (g_interactionMode == GameInteractionMode::PLAY_MODE)
    {
        g_hasPendingModal = 0;
        isLeftMouseDragActive = 0;

        maybeDrawPendingRectangle();
    }
}

int showToolboxWindowAndRefreshHUD()
{
    if (!toolboxCreated)
    {
        g_toolboxWindow = SDL_CreateWindow(
            "Kye-Tools",
            90,
            50,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_UTILITY
        );

        if (!g_toolboxWindow)
            return 0;

        g_toolboxRenderer = SDL_CreateRenderer(
            g_toolboxWindow,
            nullptr
        );

        if (!g_toolboxRenderer)
            return 0;

        toolboxCreated = true;
    }
    handleSpecialSentinelClick();
    SDL_ShowWindow(g_toolboxWindow);
    initializeRendererIfNeeded();
    renderLivesAndLevelInfo();
    advanceToNextLevelOrBlock();
    return 1;
}

int hideSecondaryWindow()
{
    if (g_secondaryWindow)
        SDL_HideWindow(g_secondaryWindow);

    return 1;
}

void appendSuffixToPath(char* outPath, int outCapacity)
{
    if (!outPath || outCapacity <= 0) return;

    // longueur actuelle de la chaîne
    size_t len = std::strlen(outPath);
    if (len == 0) return;

    // si la chaîne se termine déjà par '\' ou ':', ne rien faire
    if (outPath[len - 1] == '\\' || outPath[len - 1] == ':') {
        return;
    }

    // copier le suffixe
    size_t suffixLen = std::strlen(g_defaultOpenSuffix);

    // vérifier que ça rentre
    if (len + suffixLen >= static_cast<size_t>(outCapacity)) {
        suffixLen = static_cast<size_t>(outCapacity) - len - 1; // garder place pour '\0'
    }

    if (suffixLen > 0) {
        std::memcpy(outPath + len, g_defaultOpenSuffix, suffixLen);
        outPath[len + suffixLen] = '\0';
    }
}

bool showOpenFileDialogAndBuildFullPath(char* outFullPath)
{
    const char* file = tinyfd_openFileDialog(
        "Open Kye Level",
        "",
        0,
        nullptr,
        nullptr,
        0
    );

    if (!file)
        return false;

    std::strcpy(outFullPath, file);

    appendSuffixToPath(outFullPath, 0x50C);

    return true;
}

bool handleMainMenuCommand(MenuCommand cmd)
{
    switch (cmd)
    {

        case MenuCommand::Quit:
        {
            SDL_Event e{};
            e.type = SDL_EVENT_QUIT;
            SDL_PushEvent(&e);
            return true;
        }

        case MenuCommand::NewGame:
        {
            levelIndex = 1;
            loadLevelByIndex(levelIndex);
            isLeftMouseDragActive = 0;
            clearStatusLine(g_statusLineBuffer);
            return true;
        }

        case MenuCommand::Restart:
        {
            loadLevelByIndex(levelIndex);
            isLeftMouseDragActive = 0;
            clearStatusLine(g_statusLineBuffer);
            return true;
        }

        case MenuCommand::GotoLevel:
        {
            showGotoLevelDialog();
            return true;
        }

        case MenuCommand::OpenFile:
        {
            showOpenFileDialogAndBuildFullPath(g_selectedFilePath);
            loadLevelByIndex(levelIndex);
            isLeftMouseDragActive = 0;
            return true;
        }

        case MenuCommand::EnterEditMode:
        {
            g_interactionMode = GameInteractionMode::EDIT_MODE;
            g_timerActive = false;
            showToolboxWindowAndRefreshHUD();
            return true;
        }

        case MenuCommand::ExitEditMode:
        {
            g_interactionMode = GameInteractionMode::PLAY_MODE;
            g_timerActive = true;
            hideSecondaryWindow();
            generateFileFromMappedData();
            loadLevelByIndex(levelIndex);
            return true;
        }

        case MenuCommand::Help:
        {
            showHelp();
            return true;
        }

        case MenuCommand::About:
        {
            showAboutDialog();
            return true;
        }

        case MenuCommand::What:
        {
            showWhatDialog();
            return true;
        }

        case MenuCommand::ToggleOption:
        {
            g_optionToggleFlag = !g_optionToggleFlag;
            return true;
        }
    }

    return false;
}

void showAboutDialog()
{
    const char* message =
        "Kye\n"
        "Original game by Colin Garbutt (1992)\n\n"
        "Modern SDL port.";

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "About Kye",
        message,
        g_window
    );
}

void showHelp()
{
    const char* message =
        "Kye Help\n\n"
        "Goal: Collect all diamonds.\n\n"
        "Controls:\n"
        "Arrow keys - move\n"
        "Avoid monsters and traps.";

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Kye Help",
        message,
        g_window
    );
}

void updateNextLevelMenuItem()
{
    g_nextLevelEnabled = (levelCount > 1);
}

void handlePendingInteractionClick(int x, int y)
{
    int localX = x;
    int localY = y;

    if (!isPointInRect(localX, localY)) {
        return;
    }

    if (g_interactionMode != GameInteractionMode::PLAY_MODE) {
        return;
    }

    if (matchedEntryCount != 0) {
        matchedEntryCount = 0;
        maybeDrawPendingRectangle();
        return;
    }

    int rowIndex = localX / cellHeight;
    int colIndex = localY / cellWidth;

    handleKyeMarkerBlock(rowIndex, colIndex);

    previousRow = rowIndex;
    previousCol = colIndex;
    g_hasPendingModal = 1;
    matchedEntryCount = 1;
}

void handlePendingInteractionFinalize(int x, int y)
{
    if (!isPointInRect(x, y))
    {
        SDL_SetCursor(g_cursorArrow);
        return;
    }

    if (g_interactionMode != GameInteractionMode::PLAY_MODE)
    {
        SDL_SetCursor(g_cursorArrow);
        return;
    }

    SDL_SetCursor(g_cursorArrow);

    const int rowIndex = y / cellHeight;
    const int colIndex = x / cellWidth;

    if (isLeftMouseDragActive == 0)
    {
        return;
    }

    handleKyeMarkerBlock(rowIndex, colIndex);

    previousRow = rowIndex;
    previousCol = colIndex;
    g_hasPendingModal = 1;
}

void animateLava()
{
    if (frameCounter % 5 != 0)
        return;

    for (std::int16_t i = 0; i < g_activeSpawnerCount && i < g_gameState.entities.size(); ++i)
    {
        EntityInfo& entity = g_gameState.entities[i];

        const int dstY = entity.row * cellHeight;
        const int dstX = entity.col * cellWidth;

        const int srcX = 0x40 + (entity.animFrame << 4);

        if (entity.entityType == EntityType::Lava) // 0x1F
        {
            g_blockSheet.blit16(
                dstX,
                dstY,
                srcX,
                0x80
            );

            entity.animFrame = (entity.animFrame + 1) % 4;
        }
        else if (entity.entityType == EntityType::Lava2) // 0x20
        {
            g_blockSheet.blit16(
                dstX,
                dstY,
                srcX,
                0x90
            );

            if (entity.animFrame == 3)
                entity.entityType = EntityType::Lava;

            entity.animFrame = (entity.animFrame + 1) % 4;
        }
    }
}

void drainPendingEvents() {}

void animateDiamonds()
{
    if (frameCounter % 10 != 0)
        return;

    for (int row = 0; row < GRID_ROWS; ++row)
    {
        for (int col = 0; col < GRID_COLS; ++col)
        {
            if (g_gameState.tileMap[row][col] != EntityType::DIAMOND)
                continue;

            int dstX = gridOriginX + col * cellWidth;
            int dstY = gridOriginY + row * cellHeight;

            int srcX = 0xC0;
            int srcY = ((rand() % 2) == 1) ? 0x10 : 0x00;
            SDL_FRect src{
                (float)srcX,
                (float)srcY,
                16.0f,
                16.0f
            };

            SDL_FRect dst{
                (float)dstX,
                (float)dstY,
                16.0f,
                16.0f
            };

            SDL_RenderTexture(g_renderer, g_sheetStatics.tex, &src, &dst);
        }
    }
}

void animateOneWayTilesEvery4Frames()
{
    if (frameCounter % 4 != 0)
        return;

    int spriteOffsetX;

    if (g_oneWayAnimPhase)
    {
        spriteOffsetX = 0;
        g_oneWayAnimPhase = 0;
    }
    else
    {
        spriteOffsetX = 16;
        g_oneWayAnimPhase = 1;
    }

    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            EntityType tile = g_gameState.tileMap[row][col];

            int spriteY = -1;

            if (tile == EntityType::ONE_WAY_LEFT_TO_RIGHT_PORTAL ||
                tile == EntityType::ONE_WAY_RIGHT_TO_LEFT_PORTAL)
            {
                spriteY = 0xE0;
            }
            else if (tile == EntityType::ONE_WAY_TOP_TO_BOTTOM ||
                     tile == EntityType::ONE_WAY_BOTTOM_TO_TOP)
            {
                spriteY = 0xD0;
            }

            if (spriteY == -1)
                continue;

            int dstX = col * cellWidth;
            int dstY = row * cellHeight;

            g_blockSheet.blit16(
                dstX,
                dstY,
                spriteOffsetX,
                spriteY
            );
        }
    }
}

void def_5AE1_tryPlaceAndSpawn(int row, int col, EntityType type, uint16_t* spawnCounterPtr)
{
    placeTileAndSpawnEntityIfEmpty_Core(row, col, type, spawnCounterPtr);
}

void def_5BCD_tryPlaceAndSpawn(int row, int col, EntityType type, uint16_t* spawnCounterPtr)
{
    placeTileAndSpawnEntityIfEmpty_Core(row, col, type, spawnCounterPtr);
}

static inline bool isInBounds(int r, int c)
{
    return (r >= 0 && r < GRID_ROWS && c >= 0 && c < GRID_COLS);
}

inline void incrementEntityType(EntityType& type)
{
    type = static_cast<EntityType>(
        static_cast<std::uint16_t>(type) + 1
    );
}

void tickSpawnersEvery7Frames()
{
    if (frameCounter % 7 != 0)
    {
        finalizeLevelVisuals();
        return;
    }

    for (int i = 0; i < g_activeSpawnerCount; ++i)
    {
        EntityInfo& entity = g_gameState.entities[i];

        int row = entity.row;
        int col = entity.col;

        entity.animFrame++;

        int targetRow = row;
        int targetCol = col;
        EntityType entityType = EntityType::EMPTY_CELL;

        switch(entity.entityType)
        {
            case EntityType::UnknownA1:
                entityType = EntityType::SQUARE_ARROW_RIGHT;
                targetRow = row + 1;
                break;

            case EntityType::UnknownA2:
                entityType = EntityType::SQUARE_ARROW_UP;
                targetCol = col - 1;
                break;

            case EntityType::UnknownA3:
                entityType = EntityType::SQUARE_ARROW_LEFT;
                targetRow = row - 1;
                break;

            case EntityType::UnknownA4:
                entityType = EntityType::SQUARE_ARROW_DOWN;
                targetCol = col + 1;
                break;

            case EntityType::DISPENSER1:
                entityType = EntityType::ROUNDED_ARROW_RIGHT;
                targetRow = row + 1;
                break;

            case EntityType::DISPENSER2:
                entityType = EntityType::ROUNDED_ARROW_UP;
                targetCol = col - 1;
                break;

            case EntityType::DISPENSER3:
                entityType = EntityType::ROUNDED_ARROW_LEFT;
                targetRow = row - 1;
                break;

            case EntityType::DISPENSER4:
                entityType = EntityType::ROUNDED_ARROW_DOWN;
                targetCol = col + 1;
                break;

            default:
                continue;
        }

        if (g_gameState.tileMap[targetRow][targetCol] == EntityType::EMPTY_CELL)
        {
            int entityIndex = addEntity(
                entityType,
                targetRow,
                targetCol
            );

            moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        }
    }

    finalizeLevelVisuals();
}

inline bool isGridCellEmpty(int targetRow, int targetCol)
{
    return g_gameState.tileMap[targetCol][targetRow] == EntityType::EMPTY;
}

static void placeTileAndSpawnEntityIfEmpty_Core(
    int row, int col, EntityType type,
    uint16_t* spawnCounterPtr
)
{
    if (!isGridCellEmpty(row, col))
        return;

    *spawnCounterPtr = 0;

    const int spawnedEntityIndex = addEntity(type, row, col);
    moveAndRedrawEntity(spawnedEntityIndex, row, col);
}

inline void spawnAtIfEmpty(int targetRow, int targetCol, EntityType type, uint16_t& spawnDelayCounter)
{
    if (!isGridCellEmpty(targetRow, targetCol))
        return;

    spawnDelayCounter = 0;

    const int spawnedEntityIndex = addEntity(type, targetRow, targetCol);
    moveAndRedrawEntity(spawnedEntityIndex, targetRow, targetCol);
}

void updateLevelVisualsAndAnimations()
{
    animateMonsters();
    animateDiamonds();
    animateOneWayTilesEvery4Frames();
    tickSpawnersEvery7Frames();
    animateLava();
}

void animateMonsters()
{
    if (frameCounter % 3 != 0)
        return;

    for (std::int16_t i = 0; i < g_activeSpawnerCount && i < g_gameState.entities.size(); ++i)
    {
        EntityInfo& entity = g_gameState.entities[i];

        const int type = static_cast<int>(entity.entityType);

        if (type < (int)EntityType::EnemyPropeller || type > (int)EntityType::EnemyPropellerRound)
            continue;

        const int dstX = gridOriginX + entity.col * cellWidth;
        const int dstY = gridOriginY + entity.row * cellHeight;

        const int srcX = type * 16;
        const int srcY = entity.animFrame * 16;

        SDL_FRect src{
            (float)srcX,
            (float)srcY,
            16.0f,
            16.0f
        };

        SDL_FRect dst{
            (float)dstX,
            (float)dstY,
            16.0f,
            16.0f
        };

        SDL_RenderTexture(g_renderer, g_sheetMobiles.tex, &src, &dst);

        entity.animFrame = (entity.animFrame + 1) % 4;
    }
}

void handleDirectionalHotkeyAndAdvanceLevel(SDL_Keycode key)
{
    if (g_interactionMode != GameInteractionMode::PLAY_MODE)
        return;

    switch (key)
    {
        case SDLK_UP:
            previousRow = currentRow - 1;
            previousCol = currentCol;
            break;

        case SDLK_DOWN:
            previousRow = currentRow + 1;
            previousCol = currentCol;
            break;

        case SDLK_LEFT:
            previousRow = currentRow;
            previousCol = currentCol - 1;
            break;

        case SDLK_RIGHT:
            previousRow = currentRow;
            previousCol = currentCol + 1;
            break;

        case SDLK_HOME:
            previousRow = currentRow - 1;
            previousCol = currentCol - 1;
            break;

        case SDLK_PAGEUP:
            previousRow = currentRow - 1;
            previousCol = currentCol + 1;
            break;

        case SDLK_END:
            previousRow = currentRow + 1;
            previousCol = currentCol - 1;
            break;

        case SDLK_PAGEDOWN:
            previousRow = currentRow + 1;
            previousCol = currentCol + 1;
            break;

        default:
            return;
    }

    g_hasPendingModal = 1;
    tickLevelFlow();
    updateLevelVisualsAndAnimations();
}

int showFinalDialog()
{
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Kye",
        "Congratulations! You have completed the final level.",
        g_window
    );

    return 0;
}

int showLevelDoneDialog()
{
    const char* title = "Level Finished.";
    const char* message =
        !g_levelVictoryText.empty()
            ? g_levelVictoryText.c_str()
            : "Level completed!";

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        title,
        message,
        g_window
    );

    return 0;
}

int showGameOverDialog()
{
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Game Over",
        "That was the last Kye.\rHave another Go",
        g_window
    );

    return 0;
}

int tickLevelFlow()
{
    g_timerActive = 0;
    initializeRendererIfNeeded();

    if (g_hasPendingModal != 0)
    {
        handleKyeMovement();
    }

    if (isLevelCompleted != 0)
    {
        renderHudAndFrame();
        updateWindow();

        if (levelIndex >= levelCount)
        {
            showFinalDialog();
            levelIndex = 1;
        }
        else
        {
            showLevelDoneDialog();
            levelIndex++;
        }

        loadLevelByIndex(levelIndex);
        showNewLevelDialog(g_windowHandle2);

        isLeftMouseDragActive = 0;
        clearStatusLine(g_statusLineBuffer);

        invalidateWindow();
        updateWindow();
    }
    else
    {
        gameMainLoopTick();

        if (remainingLives < 0)
        {
            runTileSparkleEffect(2);
            drawRectangleFromGrid(currentRow, currentCol);
            showGameOverDialog();
            isLeftMouseDragActive = 0;
            loadLevelByIndex(levelIndex);
            clearStatusLine((const char*)0x29FA);
            invalidateWindow();
            updateWindow();
        }
        else
        {
            if (hasLevelTransition != 0)
            {
                handleKyeMovement();
            }
        }
    }

    advanceToNextLevelOrBlock();
    g_timerActive = 1;

    return 0;
}

void invalidateWindow()
{
    SDL_RenderClear(g_renderer);
}

void updateWindow()
{
    SDL_RenderPresent(g_renderer);
}

void handleDialogClose(NewLevelDialogResult result)
{
    g_newLevelDialogResult = result;
    g_newLevelDialogOpen = false;

    if (result == NewLevelDialogResult::Cancelled)
    {
        g_levelInput.clear();
        return;
    }

    // Accepted
    if (!g_levelInput.empty())
    {
        try
        {
            int level = std::stoi(g_levelInput);

            if (level >= 0)
            {
                g_levelIndex = level;
                loadLevelByIndex(g_levelIndex);
            }
        }
        catch (...)
        {
        }
    }

    g_levelInput.clear();
}

void updateDisplayString(const char* str) {
    if (!str) str = "";
    const std::size_t cap = static_cast<std::size_t>(stringBufferCapacity);
    if (cap == 0) return;
    std::strncpy(stringBuffer, str, cap - 1);
    stringBuffer[cap - 1] = '\0';
    g_needsRedraw = true;
}

int clearStatusLine(const char* str)
{
    std::strncpy(g_statusLineBuffer, str, sizeof(g_statusLineBuffer) - 1);
    g_statusLineBuffer[sizeof(g_statusLineBuffer) - 1] = '\0';

    return 1;
}

static void dispatchEventWithDefaults(int16_t eventCode)
{
    handleEngineEvent(eventCode, /*param0=*/0, /*param1=*/1);
}

int handlePackedMessage(const PackedMessage& msg)
{
    showFileMessage(msg.message);
    dispatchEventWithDefaults(msg.eventCode);
    return 0;
}

int dispatchNotificationByCode(int code)
{
    switch (code)
    {
        case NOTIFY_FATAL:
            handlePackedMessage(PackedMessage{"Fatal error", 3});
            break;

        case NOTIFY_ABORT:
            std::terminate();
            break;

        case NOTIFY_FILE_ERROR:
            handlePackedMessage(PackedMessage{"File error", 1});
            break;

        case NOTIFY_MEMORY_ERROR:
            handlePackedMessage(PackedMessage{"Memory error", 1});
            break;

        case NOTIFY_DATA_ERROR:
            handlePackedMessage(PackedMessage{"Data error", 1});
            break;

        case NOTIFY_STATE_ERROR:
            handlePackedMessage(PackedMessage{"Invalid state", 1});
            break;

        default:
            handlePackedMessage(PackedMessage{"Unknown error", 1});
            break;
    }

    return 0;
}

void destroyGraphicsResources()
{
    auto destroy = [](SDL_Texture*& tex)
    {
        if (tex)
        {
            SDL_DestroyTexture(tex);
            tex = nullptr;
        }
    };

    destroy(g_sheetKye.tex);
    destroy(g_sheetMobiles.tex);
    destroy(g_sheetStatics.tex);
}

void handlePaintOrRenderRequest()
{
    if (g_renderer == nullptr)
        return;
    if (g_hasDeviceContext != 0)
    {
        advanceToNextLevelOrBlock();
    }
    g_hasDeviceContext = 1;
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    initializeLayoutRects();
    renderHudAndFrame();
    g_hasDeviceContext = 0;
}

void handleEvent(const SDL_Event& e)
{
    switch (e.type)
    {
        case SDL_EVENT_QUIT:
        {
            destroyGraphicsResources();
            cleanupAndExit(0);
            return;
        }

        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_SHOWN:
        {
            handlePaintOrRenderRequest();
            return;
        }

        case SDL_EVENT_KEY_DOWN:
        {
            g_keyDownFlag = 1;
            handleDirectionalHotkeyAndAdvanceLevel(e.key.key);
            return;
        }

        case SDL_EVENT_KEY_UP:
        {
            g_keyDownFlag = 0;
            return;
        }

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            const int x = static_cast<int>(e.button.x);
            const int y = static_cast<int>(e.button.y);

            if (e.button.button == SDL_BUTTON_LEFT)
            {
                if (e.button.clicks >= 2)
                {
                    handlePointClick(x, y);
                }
                else
                {
                    handleGameClick(x, y);
                }
            }
            else if (e.button.button == SDL_BUTTON_RIGHT)
            {
                handlePendingInteractionClick(x, y);
            }

            return;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if (e.button.button == SDL_BUTTON_LEFT)
            {
                cancelPendingInteraction();
            }

            return;
        }

        case SDL_EVENT_MOUSE_MOTION:
        {
            const int x = static_cast<int>(e.motion.x);
            const int y = static_cast<int>(e.motion.y);

            handlePendingInteractionFinalize(x, y);
            return;
        }

        default:
        {
            break;
        }
    }
}

int copyMappedStringForCode(int code)
{
    auto it = exceptionMessages.find(code);

    if (it != exceptionMessages.end())
    {
        std::strcpy(stringBuffer, it->second);
        return 0;
    }

    handlePackedMessage({
        "Floating Point: Square Root of Negative Number",
        3
    });

    return 0;
}

std::int16_t handleNotificationOrCallbackByCode(uint16_t eventCode)
{
    const std::int16_t slotIndex = findIndexInTable(eventCode);
    if (static_cast<uint16_t>(slotIndex) == 0xFFFFu) {
        return 1;
    }

    const uint16_t slotWord = g_notificationHandlerSlotWord[static_cast<uint16_t>(slotIndex)];

    // slotWord == 1 => do nothing
    if (slotWord == 1u) {
        return 0;
    }

    // slotWord == 0 => fallback behavior
    if (slotWord == 0u) {
        if (eventCode == 8u) {
            copyMappedStringForCode(0x008Cu);
        } else {
            dispatchNotificationByCode(eventCode);
        }
        return 0;
    }

    // slotWord != 0 && != 1 => treat as callback pointer
    g_notificationHandlerSlotWord[static_cast<uint16_t>(slotIndex)] = 0;

    const uint16_t handlerParam = static_cast<uint16_t>(g_notificationHandlerParam[static_cast<uint16_t>(slotIndex)]);

    const auto handlerFn = reinterpret_cast<NotificationHandlerFn>(
        static_cast<std::uintptr_t>(slotWord)
    );

    handlerFn(eventCode, handlerParam);
    return 0;
}

std::int16_t triggerNotification_0x0016()
{
    return handleNotificationOrCallbackByCode(0x0016u);
}

void configureFileMode() {
    if (g_configureFileMode) g_configureFileMode();
}

void invokeExternalCallback()
{
    if (g_invokeExternalCallbackFn) {
        g_invokeExternalCallbackFn();
    }
}

void processCallbackQueueFromEngineEvent()
{
    processCallbackQueue(
        reinterpret_cast<CallbackQueueEntry*>(&g_gameState),
        reinterpret_cast<CallbackQueueEntry*>(&g_gameState)
    );
}

void advanceKyeAndCarryTile(int stepRow, int stepCol)
{
    if (selectedObjectIndex == 0xFFFF)
    {
        drawRectangleFromGrid(currentRow, currentCol);
    }
    else
    {
        g_gameState.tileMap[currentRow][currentCol] = static_cast<EntityType>(selectedObjectIndex);

        renderStaticObjects(currentRow, currentCol, g_gameState.tileMap[currentRow][currentCol]);
    }

    currentRow += stepRow;
    currentCol += stepCol;

    selectedObjectIndex = static_cast<std::uint16_t>(
        g_gameState.tileMap[currentRow][currentCol]
    );

    g_gameState.tileMap[currentRow][currentCol] = EntityType::KYE_LOCATION;

    runTileSparkleEffect(0);
}

// int processKyeCollision(int stepRow, int stepCol)
// {
//     if ((stepRow != 0 && stepCol != 0) || (stepRow == 0 && stepCol == 0))
//         return 0;

//     int newRow = currentRow + stepRow;
//     int newCol = currentCol + stepCol;

//     EntityType target = g_gameState.tileMap[newRow][newCol];

//     if (target == EntityType::EMPTY_CELL)
//     {
//         advanceKyeAndCarryTile(stepRow, stepCol);
//         return 1;
//     }

//     if (target == EntityType::BREAKABLE_BRICK)
//     {
//         advanceKyeAndCarryTile(stepRow, stepCol);
//         selectedObjectIndex = 0xFFFF;
//         return 1;
//     }

//     if (target == EntityType::DIAMOND)
//     {
//         advanceKyeAndCarryTile(stepRow, stepCol);
//         selectedObjectIndex = 0xFFFF;

//         if (isLeftMouseDragActive > 0)
//             --isLeftMouseDragActive;

//         onDiamondCollected();
//         renderLivesAndLevelInfo();
//         return 1;
//     }

//     if ((int)target >= 0 && (int)target < g_gameState.entities.size())
//     {
//         int entityIndex = (int)target;
//         EntityInfo& e = g_gameState.entities[entityIndex];

//         if (e.entityType == EntityType::Lava)
//         {
//             --remainingLives;
//             updateLivesDisplay();
//             renderLivesAndLevelInfo();
//             return 0;
//         }

//         if (e.entityType == EntityType::EMPTY_CELL)
//             return 0;

//         int pushRow = e.row + stepRow;
//         int pushCol = e.col + stepCol;

//         if (pushRow < 0 || pushRow >= GRID_ROWS ||
//             pushCol < 0 || pushCol >= GRID_COLS)
//             return 0;

//         EntityType pushTarget = g_gameState.tileMap[pushRow][pushCol];

//         if (pushTarget == EntityType::EMPTY_CELL)
//         {
//             moveAndRedrawEntity(entityIndex, pushRow, pushCol);
//             advanceKyeAndCarryTile(stepRow, stepCol);
//             return 1;
//         }

//         if ((int)pushTarget >= 0 && (int)pushTarget < g_activeSpawnerCount)
//         {
//             int targetEntityIndex = (int)pushTarget;
//             EntityInfo& targetEntity = g_gameState.entities[targetEntityIndex];

//             if (targetEntity.entityType == EntityType::Lava)
//             {
//                 if (!destroyEntityIfFallsIntoLava(entityIndex, pushRow, pushCol))
//                 {
//                     moveAndRedrawEntity(entityIndex, pushRow, pushCol);
//                 }

//                 advanceKyeAndCarryTile(stepRow, stepCol);
//                 return 1;
//             }
//         }

//         return 0;
//     }

//     return 0;
// }

int processKyeCollision(int stepRow, int stepCol)
{
    int moved = 0;

    int deltaRow = stepRow;
    int deltaCol = stepCol;

    int targetRow = currentRow + deltaRow;
    int targetCol = currentCol + deltaCol;

    const bool isDiagonalMove = (deltaRow != 0) && (deltaCol != 0);

    if (isDiagonalMove)
    {
        bool blockedDiagonal =
            (g_gameState.tileMap[targetRow][currentCol] != EntityType::EMPTY_CELL) ||
            (g_gameState.tileMap[currentRow][targetCol] != EntityType::EMPTY_CELL);

        if (blockedDiagonal && isLeftMouseDragActive != 0)
        {
            if (g_gameState.tileMap[targetRow][currentCol] == EntityType::EMPTY_CELL)
            {
                deltaCol = 0;
                targetCol = currentCol;
                blockedDiagonal = false;
            }
            else if (g_gameState.tileMap[currentRow][targetCol] == EntityType::EMPTY_CELL)
            {
                deltaRow = 0;
                targetRow = currentRow;
                blockedDiagonal = false;
            }
        }

        if (blockedDiagonal)
        {
            if (isLeftMouseDragActive != 0 && moved == 0)
            {
                static constexpr int fallbackRows[4] = { -1, 1, 0, 0 };
                static constexpr int fallbackCols[4] = { 0, 0, -1, 1 };

                const int currentDistanceSquared =
                    (previousRow - currentRow) * (previousRow - currentRow) +
                    (previousCol - currentCol) * (previousCol - currentCol);

                for (int i = 0; i < 4; ++i)
                {
                    const int candidateFallbackRow = currentRow + fallbackRows[i];
                    const int candidateFallbackCol = currentCol + fallbackCols[i];

                    if (g_gameState.tileMap[candidateFallbackRow][candidateFallbackCol] != EntityType::EMPTY_CELL)
                    {
                        continue;
                    }

                    const int fallbackDistanceSquared =
                        (previousRow - candidateFallbackRow) * (previousRow - candidateFallbackRow) +
                        (previousCol - candidateFallbackCol) * (previousCol - candidateFallbackCol);

                    if (fallbackDistanceSquared < currentDistanceSquared)
                    {
                        advanceKyeAndCarryTile(fallbackRows[i], fallbackCols[i]);
                        break;
                    }
                }
            }

            return 0;
        }
    }

    const EntityType targetCell = g_gameState.tileMap[targetRow][targetCol];

    if (targetCell == EntityType::EMPTY_CELL)
    {
        advanceKyeAndCarryTile(deltaRow, deltaCol);
        moved = 1;
    }
    else if (targetCell == EntityType::BREAKABLE_BRICK)
    {
        advanceKyeAndCarryTile(deltaRow, deltaCol);
        selectedObjectIndex = 0xFFFF;
        moved = 1;
    }
    else if (targetCell == EntityType::DIAMOND)
    {
        advanceKyeAndCarryTile(deltaRow, deltaCol);
        selectedObjectIndex = 0xFFFF;
        moved = 1;
        onDiamondCollected();
        renderLivesAndLevelInfo();
    }
    else if (targetCell == EntityType::ONE_WAY_LEFT_TO_RIGHT_PORTAL)
    {
        if (deltaRow == 0 && deltaCol == 1)
        {
            advanceKyeAndCarryTile(deltaRow, deltaCol);
        }
    }
    else if (targetCell == EntityType::ONE_WAY_RIGHT_TO_LEFT_PORTAL)
    {
        if (deltaRow == 0 && deltaCol == -1)
        {
            advanceKyeAndCarryTile(deltaRow, deltaCol);
        }
    }
    else if (targetCell == EntityType::ONE_WAY_TOP_TO_BOTTOM)
    {
        if (deltaRow == 1 && deltaCol == 0)
        {
            advanceKyeAndCarryTile(deltaRow, deltaCol);
        }
    }
    else if (targetCell == EntityType::ONE_WAY_BOTTOM_TO_TOP)
    {
        if (deltaRow == -1 && deltaCol == 0)
        {
            advanceKyeAndCarryTile(deltaRow, deltaCol);
        }
    }
    else if (static_cast<int16_t>(targetCell) >= 0)
    {
        const int entityIndex = static_cast<int>(targetCell);
        EntityInfo& entity = g_gameState.entities[entityIndex];

        if (entity.entityType == EntityType::Lava)
        {
            --remainingLives;
            updateLivesDisplay();
            renderLivesAndLevelInfo();
            return 0;
        }

        if (entity.entityType != EntityType::Lava2)
        {
            const int newRow = entity.row + deltaRow;
            const int newCol = entity.col + deltaCol;

            bool blockedPushDiagonal = false;

            if ((deltaRow != 0) && (deltaCol != 0))
            {
                blockedPushDiagonal =
                    (g_gameState.tileMap[newRow][entity.col] != EntityType::EMPTY_CELL) ||
                    (g_gameState.tileMap[entity.row][newCol] != EntityType::EMPTY_CELL);
            }

            const EntityType pushTarget = g_gameState.tileMap[newRow][newCol];

            if (pushTarget == EntityType::EMPTY_CELL)
            {
                if (!blockedPushDiagonal)
                {
                    moveAndRedrawEntity(entityIndex, newRow, newCol);
                    advanceKyeAndCarryTile(deltaRow, deltaCol);
                    moved = 1;
                }
            }
            else if (static_cast<int16_t>(pushTarget) >= 0)
            {
                const int pushTargetEntityIndex = static_cast<int>(pushTarget);
                EntityInfo& pushTargetEntity = g_gameState.entities[pushTargetEntityIndex];

                if (pushTargetEntity.entityType == EntityType::Lava && !blockedPushDiagonal)
                {
                    if (destroyEntityIfFallsIntoLava(entityIndex, newRow, newCol) == 0)
                    {
                        moveAndRedrawEntity(entityIndex, newRow, newCol);
                    }

                    advanceKyeAndCarryTile(deltaRow, deltaCol);
                    moved = 1;
                }
            }
        }
    }

    if (isLeftMouseDragActive != 0 && moved == 0)
    {
        static constexpr int fallbackRows[4] = { -1, 1, 0, 0 };
        static constexpr int fallbackCols[4] = { 0, 0, -1, 1 };

        const int currentDistanceSquared =
            (previousRow - currentRow) * (previousRow - currentRow) +
            (previousCol - currentCol) * (previousCol - currentCol);

        for (int i = 0; i < 4; ++i)
        {
            const int candidateFallbackRow = currentRow + fallbackRows[i];
            const int candidateFallbackCol = currentCol + fallbackCols[i];

            if (g_gameState.tileMap[candidateFallbackRow][candidateFallbackCol] != EntityType::EMPTY_CELL)
            {
                continue;
            }

            const int fallbackDistanceSquared =
                (previousRow - candidateFallbackRow) * (previousRow - candidateFallbackRow) +
                (previousCol - candidateFallbackCol) * (previousCol - candidateFallbackCol);

            if (fallbackDistanceSquared < currentDistanceSquared)
            {
                advanceKyeAndCarryTile(fallbackRows[i], fallbackCols[i]);
                break;
            }
        }
    }

    return 0;
}

void handlePushableBrick(int entityIndex)
{
    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleSmartEntityAlt(int entityIndex) {
    if (!tryApplyMagneticDisplacement(entityIndex))
        return;
}

void handleSquareArrowUp(int entityIndex)
{
    auto& s = g_gameState;

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;
    int targetRow = row - 1;
    int targetCol = col;

    int tile = (int)s.tileMap[targetRow][targetCol];

    if (tile == (int)EntityType::EMPTY_CELL)
    {
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, targetRow, targetCol))
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    EntityType type;

    if (tile >= 0 && tile < g_activeSpawnerCount)
        type = s.entities[tile].entityType;
    else
        type = (EntityType)tile;

    if (type == EntityType::DEFLECTOR_LEFT)
    {
        s.entities[entityIndex].entityType = EntityType::SQUARE_ARROW_RIGHT;
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (type == EntityType::DEFLECTOR_RIGHT)
    {
        s.entities[entityIndex].entityType = EntityType::SQUARE_ARROW_LEFT;
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleSquareArrowDown(int entityIndex)
{
    auto& s = g_gameState;

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int targetRow = row + 1;
    int targetCol = col;

    int tile = (int)s.tileMap[targetRow][targetCol];

    if (tile == (int)EntityType::EMPTY_CELL)
    {
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, targetRow, targetCol))
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    EntityType type;

    if (tile >= 0 && tile < g_activeSpawnerCount)
        type = s.entities[tile].entityType;
    else
        type = (EntityType)tile;

    if (type == EntityType::DEFLECTOR_LEFT)
    {
        s.entities[entityIndex].entityType = EntityType::SQUARE_ARROW_LEFT;
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (type == EntityType::DEFLECTOR_RIGHT)
    {
        s.entities[entityIndex].entityType = EntityType::SQUARE_ARROW_RIGHT;
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleSquareArrowLeft(int entityIndex)
{
    auto& s = g_gameState;

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int targetRow = row;
    int targetCol = col - 1;

    int tile = (int)s.tileMap[targetRow][targetCol];

    if (tile == (int)EntityType::EMPTY_CELL)
    {
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, targetRow, targetCol))
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    EntityType type;

    if (tile >= 0 && tile < g_activeSpawnerCount)
        type = s.entities[tile].entityType;
    else
        type = (EntityType)tile;

    if (type == EntityType::DEFLECTOR_LEFT)
    {
        s.entities[entityIndex].entityType = EntityType::SQUARE_ARROW_UP;
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (type == EntityType::DEFLECTOR_RIGHT)
    {
        s.entities[entityIndex].entityType = EntityType::SQUARE_ARROW_DOWN;
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleSquareArrowRight(int entityIndex)
{
    auto& s = g_gameState;

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int targetRow = row;
    int targetCol = col + 1;

    int tile = (int)s.tileMap[targetRow][targetCol];

    if (tile == (int)EntityType::EMPTY_CELL)
    {
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, targetRow, targetCol))
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    EntityType type;

    if (tile >= 0 && tile < g_activeSpawnerCount)
        type = s.entities[tile].entityType;
    else
        type = (EntityType)tile;

    if (type == EntityType::DEFLECTOR_LEFT)
    {
        s.entities[entityIndex].entityType = EntityType::SQUARE_ARROW_DOWN;
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (type == EntityType::DEFLECTOR_RIGHT)
    {
        s.entities[entityIndex].entityType = EntityType::SQUARE_ARROW_UP;
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleUnknownEntityType(int entityIndex) {}

void handlePusherLeft(int entityIndex)
{
    auto& s = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int left = findEntityAt(row, col - 1);

    if (left == -1)
    {
        if (s.tileMap[row][col - 1] != EntityType::EMPTY_CELL)
        {
            s.entities[entityIndex].entityType = EntityType::PUSHER_RIGHT;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }

        moveAndRedrawEntity(entityIndex, row, col - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, row, col - 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    s.entities[entityIndex].entityType = EntityType::PUSHER_RIGHT;
    moveAndRedrawEntity(entityIndex, row, col);

    left = findEntityAt(row, col - 1);

    if (left < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int targetRow = s.entities[left].row;
    int targetCol = s.entities[left].col - 1;

    EntityType tile = s.tileMap[targetRow][targetCol];

    if (tile != EntityType::EMPTY_CELL)
    {
        const int tileEntityIndex = tileToEntityIndex(tile);

        if (tileEntityIndex < 0 ||
            s.entities[tileEntityIndex].entityType != EntityType::Lava)
        {
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    if (destroyEntityIfFallsIntoLava(left, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    moveAndRedrawEntity(left, targetRow, targetCol);
    handleUnknownEntityType(entityIndex);
}


void handlePusherRight(int entityIndex)
{
    auto& s = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int right = findEntityAt(row, col + 1);

    if (right == -1)
    {
        if (s.tileMap[row][col + 1] != EntityType::EMPTY_CELL)
        {
            s.entities[entityIndex].entityType = EntityType::PUSHER_LEFT;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }

        moveAndRedrawEntity(entityIndex, row, col + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, row, col + 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    s.entities[entityIndex].entityType = EntityType::PUSHER_LEFT;
    moveAndRedrawEntity(entityIndex, row, col);

    right = findEntityAt(row, col + 1);

    if (right < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int targetRow = s.entities[right].row;
    int targetCol = s.entities[right].col + 1;

    EntityType tile = s.tileMap[targetRow][targetCol];

    if (tile != EntityType::EMPTY_CELL)
    {
        const int tileEntityIndex = tileToEntityIndex(tile);

        if (tileEntityIndex < 0 ||
            s.entities[tileEntityIndex].entityType != EntityType::Lava)
            {
                handleUnknownEntityType(entityIndex);
                return;
            }
    }

    if (destroyEntityIfFallsIntoLava(right, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    moveAndRedrawEntity(right, targetRow, targetCol);
    handleUnknownEntityType(entityIndex);
}

static bool legacyRandomBit()
{
    pseudoRandomUpdate(0x8000);

    const std::uint64_t value =
        (static_cast<std::uint64_t>(speedMultiplierHigh) << 16) |
         static_cast<std::uint64_t>(speedMultiplierLow);

    return (value & 1ULL) != 0;
}

static bool isValidEntityIndex(int index)
{
    return index >= 0 && index < static_cast<int>(g_activeSpawnerCount);
}

void handlePusherUp(int entityIndex)
{
    auto& s = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int up = findEntityAt(row - 1, col);

    if (up == -1)
    {
        if (s.tileMap[row - 1][col] != EntityType::EMPTY_CELL)
        {
            s.entities[entityIndex].entityType = EntityType::PUSHER_DOWN;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }

        moveAndRedrawEntity(entityIndex, row - 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, row - 1, col) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    s.entities[entityIndex].entityType = EntityType::PUSHER_DOWN;
    moveAndRedrawEntity(entityIndex, row, col);

    up = findEntityAt(row - 1, col);

    if (up < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int targetRow = s.entities[up].row - 1;
    int targetCol = s.entities[up].col;

    EntityType tile = s.tileMap[targetRow][targetCol];

    if (tile != EntityType::EMPTY_CELL)
    {
        const int tileEntityIndex = tileToEntityIndex(tile);

        if (tileEntityIndex < 0 ||
            s.entities[tileEntityIndex].entityType != EntityType::Lava)
            {
                handleUnknownEntityType(entityIndex);
                return;
            }
    }

    if (destroyEntityIfFallsIntoLava(up, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    moveAndRedrawEntity(up, targetRow, targetCol);
    handleUnknownEntityType(entityIndex);
}

void handlePusherDown(int entityIndex)
{
    auto& s = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int down = findEntityAt(row + 1, col);

    if (down == -1)
    {
        if (s.tileMap[row + 1][col] != EntityType::EMPTY_CELL)
        {
            s.entities[entityIndex].entityType = EntityType::PUSHER_UP;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }

        moveAndRedrawEntity(entityIndex, row + 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, row + 1, col) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    s.entities[entityIndex].entityType = EntityType::PUSHER_UP;
    moveAndRedrawEntity(entityIndex, row, col);

    down = findEntityAt(row + 1, col);

    if (down < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int targetRow = s.entities[down].row + 1;
    int targetCol = s.entities[down].col;

    EntityType tile = s.tileMap[targetRow][targetCol];

    if (tile != EntityType::EMPTY_CELL)
    {
        const int tileEntityIndex = tileToEntityIndex(tile);

        if (tileEntityIndex < 0 ||
            s.entities[tileEntityIndex].entityType != EntityType::Lava)
            {
                handleUnknownEntityType(entityIndex);
                return;
            }
    }

    if (destroyEntityIfFallsIntoLava(down, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    moveAndRedrawEntity(down, targetRow, targetCol);
    handleUnknownEntityType(entityIndex);
}

void handleRoundedArrowUp(int entityIndex)
{
    auto& s = g_gameState;

    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int row = static_cast<int>(s.entities[entityIndex].row);
    const int col = static_cast<int>(s.entities[entityIndex].col);
    const int targetRow = row - 1;
    const int targetCol = col;

    const int16_t obstacleAbove = static_cast<int16_t>(s.tileMap[targetRow][targetCol]);

    if (obstacleAbove == static_cast<int16_t>(EntityType::EMPTY_CELL))
    {
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    bool isRoundedArrowAbove = false;
    bool isRoundedPushableAbove = false;

    if (obstacleAbove >= 0 && obstacleAbove < static_cast<int16_t>(g_activeSpawnerCount))
    {
        const EntityType obstacleType = s.entities[obstacleAbove].entityType;

        if (obstacleType >= EntityType::ROUNDED_ARROW_UP &&
            obstacleType <= EntityType::ROUNDED_ARROW_RIGHT)
        {
            isRoundedArrowAbove = true;
        }

        if (obstacleType == EntityType::ROUNDED_PUSHABLE_BRICK)
        {
            isRoundedPushableAbove = true;
        }
    }

    const bool leftCellsEmpty =
        (col - 1 >= 0) &&
        (s.tileMap[row][col - 1] == EntityType::EMPTY_CELL) &&
        (s.tileMap[targetRow][col - 1] == EntityType::EMPTY_CELL);

    const bool rightCellsEmpty =
        (col + 1 < GRID_COLS) &&
        (s.tileMap[row][col + 1] == EntityType::EMPTY_CELL) &&
        (s.tileMap[targetRow][col + 1] == EntityType::EMPTY_CELL);

    const bool allowLeft =
        leftCellsEmpty &&
        (
            obstacleAbove == static_cast<int16_t>(EntityType::BOTTOM_LEFT_ROUND_WALL) ||
            obstacleAbove == static_cast<int16_t>(EntityType::BOTTOM_ROUND_WALL) ||
            obstacleAbove == static_cast<int16_t>(EntityType::LEFT_ROUND_WALL) ||
            isRoundedArrowAbove ||
            isRoundedPushableAbove
        );

    const bool allowRight =
        rightCellsEmpty &&
        (
            obstacleAbove == static_cast<int16_t>(EntityType::BOTTOM_RIGHT_ROUND_WALL) ||
            obstacleAbove == static_cast<int16_t>(EntityType::BOTTOM_ROUND_WALL) ||
            obstacleAbove == static_cast<int16_t>(EntityType::RIGHT_ROUND_WALL) ||
            isRoundedArrowAbove ||
            isRoundedPushableAbove
        );

    if (allowLeft && allowRight)
    {
        if (DeterministicRNG::next(0, 1) == 0)
        {
            moveAndRedrawEntity(entityIndex, row - 1, col + 1);
        }
        else
        {
            moveAndRedrawEntity(entityIndex, row - 1, col - 1);
        }

        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowRight)
    {
        moveAndRedrawEntity(entityIndex, row - 1, col + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowLeft)
    {
        moveAndRedrawEntity(entityIndex, row - 1, col - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (obstacleAbove >= 0 && obstacleAbove < static_cast<int16_t>(g_activeSpawnerCount))
    {
        const EntityType obstacleType = s.entities[obstacleAbove].entityType;

        if (obstacleType == EntityType::DEFLECTOR_LEFT)
        {
            s.entities[entityIndex].entityType = EntityType::ROUNDED_ARROW_RIGHT;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }

        if (obstacleType == EntityType::DEFLECTOR_RIGHT)
        {
            s.entities[entityIndex].entityType = EntityType::ROUNDED_ARROW_LEFT;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleRoundedArrowDown(int entityIndex)
{
    auto& s = g_gameState;

    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int row = static_cast<int>(s.entities[entityIndex].row);
    const int col = static_cast<int>(s.entities[entityIndex].col);
    const int targetRow = row + 1;
    const int targetCol = col;

    const int16_t obstacleBelow = static_cast<int16_t>(s.tileMap[targetRow][targetCol]);

    if (obstacleBelow == static_cast<int16_t>(EntityType::EMPTY_CELL))
    {
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    // FIX 2: glide on TOP_LEFT_ROUNDED_WALL => RIGHT
    if (obstacleBelow == static_cast<int16_t>(EntityType::TOP_LEFT_ROUND_WALL))
    {
        if (col + 1 < GRID_COLS &&
            s.tileMap[row][col + 1] == EntityType::EMPTY_CELL)
        {
            moveAndRedrawEntity(entityIndex, row, col + 1);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    bool isRoundedArrowBelow = false;
    bool isRoundedPushableBelow = false;

    if (obstacleBelow >= 0 && obstacleBelow < static_cast<int16_t>(g_activeSpawnerCount))
    {
        const EntityType obstacleType = s.entities[obstacleBelow].entityType;

        if (obstacleType >= EntityType::ROUNDED_ARROW_UP &&
            obstacleType <= EntityType::ROUNDED_ARROW_RIGHT)
        {
            isRoundedArrowBelow = true;
        }

        if (obstacleType == EntityType::ROUNDED_PUSHABLE_BRICK)
        {
            isRoundedPushableBelow = true;
        }
    }

    const bool leftCellsEmpty =
        (col - 1 >= 0) &&
        (s.tileMap[row][col - 1] == EntityType::EMPTY_CELL) &&
        (s.tileMap[targetRow][col - 1] == EntityType::EMPTY_CELL);

    const bool rightCellsEmpty =
        (col + 1 < GRID_COLS) &&
        (s.tileMap[row][col + 1] == EntityType::EMPTY_CELL) &&
        (s.tileMap[targetRow][col + 1] == EntityType::EMPTY_CELL);

    const bool allowLeft =
        leftCellsEmpty &&
        (
            obstacleBelow == static_cast<int16_t>(EntityType::TOP_RIGHT_ROUND_WALL) ||
            obstacleBelow == static_cast<int16_t>(EntityType::TOP_ROUND_WALL) ||
            obstacleBelow == static_cast<int16_t>(EntityType::RIGHT_ROUND_WALL) ||
            obstacleBelow == static_cast<int16_t>(EntityType::LEFT_ROUND_WALL) ||
            obstacleBelow == static_cast<int16_t>(EntityType::TOP_LEFT_ROUND_WALL) ||
            isRoundedArrowBelow ||
            isRoundedPushableBelow
        );

    const bool allowRight =
        rightCellsEmpty &&
        (
            obstacleBelow == static_cast<int16_t>(EntityType::TOP_LEFT_ROUND_WALL) ||
            obstacleBelow == static_cast<int16_t>(EntityType::TOP_ROUND_WALL) ||
            obstacleBelow == static_cast<int16_t>(EntityType::LEFT_ROUND_WALL) ||
            obstacleBelow == static_cast<int16_t>(EntityType::RIGHT_ROUND_WALL) ||
            obstacleBelow == static_cast<int16_t>(EntityType::TOP_RIGHT_ROUND_WALL) ||
            isRoundedArrowBelow ||
            isRoundedPushableBelow
        );

    if (allowLeft && allowRight)
    {
        if (DeterministicRNG::next(0, 1) == 0)
        {
            moveAndRedrawEntity(entityIndex, row + 1, col + 1);
        }
        else
        {
            moveAndRedrawEntity(entityIndex, row + 1, col - 1);
        }

        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowLeft)
    {
        moveAndRedrawEntity(entityIndex, row + 1, col - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowRight)
    {
        moveAndRedrawEntity(entityIndex, row + 1, col + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (obstacleBelow >= 0 && obstacleBelow < static_cast<int16_t>(g_activeSpawnerCount))
    {
        const EntityType obstacleType = s.entities[obstacleBelow].entityType;

        if (obstacleType == EntityType::DEFLECTOR_LEFT)
        {
            s.entities[entityIndex].entityType = EntityType::ROUNDED_ARROW_LEFT;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }

        if (obstacleType == EntityType::DEFLECTOR_RIGHT)
        {
            s.entities[entityIndex].entityType = EntityType::ROUNDED_ARROW_RIGHT;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleRoundedArrowLeft(int entityIndex)
{
    auto& s = g_gameState;

    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int row = static_cast<int>(s.entities[entityIndex].row);
    const int col = static_cast<int>(s.entities[entityIndex].col);
    const int targetRow = row;
    const int targetCol = col - 1;

    const int16_t obstacleLeft = static_cast<int16_t>(s.tileMap[targetRow][targetCol]);

    if (obstacleLeft == static_cast<int16_t>(EntityType::EMPTY_CELL))
    {
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (obstacleLeft == static_cast<int16_t>(EntityType::TOP_RIGHT_ROUND_WALL))
    {
        if (row + 1 < GRID_ROWS &&
            s.tileMap[row + 1][col] == EntityType::EMPTY_CELL)
        {
            moveAndRedrawEntity(entityIndex, row + 1, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    bool isRoundedArrowLeft = false;
    bool isRoundedPushableLeft = false;

    if (obstacleLeft >= 0 && obstacleLeft < static_cast<int16_t>(g_activeSpawnerCount))
    {
        const EntityType obstacleType = s.entities[obstacleLeft].entityType;

        if (obstacleType >= EntityType::ROUNDED_ARROW_UP &&
            obstacleType <= EntityType::ROUNDED_ARROW_RIGHT)
        {
            isRoundedArrowLeft = true;
        }

        if (obstacleType == EntityType::ROUNDED_PUSHABLE_BRICK)
        {
            isRoundedPushableLeft = true;
        }
    }

    const bool upCellsEmpty =
        (row - 1 >= 0) &&
        (s.tileMap[row - 1][col] == EntityType::EMPTY_CELL) &&
        (s.tileMap[row - 1][targetCol] == EntityType::EMPTY_CELL);

    const bool downCellsEmpty =
        (row + 1 < GRID_ROWS) &&
        (s.tileMap[row + 1][col] == EntityType::EMPTY_CELL) &&
        (s.tileMap[row + 1][targetCol] == EntityType::EMPTY_CELL);

    const bool allowUp =
        upCellsEmpty &&
        (
            obstacleLeft == static_cast<int16_t>(EntityType::BOTTOM_RIGHT_ROUND_WALL) ||
            obstacleLeft == static_cast<int16_t>(EntityType::RIGHT_ROUND_WALL) ||
            obstacleLeft == static_cast<int16_t>(EntityType::BOTTOM_ROUND_WALL) ||
            obstacleLeft == static_cast<int16_t>(EntityType::TOP_RIGHT_ROUND_WALL) ||
            isRoundedArrowLeft ||
            isRoundedPushableLeft
        );

    const bool allowDown =
        downCellsEmpty &&
        (
            obstacleLeft == static_cast<int16_t>(EntityType::TOP_RIGHT_ROUND_WALL) ||
            obstacleLeft == static_cast<int16_t>(EntityType::RIGHT_ROUND_WALL) ||
            obstacleLeft == static_cast<int16_t>(EntityType::TOP_ROUND_WALL) ||
            obstacleLeft == static_cast<int16_t>(EntityType::BOTTOM_RIGHT_ROUND_WALL) ||
            isRoundedArrowLeft ||
            isRoundedPushableLeft
        );

    if (allowUp && allowDown)
    {
        if (DeterministicRNG::next(0, 1) == 0)
            moveAndRedrawEntity(entityIndex, row + 1, col - 1);
        else
            moveAndRedrawEntity(entityIndex, row - 1, col - 1);

        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowUp)
    {
        moveAndRedrawEntity(entityIndex, row - 1, col - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowDown)
    {
        moveAndRedrawEntity(entityIndex, row + 1, col - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (obstacleLeft >= 0 && obstacleLeft < static_cast<int16_t>(g_activeSpawnerCount))
    {
        const EntityType obstacleType = s.entities[obstacleLeft].entityType;

        if (obstacleType == EntityType::DEFLECTOR_LEFT)
        {
            s.entities[entityIndex].entityType = EntityType::ROUNDED_ARROW_UP;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }

        if (obstacleType == EntityType::DEFLECTOR_RIGHT)
        {
            s.entities[entityIndex].entityType = EntityType::ROUNDED_ARROW_DOWN;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleRoundedArrowRight(int entityIndex)
{
    auto& s = g_gameState;

    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int row = static_cast<int>(s.entities[entityIndex].row);
    const int col = static_cast<int>(s.entities[entityIndex].col);
    const int targetRow = row;
    const int targetCol = col + 1;

    const int16_t obstacleRight = static_cast<int16_t>(s.tileMap[targetRow][targetCol]);

    if (obstacleRight == static_cast<int16_t>(EntityType::EMPTY_CELL))
    {
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    bool isRoundedArrowRight = false;
    bool isRoundedPushableRight = false;

    if (obstacleRight >= 0 && obstacleRight < static_cast<int16_t>(g_activeSpawnerCount))
    {
        const EntityType obstacleType = s.entities[obstacleRight].entityType;

        if (obstacleType >= EntityType::ROUNDED_ARROW_UP &&
            obstacleType <= EntityType::ROUNDED_ARROW_RIGHT)
        {
            isRoundedArrowRight = true;
        }

        if (obstacleType == EntityType::ROUNDED_PUSHABLE_BRICK)
        {
            isRoundedPushableRight = true;
        }
    }

    const bool upCellsEmpty =
        (row - 1 >= 0) &&
        (s.tileMap[row - 1][col] == EntityType::EMPTY_CELL) &&
        (s.tileMap[row - 1][targetCol] == EntityType::EMPTY_CELL);

    const bool downCellsEmpty =
        (row + 1 < GRID_ROWS) &&
        (s.tileMap[row + 1][col] == EntityType::EMPTY_CELL) &&
        (s.tileMap[row + 1][targetCol] == EntityType::EMPTY_CELL);

    const bool allowUp =
        upCellsEmpty &&
        (
            obstacleRight == static_cast<int16_t>(EntityType::TOP_LEFT_ROUND_WALL) ||
            obstacleRight == static_cast<int16_t>(EntityType::LEFT_ROUND_WALL) ||
            obstacleRight == static_cast<int16_t>(EntityType::TOP_ROUND_WALL) ||
            isRoundedArrowRight ||
            isRoundedPushableRight
        );

    const bool allowDown =
        downCellsEmpty &&
        (
            obstacleRight == static_cast<int16_t>(EntityType::BOTTOM_LEFT_ROUND_WALL) ||
            obstacleRight == static_cast<int16_t>(EntityType::LEFT_ROUND_WALL) ||
            obstacleRight == static_cast<int16_t>(EntityType::BOTTOM_ROUND_WALL) ||
            isRoundedArrowRight ||
            isRoundedPushableRight
        );

    if (allowUp && allowDown)
    {
        if (DeterministicRNG::next(0, 1) == 0)
        {
            moveAndRedrawEntity(entityIndex, row + 1, col + 1);
        }
        else
        {
            moveAndRedrawEntity(entityIndex, row - 1, col + 1);
        }

        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowUp)
    {
        moveAndRedrawEntity(entityIndex, row - 1, col + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowDown)
    {
        moveAndRedrawEntity(entityIndex, row + 1, col + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (obstacleRight >= 0 && obstacleRight < static_cast<int16_t>(g_activeSpawnerCount))
    {
        const EntityType obstacleType = s.entities[obstacleRight].entityType;

        if (obstacleType == EntityType::DEFLECTOR_LEFT)
        {
            s.entities[entityIndex].entityType = EntityType::ROUNDED_ARROW_DOWN;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }

        if (obstacleType == EntityType::DEFLECTOR_RIGHT)
        {
            s.entities[entityIndex].entityType = EntityType::ROUNDED_ARROW_UP;
            moveAndRedrawEntity(entityIndex, row, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleMonsterEntityType(int entityIndex)
{
    auto& s = g_gameState;

    if (checkAndHandleDeathCondition(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    if ((frameCounter % 3) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (tryApplyMagneticDisplacement(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    int newRow = s.entities[entityIndex].row;
    int newCol = s.entities[entityIndex].col;
    if (canMagnetMoveEntity(entityIndex) != 0)
    {
        if (destroyEntityIfFallsIntoLava(entityIndex, newRow, newCol) != 0)
        {
            handleUnknownEntityType(entityIndex);
            return;
        }

        moveAndRedrawEntity(entityIndex, newRow, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (DeterministicRNG::next(0, 1) == 1)
    {
        adjustMonsterTargetTowardPosition(entityIndex, currentRow, currentCol, &newRow, &newCol);
    }
        else
    {
        int dx = 0;
        int dy = 0;

        if (DeterministicRNG::next(0, 1) == 1)
        {
            dx = DeterministicRNG::next(-1, 1);
        }
        else
        {
            dy = DeterministicRNG::next(-1, 1);
        }

        int tryRow = newRow + dy;
        int tryCol = newCol + dx;

        if (tryRow >= 0 && tryRow < GRID_ROWS &&
            tryCol >= 0 && tryCol < GRID_COLS)
        {
            int tile = static_cast<int>(s.tileMap[tryRow][tryCol]);

            if (tile == static_cast<int>(EntityType::EMPTY_CELL))
            {
                newRow = tryRow;
                newCol = tryCol;
            }
            else if (tile >= 0 && tile < g_activeSpawnerCount)
            {
                if (s.entities[tile].entityType == EntityType::Lava)
                {
                    newRow = tryRow;
                    newCol = tryCol;
                }
            }
        }
    }
    if (destroyEntityIfFallsIntoLava(entityIndex, newRow, newCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    moveAndRedrawEntity(entityIndex, newRow, newCol);
    handleUnknownEntityType(entityIndex);
}

static constexpr std::array<EntityHandler, 60> ENTITY_HANDLERS = {{
    handlePushableBrick,    // 0
    handleSquareArrowUp,        // 1
    handleSquareArrowDown,      // 2
    handleSquareArrowLeft,      // 3
    handleSquareArrowRight,     // 4
    handleMagnetVertical,       // 5
    handleMagnetHorizontal,     // 6
    handlePusherUp,             // 7
    handlePusherDown,           // 8
    handlePusherLeft,           // 9
    handlePusherRight,          // 10
    handleRoundedArrowUp,        // 11
    handleRoundedArrowDown,      // 12
    handleRoundedArrowLeft,      // 13
    handleRoundedArrowRight,     // 14
    handleMonsterEntityType,    // 15
    handleMonsterEntityType,    // 16
    handleMonsterEntityType,    // 17
    handleMonsterEntityType,    // 18
    handleMonsterEntityType,    // 19
    handlePushableBrick,   // 20
    handlePushableBrick,   // 21
    handlePushableBrick,   // 22
    handlePushableBrick,   // 23
    handlePushableBrick,   // 24
    handlePushableBrick,   // 25
    handleUnknownEntityType,   // 26
    handleUnknownEntityType,   // 27
    handleUnknownEntityType,   // 28
    handleUnknownEntityType,   // 29
    handleUnknownEntityType,   // 30
    handleUnknownEntityType,   // 31
    handleUnknownEntityType,   // 32
    handleUnknownEntityType,   // 33
    handleUnknownEntityType,   // 34
    handleUnknownEntityType,   // 35
    handleUnknownEntityType,   // 36
    handleUnknownEntityType,   // 37
    handleUnknownEntityType,   // 38
    handleUnknownEntityType,   // 39
    handleUnknownEntityType,   // 40
    handleUnknownEntityType,   // 41
    handleSmartEntityAlt,      // 42
    handleSmartEntityAlt,      // 43
    handleSmartEntityAlt,      // 44
    handleSmartEntityAlt,      // 45
    handleSmartEntityAlt,      // 46
    handleSmartEntityAlt,       // 47
    handleUnknownEntityType,    // 48
    handleUnknownEntityType,    // 49
    handleUnknownEntityType,    // 50
    handleUnknownEntityType,    // 51
    handleUnknownEntityType,    // 52
    handleUnknownEntityType,    // 53
    handleUnknownEntityType,    // 54
    handleUnknownEntityType,    // 55
    handleUnknownEntityType,    // 56
    handleUnknownEntityType,    // 57
    handleUnknownEntityType,    // 58
    handleUnknownEntityType     // 59
}};

void gameMainLoopTick()
{
    ++frameCounter;

    if (frameCounter > 0x7D00) {
        frameCounter = 0;
    }
    updateCountdownEntities();

    int entityIndex = 0;

    while (entityIndex < g_activeSpawnerCount)
    {
        EntityType entityType = g_gameState.entities[entityIndex].entityType;
        int type = (int)entityType;

        if (entityType <= EntityType::COUNTDOWN_9)
        {
            // std::cout
            // << "ENTITY "
            // << entityIndex
            // << " type="
            // << (int)g_gameState.entities[entityIndex].entityType
            // << " row="
            // << g_gameState.entities[entityIndex].row
            // << " col="
            // << g_gameState.entities[entityIndex].col
            // << std::endl;
            ENTITY_HANDLERS[(int)entityType](entityIndex);
        }
        else
        {
            handleUnknownEntityType(entityIndex);
        }

        ++entityIndex;
    }
}
