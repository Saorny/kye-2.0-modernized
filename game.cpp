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
bool hasLevelList = false;
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
        remainingDiamondCount = 0;
        return true;
    }

    return false;
}

static inline bool isFixedTile(EntityType type)
{
    return type >= EntityType::TOP_RIGHT_ROUNDED_WALL && type <= EntityType::BOTTOM_LEFT_ROUND_WALL;
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

void moveAndRedrawEntity(int entityIndex, int newRow, int newCol)
{
    auto& e = g_gameState.entities[entityIndex];

    // clear old
    g_gameState.tileMap[e.row][e.col] = EntityType::EMPTY_CELL;

    // move
    e.row = newRow;
    e.col = newCol;

    // set new
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

bool tryMoveMagnet(int entityIndex)
{
    auto& e = g_gameState.entities[entityIndex];
    const int row = e.row;
    const int col = e.col;

    // --- 1️⃣ RIGHT ---
    if (g_gameState.tileMap[row][col + 1] == EntityType::EMPTY_CELL)
    {
        int target = g_gameState.rightEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == EntityType::MagnetVertical)
        {
            moveAndRedrawEntity(entityIndex, row, col + 1);
            return true;
        }
    }

    // --- 2️⃣ LEFT ---
    if (g_gameState.tileMap[row][col - 1] == EntityType::EMPTY_CELL)
    {
        int target = g_gameState.leftEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == EntityType::MagnetVertical)
        {
            moveAndRedrawEntity(entityIndex, row, col - 1);
            return true;
        }
    }

    // --- 3️⃣ DOWN ---
    if (g_gameState.tileMap[row + 1][col] == EntityType::EMPTY_CELL)
    {
        int target = g_gameState.bottomEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == EntityType::MagnetHorizontal)
        {
            moveAndRedrawEntity(entityIndex, row + 1, col);
            return true;
        }
    }

    // --- 4️⃣ UP ---
    if (g_gameState.tileMap[row - 1][col] == EntityType::EMPTY_CELL)
    {
        int target = g_gameState.topEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == EntityType::MagnetHorizontal)
        {
            moveAndRedrawEntity(entityIndex, row - 1, col);
            return true;
        }
    }

    return false;
}

int canEntityMoveMagnet(int entityIndex)
{
    auto& e = g_gameState.entities[entityIndex];
    const int row = e.row;
    const int col = e.col;

    // --- Right ---
    {
        int target = g_gameState.rightEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == EntityType::MagnetVertical)
            return 1;
    }

    // --- Left ---
    {
        int target = g_gameState.leftEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == EntityType::MagnetVertical)
            return 1;
    }

    // --- Down ---
    {
        int target = g_gameState.bottomEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == EntityType::MagnetHorizontal)
            return 1;
    }

    // --- Up ---
    {
        int target = g_gameState.topEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == EntityType::MagnetHorizontal)
            return 1;
    }

    return 0;
}

bool checkAndHandleDeathCondition(int changeIndex)
{
    const EntityInfo& entry = g_gameState.entities[changeIndex];

    int row = entry.row;
    int col = entry.col;

    bool death =
        g_gameState.leftEntityMap[row][col]   == -2 ||
        g_gameState.rightEntityMap[row][col]  == -2 ||
        g_gameState.topEntityMap[row][col]    == -2 ||
        g_gameState.bottomEntityMap[row][col] == -2;

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

    previousRow = kyeCol;
    previousCol = kyeRow;

    int leftX   = kyeCol - 1;
    int bottomY = kyeRow + 1;
    int rightX  = kyeCol + 1;
    int topY    = kyeRow - 1;

    int scanLength = 2;
    int iteration  = 1;

    while (iteration < 5)
    {
        int x = leftX;
        int y = bottomY;

        for (int i = 0; i < scanLength; ++i)
        {
            previousRow = x;
            previousCol = y;

            if (g_gameState.tileMap[x][y] == EntityType::EMPTY_CELL)
                return;

            y++;
        }

        x = rightX;
        y = bottomY;

        for (int i = 0; i < scanLength; ++i)
        {
            previousRow = x;
            previousCol = y;

            if (g_gameState.tileMap[x][y] == EntityType::EMPTY_CELL)
                return;

            x--;
        }

        x = rightX;
        y = topY;

        for (int i = 0; i < scanLength; ++i)
        {
            previousRow = x;
            previousCol = y;

            if (g_gameState.tileMap[x][y] == EntityType::EMPTY_CELL)
                return;

            y--;
        }

        x = leftX;
        y = topY;

        for (int i = 0; i < scanLength; ++i)
        {
            previousRow = x;
            previousCol = y;

            if (g_gameState.tileMap[x][y] == EntityType::EMPTY_CELL)
                return;

            x++;
        }

        leftX--;
        bottomY++;
        rightX++;
        topY--;
        scanLength += 2;
        iteration++;
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

    hasLevelList          = false;
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

static inline void safeCopy(char* dst, std::size_t dstCap, const char* src)
{
    if (!dst || dstCap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    std::snprintf(dst, dstCap, "%s", src);
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

    g_gameState.entities[index].entityType = entityCode;
    g_gameState.entities[index].row = row;
    g_gameState.entities[index].col = col;
    g_gameState.entities[index].animFrame = 0;

    // 🔥 CRITIQUE
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

void loadLevelRow(int row, const char* lineData)
{
    if (row >= GRID_ROWS) {
        cout << "Row exeeding..." << row << endl;
        return;
    }
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
                case 0: // empty
                    // cout << "Empty (" << row << ";" << col << ") =" << tileCode << endl;
                    g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;
                    break;

                case 1:
                {
                    cout << "FIXED TILE (" << row << ";" << col << ") char="
                        << inputChar << " code=" << (int)entityCode << endl;

                    g_gameState.tileMap[row][col] = entityCode;
                    break;
                }
                case 2: // entity
                {
                    cout << "Mobile entity (" << row << ";" << col << ") =" << (int)entityCode << endl;
                    int changeIndex =
                        addEntity(entityCode, row, col);

                    if (changeIndex >= 0)
                    {
                        uint16_t rnd = pseudoRandomUpdate(0x8000);

                        uint16_t anim = divide64_unsigned(rnd);

                        g_gameState.entities[changeIndex].animFrame = anim;
                    }
                    break;
                }

                case 3: // player spawn
                {
                    g_gameState.tileMap[row][col] = EntityType::KYE_LOCATION;
                    cout << "setting Kye (" << row << ";" << col << ") =" << (int)entityCode << endl;
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
    remainingDiamondCount = 0;

    for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++)
    {
        if (g_gameState.tileMap[r][c] == EntityType::DIAMOND)
            remainingDiamondCount++;
    }

    bool playerFound = false;

    for (int r = 0; r < GRID_ROWS; r++)
    for (int c = 0; c < GRID_COLS; c++)
    {
        if (g_gameState.tileMap[r][c] == EntityType::KYE_LOCATION)
        {
            currentRow = r;
            currentCol = c;
            playerFound = true;
        }
    }

    if (!playerFound)
    {
        cout << "Not found, resetting Kye location..." << endl;
        currentRow = 3;
        currentCol = 3;
        kyeRow = 3;
        kyeCol = 3;
        selectedTileValue = EntityType::EMPTY;
    }

    finalizeLevelVisuals();
    return 0;
}

int destroyEntityIfFallsIntoLava(int index, int newRow, int newCol)
{
    int tile = (int)g_gameState.tileMap[newRow][newCol];

    if (tile < 0)
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

                for (int row = 0; row < GRID_ROWS; ++row)
                {
                    for (int col = 0; col < GRID_COLS; ++col)
                    {
                        auto& tile = g_gameState.tileMap[row][col];
                        int value = static_cast<int>(tile);

                        if (value > i)
                        {
                            tile = static_cast<EntityType>(value - 1);
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

    if (g_interactionMode != GameInteractionMode::NormalPlay)
        return;

    initializeRendererIfNeeded();
    advanceToNextLevelOrBlock();
}

int handlePendingBlock(int rowIndex, int colIndex)
{
    int row = rowIndex;
    int col = colIndex;

    if (isPendingDraw &&
        pendingRow == row &&
        pendingCol == col)
    {
        return 0;
    }

    maybeDrawPendingRectangle();

    pendingRow = row;
    pendingCol = col;

    int dRow = std::abs(row - currentRow);
    if (dRow <= 1)
    {
        int dCol = std::abs(col - currentCol);
        if (dCol <= 1)
        {
            return 0;
        }
    }

    if (g_gameState.tileMap[pendingRow][pendingCol] == EntityType::KYE_LOCATION)
    {
        drawPendingBlock();
        isPendingDraw = 1;
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

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    if (s.leftEntityMap[row][col] == -1 &&
        s.entityToLeft[row][col] == (int16_t)0xFFFE)
    {
        moveAndRedrawEntity(entityIndex, row, col + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (s.rightEntityMap[row][col] == -1 &&
        s.entityToRight[row][col] == (int16_t)0xFFFE)
    {
        moveAndRedrawEntity(entityIndex, row, col - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (s.bottomEntityMap[row][col] == -1)
    {
        int auxIndex = s.entityBelow[row][col];

        if (auxIndex >= 0 &&
            s.entities[auxIndex].entityType == EntityType::MagnetHorizontal)
        {
            moveAndRedrawEntity(entityIndex, row + 1, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    if (s.topEntityMap[row][col] == -1)
    {
        int auxIndex = s.entityAbove[row][col];

        if (auxIndex >= 0 &&
            s.entities[auxIndex].entityType == EntityType::MagnetHorizontal)
        {
            moveAndRedrawEntity(entityIndex, row - 1, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleMagnetHorizontal(int entityIndex)
{
    auto& s = g_gameState;

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    // case 1
    if (s.bottomEntityMap[row][col] == -1 &&
        s.entityBelow[row][col] == static_cast<int16_t>(EntityType::KYE_LOCATION))
    {
        moveAndRedrawEntity(entityIndex, row + 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    // case 2
    if (s.topEntityMap[row][col] == -1 &&
        s.entityBelow[row][col] == static_cast<int16_t>(EntityType::KYE_LOCATION))
    {
        moveAndRedrawEntity(entityIndex, row - 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    // case 3
    if (s.leftEntityMap[row][col] == -1)
    {
        int auxIndex = s.entityToLeft[row][col];

        if (auxIndex >= 0 &&
            s.entities[auxIndex].entityType == EntityType::MagnetVertical)
        {
            moveAndRedrawEntity(entityIndex, row, col + 1);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    // case 4
    if (s.rightEntityMap[row][col] == -1)
    {
        int auxIndex = s.entityToRight[row][col];

        if (auxIndex >= 0 &&
            s.entities[auxIndex].entityType == EntityType::MagnetVertical)
        {
            moveAndRedrawEntity(entityIndex, row, col - 1);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void adjustSmartEntityTarget(int entityIndex,
                             int srcRow,
                             int srcCol,
                             int* targetRow,
                             int* targetCol)
{
    int curRow = g_gameState.entities[entityIndex].row;
    int curCol = g_gameState.entities[entityIndex].col;

    if (targetRow) *targetRow = curRow;
    if (targetCol) *targetCol = curCol;

    int dy = 0;
    if (srcRow > curRow)      dy =  1;
    else if (srcRow < curRow) dy = -1;

    int dx = 0;
    if (srcCol > curCol)      dx =  1;
    else if (srcCol < curCol) dx = -1;

    if (dy != 0 && dx != 0) {
        auto isEmpty = [](int row, int col) -> bool {
            if (row < 0 || row >= GRID_ROWS ||
                col < 0 || col >= GRID_COLS)
                return false;
            return g_gameState.auxTopRightEntityMap[row][col] == (int)EntityType::EMPTY_CELL;
        };

        bool verticalEmpty   = isEmpty(curRow + dy, curCol);
        bool horizontalEmpty = isEmpty(curRow,       curCol + dx);

        if (verticalEmpty && horizontalEmpty) {
            dy = 0;
        } else if (verticalEmpty) {
            dx = 0;
        } else if (horizontalEmpty) {
            dy = 0;
        } else {
            dy = 0;
        }
    }

    int candidateRow = curRow + dy;
    int candidateCol = curCol + dx;

    auto isEmpty = [](int row, int col) -> bool {
        if (row < 0 || row >= GRID_ROWS ||
            col < 0 || col >= GRID_COLS)
            return false;
        return g_gameState.auxTopRightEntityMap[row][col] == (int)EntityType::EMPTY_CELL;
    };

    if (isEmpty(candidateRow, candidateCol)) {
        if (targetRow) *targetRow = candidateRow;
        if (targetCol) *targetCol = candidateCol;
    }
}

static inline std::int16_t tileRandomCoord16()
{
  const uint16_t r = pseudoRandomUpdate();
  return static_cast<std::int16_t>((r >> 11) & 0x000F);
}

int16_t handleGameClick(int16_t x, int16_t y)
{
    const int16_t rawX = x;
    const int16_t rawY = y;

    if (!isPointInRect(x, y)) {
        return 0;
    }

    switch (g_interactionMode) {
        case GameInteractionMode::PendingBlock: {
            advanceToNextLevelOrBlock();
            const int16_t rowIndex = static_cast<int16_t>(rawX / cellHeight);
            const int16_t colIndex = static_cast<int16_t>(rawY / cellWidth);

            handlePendingBlock(rowIndex, colIndex);

            previousRow = rowIndex;
            previousCol = colIndex;
            g_hasPendingModal = 1;
            matchedEntryCount  = 1;

            return 0;
        }

        case GameInteractionMode::NormalPlay: {
            const int16_t rowIndex = static_cast<int16_t>(rawX / cellHeight);
            const int16_t colIndex = static_cast<int16_t>(rawY / cellWidth);
            uint32_t cellId = getCellId(rowIndex, colIndex);

            handleClickOnGridCell(cellId);
            initializeRendererIfNeeded();
            updateGridCell(rowIndex, colIndex);
            renderLivesAndLevelInfo();
            releaseDialogResources();
            return 0;
        }

        default:
            return 0;
    }
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

    snprintf(buffer, sizeof(buffer), "Diamonds left: %d", remainingDiamondCount);
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

void handleClickOnGridCell(std::uint32_t cellId)
{
    // const std::int16_t row = static_cast<std::int16_t>(cellId & 0xFFFF);
    // const std::int16_t col = static_cast<std::int16_t>((cellId >> 16) & 0xFFFF);

    // if (g_gameState.tileMap[row][col] == EntityType::EMPTY_CELL)
    // {
    //     const std::int16_t index = static_cast<std::int16_t>(g_currentNameIndex);
    //     const std::int16_t stride = 0x1A / 2;

    //     extern std::int16_t g_entryRowTable[];
    //     extern std::int16_t g_entryColTable[];
    //     extern std::int16_t g_entryEnabledTable[];

    //     const std::int16_t entryBase = static_cast<std::int16_t>(index * stride);

    //     if (g_entryEnabledTable[index] != 0)
    //     {
    //         executeCurrentEntryAction(
    //             g_entryRowTable[entryBase],
    //             g_entryColTable[entryBase],
    //             row,
    //             col
    //         );
    //         return;
    //     }
    // }

    // handleStandardCellClick(row, col);
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
            if (type >= EntityType::TOP_RIGHT_ROUNDED_WALL && type <= EntityType::BOTTOM_LEFT_ROUND_WALL)
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

    if (g_interactionMode == GameInteractionMode::NormalPlay)
    {
        g_hasPendingModal = 0;
        remainingDiamondCount = 0;

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
            remainingDiamondCount = 0;
            clearStatusLine(g_statusLineBuffer);
            return true;
        }

        case MenuCommand::Restart:
        {
            loadLevelByIndex(levelIndex);
            remainingDiamondCount = 0;
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
            remainingDiamondCount = 0;
            return true;
        }

        case MenuCommand::EnterEditMode:
        {
            g_interactionMode = GameInteractionMode::PendingBlock;
            g_timerActive = false;
            showToolboxWindowAndRefreshHUD();
            return true;
        }

        case MenuCommand::ExitEditMode:
        {
            g_interactionMode = GameInteractionMode::NormalPlay;
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

    if (g_interactionMode != GameInteractionMode::NormalPlay) {
        return;
    }

    if (matchedEntryCount != 0) {
        matchedEntryCount = 0;
        maybeDrawPendingRectangle();
        return;
    }

    int rowIndex = localX / cellHeight;
    int colIndex = localY / cellWidth;

    handlePendingBlock(rowIndex, colIndex);

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

    if (g_interactionMode != GameInteractionMode::NormalPlay)
    {
        SDL_SetCursor(g_cursorArrow);
        return;
    }

    SDL_SetCursor(g_cursorArrow);

    const int rowIndex = x / cellHeight;
    const int colIndex = y / cellWidth;

    if (remainingDiamondCount == 0)
        return;

    handlePendingBlock(rowIndex, colIndex);

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
                entityType = EntityType::ArrowRight;
                targetRow = row + 1;
                break;

            case EntityType::UnknownA2:
                entityType = EntityType::ARROW_UP;
                targetCol = col - 1;
                break;

            case EntityType::UnknownA3:
                entityType = EntityType::ArrowLeft;
                targetRow = row - 1;
                break;

            case EntityType::UnknownA4:
                entityType = EntityType::ARROW_DOWN;
                targetCol = col + 1;
                break;

            case EntityType::DISPENSER1:
                entityType = EntityType::CURVED_ARROW_RIGHT;
                targetRow = row + 1;
                break;

            case EntityType::DISPENSER2:
                entityType = EntityType::CURVED_ARROW_UP;
                targetCol = col - 1;
                break;

            case EntityType::DISPENSER3:
                entityType = EntityType::CURVED_ARROW_LEFT;
                targetRow = row - 1;
                break;

            case EntityType::DISPENSER4:
                entityType = EntityType::CURVED_ARROW_DOWN;
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
    if (g_interactionMode != GameInteractionMode::NormalPlay)
        return;

    switch (key)
    {
        // Flèches
        case SDLK_LEFT:
            previousRow = currentRow;
            previousCol = currentCol - 1;
            break;

        case SDLK_RIGHT:
            previousRow = currentRow;
            previousCol = currentCol + 1;
            break;

        case SDLK_UP:
            previousRow = currentRow - 1;
            previousCol = currentCol;
            break;

        case SDLK_DOWN:
            previousRow = currentRow + 1;
            previousCol = currentCol;
            break;

        // Pavé numérique (diagonales)
        case SDLK_KP_7: // up-left
            previousRow = currentRow - 1;
            previousCol = currentCol - 1;
            break;

        case SDLK_KP_1: // down-left
            previousRow = currentRow - 1;
            previousCol = currentCol + 1;
            break;

        case SDLK_KP_9: // up-right
            previousRow = currentRow + 1;
            previousCol = currentCol - 1;
            break;

        case SDLK_KP_3: // down-right
            previousRow = currentRow + 1;
            previousCol = currentCol + 1;
            break;

        default:
            return;
    }

    g_hasPendingModal = 1;
    tickLevelFlow();
    // updateLevelVisualsAndAnimations();
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
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Level Complete",
        "Level completed!",
        g_window
    );

    return 0;
}

int showGameOverDialog()
{
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Game Over",
        "You have lost all your lives!",
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

    if (hasLevelList != 0)
    {
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

        remainingDiamondCount = 0;

        clearStatusLine((const char*)0x29FA);

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
            remainingDiamondCount = 0;
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
            destroyGraphicsResources();
            cleanupAndExit(0);
            return;

        case SDL_EVENT_WINDOW_EXPOSED:
        case SDL_EVENT_WINDOW_SHOWN:
            handlePaintOrRenderRequest();
            return;

        case SDL_EVENT_KEY_DOWN:
            g_keyDownFlag = 1;
            handleDirectionalHotkeyAndAdvanceLevel(e.key.key);
            return;

        case SDL_EVENT_KEY_UP:
            g_keyDownFlag = 0;
            return;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        {
            int x = e.button.x;
            int y = e.button.y;

            if (e.button.button == SDL_BUTTON_LEFT)
            {
                handleGameClick(x, y);
            }
            else if (e.button.button == SDL_BUTTON_RIGHT)
            {
                cancelPendingInteraction();
            }

            return;
        }

        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            int x = e.button.x;
            int y = e.button.y;

            handlePendingInteractionFinalize(x, y);
            return;
        }

        case SDL_EVENT_MOUSE_MOTION:
        {
            int x = e.motion.x;
            int y = e.motion.y;

            handlePointClick(x, y);
            return;
        }

        case SDL_EVENT_WINDOW_RESIZED:
            handlePendingInteractionClick(
                e.window.data1,
                e.window.data2
            );
            return;

        default:
            break;
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

int processKyeCollision(int stepRow, int stepCol)
{
    if ((stepRow != 0 && stepCol != 0) || (stepRow == 0 && stepCol == 0))
        return 0;

    int newRow = currentRow + stepRow;
    int newCol = currentCol + stepCol;

    EntityType target = g_gameState.tileMap[newRow][newCol];

    if (target == EntityType::EMPTY_CELL)
    {
        advanceKyeAndCarryTile(stepRow, stepCol);
        return 1;
    }

    if (target == EntityType::BREAKABLE_BRICK)
    {
        advanceKyeAndCarryTile(stepRow, stepCol);
        selectedObjectIndex = 0xFFFF;
        return 1;
    }

    if (target == EntityType::DIAMOND)
    {
        advanceKyeAndCarryTile(stepRow, stepCol);
        selectedObjectIndex = 0xFFFF;
        --remainingDiamondCount;
        renderLivesAndLevelInfo();
        return 1;
    }

    if ((int)target >= 0 && (int)target < g_gameState.entities.size())
    {
        int entityIndex = (int)target;
        EntityInfo& e = g_gameState.entities[entityIndex];

        if (e.entityType == EntityType::Lava)
        {
            --remainingLives;
            updateLivesDisplay();
            renderLivesAndLevelInfo();
            return 0;
        }

        if (e.entityType == EntityType::EMPTY_CELL)
            return 0;

        int pushRow = e.row + stepRow;
        int pushCol = e.col + stepCol;

        if (pushRow < 0 || pushRow >= GRID_ROWS ||
            pushCol < 0 || pushCol >= GRID_COLS)
            return 0;

        EntityType pushTarget = g_gameState.tileMap[pushRow][pushCol];

        if (pushTarget == EntityType::EMPTY_CELL)
        {
            moveAndRedrawEntity(entityIndex, pushRow, pushCol);
            advanceKyeAndCarryTile(stepRow, stepCol);
            return 1;
        }

        if ((int)pushTarget >= 0 && (int)pushTarget < g_activeSpawnerCount)
        {
            int targetEntityIndex = (int)pushTarget;
            EntityInfo& targetEntity = g_gameState.entities[targetEntityIndex];

            if (targetEntity.entityType == EntityType::Lava)
            {
                if (!destroyEntityIfFallsIntoLava(entityIndex, pushRow, pushCol))
                {
                    moveAndRedrawEntity(entityIndex, pushRow, pushCol);
                }

                advanceKyeAndCarryTile(stepRow, stepCol);
                return 1;
            }
        }

        return 0;
    }

    return 0;
}

// --- Common handler for “smart” mobile entities (type 0, 20–26, etc.) ---
void handleSmartEntityCommon(int entityIndex) {
    if (!tryMoveMagnet(entityIndex))
        return;
}

// --- Alternative handler for “smart” entities (type 50–59) ---
void handleSmartEntityAlt(int entityIndex) {
    if (!tryMoveMagnet(entityIndex))
        return;
}

void handleArrowUp(int entityIndex)
{
    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // auto& e = g_gameState.entities[entityIndex];

    // int row = e.row;
    // int col = e.col;

    // int targetIndex = g_gameState.rightEntityMap[row][col];

    // // --- case empty
    // if (targetIndex == -1) // ou EMPTY_CELL selon ton mapping
    // {
    //     moveAndRedrawEntity(entityIndex, row, col - 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // // --- replace entity
    // if (replaceEntityIfTargetMatches(entityIndex, row, col - 1) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // // --- check entity type
    // if (targetIndex >= 0 && targetIndex < g_activeSpawnerCount)
    // {
    //     EntityInfo& t = g_gameState.entities[targetIndex];

    //     if (t.entityType == EntityType::DEFLECTOR_RIGHT)
    //     {
    //         e.entityType = EntityType::ArrowRight;
    //         handleUnknownEntityType(entityIndex);
    //         return;
    //     }

    //     if (t.entityType == EntityType::DEFLECTOR_LEFT)
    //     {
    //         e.entityType = EntityType::ArrowLeft;
    //         handleUnknownEntityType(entityIndex);
    //         return;
    //     }
    // }

    // handleUnknownEntityType(entityIndex);
}

void handleArrowDown(int entityIndex)
{
    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int newRow = pendingRow;
    // const int newCol = pendingCol;

    // const EntityType targetIndex = g_gameState.leftEntityMap[newRow][newCol];

    // if (targetIndex == EntityType::EMPTY_CELL)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow, newCol + 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol + 1) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (targetIndex >= EntityType::EMPTY_CELL)
    // {
    //     const EntityType targetType =
    //         g_gameState.entities(targetIndex).entityType;

    //     if (targetType == EntityType::DEFLECTOR_LEFT)
    //     {
    //         g_gameState.entities[entityIndex].entityType = EntityType::ArrowLeft;
    //         moveAndRedrawEntity(entityIndex, newRow, newCol);
    //         handleUnknownEntityType(entityIndex);
    //         return;
    //     }

    //     if (targetType == EntityType::DEFLECTOR_RIGHT)
    //     {
    //         g_gameState.entities[entityIndex].entityType = EntityType::ArrowRight;
    //         moveAndRedrawEntity(entityIndex, newRow, newCol);
    //         handleUnknownEntityType(entityIndex);
    //         return;
    //     }
    // }

    // handleUnknownEntityType(entityIndex);
}

void handleArrowLeft(int entityIndex)
{
    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int newRow = pendingRow;
    // const int newCol = pendingCol;

    // const EntityType targetIndex = g_gameState.topEntityMap[newRow][newCol];

    // if (targetIndex == EntityType::EMPTY_CELL)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow - 1, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (replaceEntityIfTargetMatches(entityIndex, newRow - 1, newCol) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (targetIndex >= EntityType::EMPTY_CELL)
    // {
    //     const EntityType targetType = g_gameState.entities[targetIndex].entityType;

    //     if (targetType == EntityType::DEFLECTOR_LEFT)
    //     {
    //         g_gameState.entities[entityIndex].entityType = EntityType::ARROW_UP;
    //         moveAndRedrawEntity(entityIndex, newRow, newCol);
    //         handleUnknownEntityType(entityIndex);
    //         return;
    //     }

    //     if (targetType == EntityType::DEFLECTOR_RIGHT)
    //     {
    //         g_gameState.entities[entityIndex].entityType = EntityType::ARROW_DOWN;
    //         moveAndRedrawEntity(entityIndex, newRow, newCol);
    //         handleUnknownEntityType(entityIndex);
    //         return;
    //     }
    // }

    // handleUnknownEntityType(entityIndex);
}

void handleArrowRight(int entityIndex)
{
    // auto& state = g_gameState;

    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // int row = state.entities[entityIndex].row;
    // int col = state.entities[entityIndex].col;

    // EntityType belowIndex = state.bottomEntityMap[row][col];

    // if (belowIndex == EntityType::EMPTY_CELL)
    // {
    //     moveAndRedrawEntity(entityIndex, row + 1, col);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }
    // if (replaceEntityIfTargetMatches(entityIndex, row + 1, col) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (belowIndex < EntityType::EMPTY_CELL)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // EntityType targetState = g_gameState.entities[belowIndex].entityType;

    // if (targetState == EntityType::DEFLECTOR_LEFT)
    // {
    //     state.entities[entityIndex].entityType = EntityType::ARROW_DOWN;
    //     moveAndRedrawEntity(entityIndex, row, col);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (targetState == EntityType::DEFLECTOR_RIGHT)
    // {
    //     state.entities[entityIndex].entityType = EntityType::ARROW_UP;
    //     moveAndRedrawEntity(entityIndex, row, col);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // handleUnknownEntityType(entityIndex);
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

    if (tryMoveMagnet(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMoveMagnet(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int left = s.leftEntityMap[row][col];

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

    left = s.leftEntityMap[row][col];

    if (left < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    auto& target = s.entities[left];

    int targetRow = target.row;
    int targetCol = target.col - 1;

    EntityType tile = s.tileMap[targetRow][targetCol];

    if (tile != EntityType::EMPTY_CELL)
    {
        if ((int)tile < 0 ||
            s.entities[(int)tile].entityType != EntityType::Lava)
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

static bool legacyRandomBit()
{
    pseudoRandomUpdate(0x8000);

    const std::uint64_t value =
        (static_cast<std::uint64_t>(speedMultiplierHigh) << 16) |
         static_cast<std::uint64_t>(speedMultiplierLow);

    return (value & 1ULL) != 0;
}

void handlePusherUp(int entityIndex)
{
    // auto& state = g_gameState;

    // if ((frameCounter % 5) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // int newRow = state.entities[entityIndex].row;
    // int newCol = state.entities[entityIndex].col;

    // EntityType leftIndex = state.leftEntityMap[newRow][newCol];

    // if (leftIndex == EntityType::EMPTY)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow, newCol - 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol - 1) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // state.entities[entityIndex].entityType = EntityType::PUSHER_DOWN;

    // moveAndRedrawEntity(entityIndex, newRow, newCol);

    // leftIndex = state.leftEntityMap[newRow][newCol];

    // if (leftIndex < EntityType::EMPTY_CELL)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // auto& target = state.entities[leftIndex];

    // int targetRow = target.row;
    // int targetCol = target.col - 1;

    // EntityType auxIndex = state.rightEntityMap[targetRow][targetCol];

    // if (auxIndex != EntityType::EMPTY)
    // {
    //     EntityType auxAction = state.entities[auxIndex].entityType;

    //     if (auxAction != EntityType::Lava)
    //     {
    //         handleUnknownEntityType(entityIndex);
    //         return;
    //     }
    // }

    // if (replaceEntityIfTargetMatches(leftIndex, targetRow, targetCol) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // moveAndRedrawEntity(leftIndex, targetRow, targetCol);

    // handleUnknownEntityType(entityIndex);
}


void handlePusherDown(int entityIndex)
{
    auto& state = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryMoveMagnet(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMoveMagnet(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int newRow = state.entities[entityIndex].row;
    int newCol = state.entities[entityIndex].col;

    int rightIndex = state.rightEntityMap[newRow][newCol];

    if (rightIndex == -1)
    {
        moveAndRedrawEntity(entityIndex, newRow, newCol + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (destroyEntityIfFallsIntoLava(entityIndex, newRow, newCol + 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    state.entities[entityIndex].entityType = EntityType::PUSHER_UP;

    moveAndRedrawEntity(entityIndex, newRow, newCol);

    rightIndex = state.rightEntityMap[newRow][newCol];

    if (rightIndex < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    auto& target = state.entities[rightIndex];

    int targetRow = target.row;
    int targetCol = target.col + 1;

    int auxIndex = state.rightEntityMap[targetRow][targetCol];

    if (auxIndex != -1)
    {
        EntityType auxAction = state.entities[auxIndex].entityType;

        if (auxAction != EntityType::Lava)
        {
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    if (destroyEntityIfFallsIntoLava(rightIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    moveAndRedrawEntity(rightIndex, targetRow, targetCol);

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

    if (tryMoveMagnet(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMoveMagnet(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = s.entities[entityIndex].row;
    int col = s.entities[entityIndex].col;

    int right = s.rightEntityMap[row][col];

    if (right == -1)
    {
        if (s.tileMap[row][col + 1] != EntityType::EMPTY_CELL)
        {
            s.entities[entityIndex].entityType = EntityType::PUSHER_LEFT;
            moveAndRedrawEntity(entityIndex, row, col);

            right = s.rightEntityMap[row][col];
            if (right >= 0)
            {
                int targetRow = s.entities[right].row;
                int targetCol = s.entities[right].col + 1;
                EntityType tile = s.tileMap[targetRow][targetCol];

                if (tile == EntityType::EMPTY_CELL ||
                    ((int)tile >= 0 && s.entities[(int)tile].entityType == EntityType::Lava))
                {
                    if (destroyEntityIfFallsIntoLava(right, targetRow, targetCol) == 0)
                    {
                        moveAndRedrawEntity(right, targetRow, targetCol);
                    }
                }
            }

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

    int targetRow = s.entities[right].row;
    int targetCol = s.entities[right].col + 1;
    EntityType tile = s.tileMap[targetRow][targetCol];

    if (tile != EntityType::EMPTY_CELL)
    {
        if ((int)tile < 0 || s.entities[(int)tile].entityType != EntityType::Lava)
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

void handleCurvedArrowUp(int entityIndex)
{
    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int newRow = pendingRow;
    // const int newCol = pendingCol;

    // const int leftIndex  = g_gameState.rightEntityMap[newRow][newCol];
    // const int downIndex  = g_gameState.bottomEntityMap[newRow][newCol];
    // const int upIndex    = g_gameState.topEntityMap[newRow][newCol];
    // const int diagIndex  = g_gameState.auxTopRightEntityMap[newRow][newCol];

    // if (leftIndex == EntityType::EMPTY_CELL)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow, newCol - 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol - 1) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // bool allowDown =
    //     (downIndex == CELL_FLAG_EMPTY &&
    //      upIndex == CELL_FLAG_EMPTY);

    // bool allowUp =
    //     (downIndex == CELL_FLAG_EMPTY &&
    //      diagIndex == CELL_FLAG_EMPTY);

    // if (allowDown && allowUp)
    // {
    //     const bool goDown = legacyRandomBit();

    //     const int targetRow = goDown
    //         ? (newRow + 1)
    //         : (newRow - 1);

    //     moveAndRedrawEntity(entityIndex, targetRow, newCol - 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (allowDown)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow + 1, newCol - 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (allowUp)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow - 1, newCol - 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (leftIndex >= 0 &&
    //     g_gameState.entities[leftIndex].entityType == EntityType::DEFLECTOR_RIGHT)
    // {
    //     g_gameState.entities[entityIndex].entityType = EntityType::CURVED_ARROW_RIGHT;

    //     moveAndRedrawEntity(entityIndex, newRow, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (leftIndex >= 0 &&
    //     g_gameState.entities[leftIndex].entityType == EntityType::DEFLECTOR_LEFT)
    // {
    //     g_gameState.entities[entityIndex].entityType = EntityType::CURVED_ARROW_LEFT;

    //     moveAndRedrawEntity(entityIndex, newRow, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // handleUnknownEntityType(entityIndex);
}

void handleCurvedArrowDown(int entityIndex)
{
    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int newRow = pendingRow;
    // const int newCol = pendingCol;

    // const int rightIndex = g_gameState.leftEntityMap[newRow][newCol];

    // if (rightIndex == 0x0000)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow, newCol + 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol + 1) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int upIndex    = g_gameState.topEntityMap[newRow][newCol];
    // const int downIndex  = g_gameState.bottomEntityMap[newRow][newCol];
    // const int diagUp     = g_gameState.auxTopRightEntityMap[newRow][newCol];
    // const int diagDown   = g_gameState.auxBottomRightEntityMap[newRow][newCol];

    // const bool allowUp =
    //     (upIndex == 0x0000 && diagUp == CELL_FLAG_EMPTY);

    // const bool allowDown =
    //     (downIndex == CELL_FLAG_EMPTY && diagDown == CELL_FLAG_EMPTY);

    // if (allowUp && allowDown)
    // {
    //     const bool goDown = legacyRandomBit();
    //     const int targetRow = goDown ? (newRow + 1)
    //                                  : (newRow - 1);

    //     moveAndRedrawEntity(entityIndex, targetRow, newCol + 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (allowUp)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow - 1, newCol + 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (allowDown)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow + 1, newCol + 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (rightIndex >= 0 &&
    //     g_gameState.entities[rightIndex].entityType == EntityType::DEFLECTOR_RIGHT)
    // {
    //     g_gameState.entities[entityIndex].entityType = EntityType::CURVED_ARROW_LEFT;

    //     moveAndRedrawEntity(entityIndex, newRow, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (rightIndex >= 0 &&
    //     g_gameState.entities[rightIndex].entityType == EntityType::DEFLECTOR_LEFT)
    // {
    //     g_gameState.entities[entityIndex].entityType = EntityType::CURVED_ARROW_RIGHT;

    //     moveAndRedrawEntity(entityIndex, newRow, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // handleUnknownEntityType(entityIndex);
}

void handleCurvedArrowLeft(int entityIndex)
{
    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int newRow = pendingRow;
    // const int newCol = pendingCol;

    // const int upIndex = g_gameState.auxTopRightEntityMap[newRow][newCol];

    // if (upIndex == 0x0000)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow - 1, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (replaceEntityIfTargetMatches(entityIndex, newRow - 1, newCol) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int leftIndex  = g_gameState.rightEntityMap[newRow][newCol];
    // const int rightIndex = g_gameState.leftEntityMap[newRow][newCol];
    // const int diagLeft   = g_gameState.auxMap127A[newRow][newCol];
    // const int diagRight  = g_gameState.auxMap1282[newRow][newCol];

    // const bool allowLeft  = (leftIndex  == CELL_FLAG_EMPTY && diagLeft  == CELL_FLAG_EMPTY);
    // const bool allowRight = (rightIndex == CELL_FLAG_EMPTY && diagRight == CELL_FLAG_EMPTY);

    // if (allowLeft && allowRight)
    // {
    //     const bool goRight = legacyRandomBit();
    //     const int targetCol = goRight ? (newCol + 1)
    //                                   : (newCol - 1);

    //     moveAndRedrawEntity(entityIndex, newRow - 1, targetCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (allowLeft)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow - 1, newCol - 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (allowRight)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow - 1, newCol + 1);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (upIndex >= 0 &&
    //     g_gameState.entities[upIndex].entityType == EntityType::DEFLECTOR_RIGHT)
    // {
    //     g_gameState.entities[entityIndex].entityType = EntityType::CURVED_ARROW_UP;

    //     moveAndRedrawEntity(entityIndex, newRow, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (upIndex >= 0 &&
    //     g_gameState.entities[upIndex].entityType == EntityType::DEFLECTOR_LEFT)
    // {
    //     g_gameState.entities[entityIndex].entityType = EntityType::CURVED_ARROW_DOWN;

    //     moveAndRedrawEntity(entityIndex, newRow, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // handleUnknownEntityType(entityIndex);
}

void handleCurvedArrowRight(int entityIndex)
{
    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int newRow = pendingRow;
    // const int newCol = pendingCol;

    // const int belowIndex =
    //     g_gameState.bottomEntityMap[newRow][newCol];

    // if (belowIndex == 0x0000)
    // {
    //     moveAndRedrawEntity(entityIndex, newRow + 1, newCol);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (replaceEntityIfTargetMatches(entityIndex, newRow + 1, newCol) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // const int leftIndex =
    //     g_gameState.leftEntityMap[newRow][newCol];

    // const int rightIndex =
    //     g_gameState.rightEntityMap[newRow][newCol];

    // const int diagLeft =
    //     g_gameState.auxTopRightEntityMap[newRow][newCol];

    // const int diagRight =
    //     g_gameState.auxBottomRightEntityMap[newRow][newCol];

    // const bool condRange =
    //     (belowIndex >= 0 &&
    //      g_gameState.entities[belowIndex].entityType >= EntityType::CURVED_ARROW_UP &&
    //      g_gameState.entities[belowIndex].entityType <= EntityType::CURVED_ARROW_RIGHT);

    // const bool condExact =
    //     (belowIndex >= 0 &&
    //      g_gameState.entities[belowIndex].entityType ==
    //      EntityType::RoundedPushableBrick);

    // bool allowDiagRight = false;

    // if (rightIndex == CELL_FLAG_EMPTY &&
    //     diagLeft  == CELL_FLAG_EMPTY)
    // {
    //     if (belowIndex == static_cast<int>(EntityType::TOP_LEFT_ROUNDED_WALL) ||
    //         belowIndex == static_cast<int>(EntityType::LEFT_ROUNDED_WALL) ||
    //         belowIndex == static_cast<int>(EntityType::TOP_ROUNDED_WALL) ||
    //         condRange ||
    //         condExact)
    //     {
    //         allowDiagRight = true;
    //     }
    // }

    // bool allowDiagLeft = false;

    // if (leftIndex == CELL_FLAG_EMPTY &&
    //     diagRight == CELL_FLAG_EMPTY)
    // {
    //     if (belowIndex == static_cast<int>(EntityType::BOTTOM_LEFT_ROUND_WALL) ||
    //         belowIndex == static_cast<int>(EntityType::LEFT_ROUNDED_WALL) ||
    //         belowIndex == static_cast<int>(EntityType::BOTTOM_ROUNDED_WALL) ||
    //         condRange ||
    //         condExact)
    //     {
    //         allowDiagLeft = true;
    //     }
    // }

    // if (allowDiagRight && allowDiagLeft)
    // {
    //     const bool goLeft = legacyRandomBit();
    //     const int targetCol =
    //         goLeft ? (newCol - 1)
    //                : (newCol + 1);

    //     moveAndRedrawEntity(entityIndex,
    //                         newRow + 1,
    //                         targetCol);

    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (allowDiagRight)
    // {
    //     moveAndRedrawEntity(entityIndex,
    //                         newRow + 1,
    //                         newCol + 1);

    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (allowDiagLeft)
    // {
    //     moveAndRedrawEntity(entityIndex,
    //                         newRow + 1,
    //                         newCol - 1);

    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (belowIndex >= 0 &&
    //     g_gameState.entities[belowIndex].entityType ==
    //     EntityType::DEFLECTOR_LEFT)
    // {
    //     g_gameState.entities[entityIndex].entityType =
    //         EntityType::CURVED_ARROW_DOWN;

    //     moveAndRedrawEntity(entityIndex,
    //                         newRow,
    //                         newCol);

    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (belowIndex >= 0 &&
    //     g_gameState.entities[belowIndex].entityType ==
    //     EntityType::DEFLECTOR_RIGHT)
    // {
    //     g_gameState.entities[entityIndex].entityType =
    //         EntityType::CURVED_ARROW_UP;

    //     moveAndRedrawEntity(entityIndex,
    //                         newRow,
    //                         newCol);

    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // handleUnknownEntityType(entityIndex);
}

int randomInt(int min, int max)
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
}

void handleMonsterEntityType(int entityIndex)
{
    // auto& s = g_gameState;

    // if (checkAndHandleDeathCondition(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if ((frameCounter % 3) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (tryMoveMagnet(entityIndex) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // if (canEntityMoveMagnet(entityIndex) != 0)
    // {
    //     int row = s.entities[entityIndex].row;
    //     int col = s.entities[entityIndex].col;

    //     if (destroyEntityIfFallsIntoLava(entityIndex, row, col) != 0)
    //     {
    //         handleUnknownEntityType(entityIndex);
    //         return;
    //     }

    //     moveAndRedrawEntity(entityIndex, row, col);
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // int newRow = s.entities[entityIndex].row;
    // int newCol = s.entities[entityIndex].col;

    // if (randomInt(0, 1) == 1)
    // {
    //     adjustSmartEntityTarget(entityIndex, currentRow, currentCol, &newRow, &newCol);
    // }
    // else
    // {
    //     int dx = randomInt(-1, 1);
    //     int dy = randomInt(-1, 1);

    //     int tryRow = newRow + dy;
    //     int tryCol = newCol + dx;

    //     if (tryRow >= 0 && tryRow < GRID_ROWS &&
    //         tryCol >= 0 && tryCol < GRID_COLS)
    //     {
    //         int tile = (int)s.tileMap[tryRow][tryCol];

    //         if (tile == (int)EntityType::EMPTY_CELL ||
    //             (tile >= 0 && tile < 256 &&
    //              s.entities[tile].entityType == EntityType::Lava))
    //         {
    //             newRow = tryRow;
    //             newCol = tryCol;
    //         }
    //     }
    // }

    // if (destroyEntityIfFallsIntoLava(entityIndex, newRow, newCol) != 0)
    // {
    //     handleUnknownEntityType(entityIndex);
    //     return;
    // }

    // moveAndRedrawEntity(entityIndex, newRow, newCol);

    // handleUnknownEntityType(entityIndex);
}

static constexpr std::array<EntityHandler, 60> ENTITY_HANDLERS = {{
    handleSmartEntityCommon,   // 0
    handleArrowUp,            // 1
    handleArrowDown,           // 2
    handleArrowLeft,            // 3
    handleArrowRight,           // 4
    handleMagnetVertical,       // 5
    handleMagnetHorizontal,     // 6
    handlePusherUp,             // 7
    handlePusherDown,           // 8
    handlePusherLeft,           // 9
    handlePusherRight,          // 10
    handleCurvedArrowUp,        // 11
    handleCurvedArrowDown,      // 12
    handleCurvedArrowLeft,      // 13
    handleCurvedArrowRight,     // 14
    handleMonsterEntityType,    // 15
    handleMonsterEntityType,    // 16
    handleMonsterEntityType,    // 17
    handleMonsterEntityType,    // 18
    handleMonsterEntityType,    // 19
    handleSmartEntityCommon,   // 20
    handleSmartEntityCommon,   // 21
    handleSmartEntityCommon,   // 22
    handleSmartEntityCommon,   // 23
    handleSmartEntityCommon,   // 24
    handleSmartEntityCommon,   // 25
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
            ENTITY_HANDLERS[(int)entityType](entityIndex);
        }
        else
        {
            handleUnknownEntityType(entityIndex);
        }

        ++entityIndex;
    }
}
