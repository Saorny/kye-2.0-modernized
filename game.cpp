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
int g_levelJustLoadedFlag = 1;
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

extern std::string g_levelRawText;
extern std::string g_levelHintText;
extern std::string g_levelDisplayText;

constexpr int kMaxLevelEntries = 32;

std::int16_t specialCellStateFlag = 0;

std::string g_nameInputBuffer;

std::string g_statusLineText;

static bool g_keyDownFlag = false;

extern const char* g_str_1169;
extern const char* g_str_1171;
extern const char* g_str_117A;
extern const char* g_str_1189;
extern const char* g_str_1192;
extern const char* g_str_119C;
extern const char* g_str_11A4;
extern const char* g_str_11AF;
extern const char* g_str_11BE;
extern const char* g_str_11CE;

extern std::string g_levelRawText;
extern std::string g_levelHintText;
extern std::string g_levelDisplayText;

static constexpr int16_t kTargetRequiredState = 0x001F;
static constexpr int16_t kTargetClearedState  = 0x0020;

static SDL_TimerID g_timerId = 0;

static constexpr std::int16_t TILE_SEL_SENTINEL = (std::uint16_t)EntityType::Diamond;
static constexpr std::int16_t TILE_SPAWN        = (std::uint16_t)0xFFFE;
static constexpr std::int16_t TILE_EXIT         = (std::uint16_t)0xFFF9;

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

static inline bool isFixedTile(std::int16_t v) {
    // 0xFFF5..0xFFFD inclus
    return (v >= (std::int16_t)0xFFF5) && (v <= (std::int16_t)0xFFFD);
}

// Un type d'event SDL "privé" pour ton polling
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
    // 1) Register l'event type (une seule fois)
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

void updateLevelEntitiesEvery30Frames() {
    if (frameCounter % 30 != 0) return;

    for (int i = 0; i < g_activeSpawnerCount; ++i) {
        auto& entry = g_gameState.entities[i];

        if (entry.entityType < 0x32 || entry.entityType > 0x3B) {
            continue;
        }

        int row = entry.row;
        int col = entry.col;

        if (entry.entityType > 0x32) {
            --entry.entityType;
            moveAndRedrawEntity(i, row, col);  // Possibly triggers animation or state shift
        } else {
            drawRectangleFromGrid(row, col);
            markEntryInactive(i);
        }
    }

    finalizeLevelVisuals();
}

void moveAndRedrawEntity(int entityIndex, int newRow, int newCol)
{
    auto& entity = g_gameState.entities[entityIndex];

    const int oldRow = entity.row;
    const int oldCol = entity.col;
    drawRectangleFromGrid(oldRow, oldCol);
    if (oldRow >= 0 && oldRow < GRID_ROWS &&
        oldCol >= 0 && oldCol < GRID_COLS)
    {
        g_entityIndexGrid[oldRow][oldCol] = -1;
    }
    entity.row = newRow;
    entity.col = newCol;
    if (newRow >= 0 && newRow < GRID_ROWS &&
        newCol >= 0 && newCol < GRID_COLS)
    {
        g_entityIndexGrid[newRow][newCol] = entityIndex;
    }
    renderEntityToSdl(entityIndex);
}

void showNewLevelDialog(SDL_Window* window)
{
    g_window = window;
    g_newLevelDialogOpen = true;
    g_newLevelDialogResult = NewLevelDialogResult::None;
    g_levelInput.clear();

    SDL_StartTextInput(g_window);
}

bool tryMoveSmartEntity(int entityIndex)
{
    auto& e = g_gameState.entities[entityIndex];

    const int row = e.row;
    const int col = e.col;

    // --- 1️⃣ RIGHT ---
    if (g_gridMain[row][col + 1] == CELL_FLAG_EMPTY)
    {
        int target = g_gameState.rightEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == 5)
        {
            moveAndRedrawEntity(entityIndex, row, col + 1);
            return true;
        }
    }

    // --- 2️⃣ LEFT ---
    if (g_gridMain[row][col - 1] == CELL_FLAG_EMPTY)
    {
        int target = g_gameState.leftEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == 5)
        {
            moveAndRedrawEntity(entityIndex, row, col - 1);
            return true;
        }
    }

    // --- 3️⃣ DOWN ---
    if (g_gridMain[row + 1][col] == CELL_FLAG_EMPTY)
    {
        int target = g_gameState.bottomEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == 6)
        {
            moveAndRedrawEntity(entityIndex, row + 1, col);
            return true;
        }
    }

    // --- 4️⃣ UP ---
    if (g_gridMain[row - 1][col] == CELL_FLAG_EMPTY)
    {
        int target = g_gameState.topEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == 6)
        {
            moveAndRedrawEntity(entityIndex, row - 1, col);
            return true;
        }
    }

    return false;
}

int canEntityMove(int entityIndex)
{
    auto& e = g_gameState.entities[entityIndex];
    const int row = e.row;
    const int col = e.col;

    // --- Right ---
    {
        int target = g_gameState.rightEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == 5)
            return 1;
    }

    // --- Left ---
    {
        int target = g_gameState.leftEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == 5)
            return 1;
    }

    // --- Down ---
    {
        int target = g_gameState.bottomEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == 6)
            return 1;
    }

    // --- Up ---
    {
        int target = g_gameState.topEntityMap[row][col];
        if (target >= 0 &&
            g_gameState.entities[target].entityType == 6)
            return 1;
    }

    return 0;
}

bool checkAndHandleDeathCondition(int changeIndex)
{
    const LevelChange& entry = changeList[changeIndex];

    const int row = entry.row;
    const int col = entry.col;

    bool death =
        g_gameState.rightEntityMap[row][col]  == CELL_FLAG_SENTINEL ||
        g_gameState.leftEntityMap[row][col]   == CELL_FLAG_SENTINEL ||
        g_gameState.topEntityMap[row][col]    == CELL_FLAG_SENTINEL ||
        g_gameState.bottomEntityMap[row][col] == CELL_FLAG_SENTINEL;

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

    previousRow = spawnCol;
    previousCol = spawnRow;

    int leftX   = spawnCol - 1;
    int bottomY = spawnRow + 1;
    int rightX  = spawnCol + 1;
    int topY    = spawnRow - 1;

    int scanLength = 2;
    int iteration  = 1;

    while (iteration < 5)
    {
        // ↓ colonne gauche vers bas
        int x = leftX;
        int y = bottomY;

        for (int i = 0; i < scanLength; ++i)
        {
            previousRow = x;
            previousCol = y;

            if (gameGrid[y][x] == CELL_FLAG_EMPTY)
                return;

            y++;
        }

        // → ligne bas vers droite
        x = rightX;
        y = bottomY;

        for (int i = 0; i < scanLength; ++i)
        {
            previousRow = x;
            previousCol = y;

            if (gameGrid[y][x] == CELL_FLAG_EMPTY)
                return;

            x--;
        }

        // ↑ colonne droite vers haut
        x = rightX;
        y = topY;

        for (int i = 0; i < scanLength; ++i)
        {
            previousRow = x;
            previousCol = y;

            if (gameGrid[y][x] == CELL_FLAG_EMPTY)
                return;

            y--;
        }

        // ← ligne haut vers gauche
        x = leftX;
        y = topY;

        for (int i = 0; i < scanLength; ++i)
        {
            previousRow = x;
            previousCol = y;

            if (gameGrid[y][x] == CELL_FLAG_EMPTY)
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

    previousRow = srcRow;
    previousCol = srcCol;
}

int loadLevelByIndex(int level)
{
    std::cout << "Loading " << g_selectedFilePath << ", level " << level << std::endl;
    if (!fileAccessEnabled) {
        return 0;
    }

    FileLike* file = openAndPrepareFileFromSlot(g_selectedFilePath, "r");
    if (!file) {
        showMessage(g_selectedFilePath, "Cannot open file: ");
        resetLevelStateMemory();
        fileAccessEnabled = false;
        return 0;
    }

    resetAndSeekFile(file, 0);

    std::array<char, 0x4F + 1> outBuf{};
    readLineToBuffer(file, outBuf.data(), 0x4F);
    levelCount = parseSignedDecimalString(outBuf.data());

    hasLevelList            = false;
    g_hasPendingModal       = false;
    g_levelJustLoadedFlag   = true;
    hasLevelTransition      = false;
    remainingLives          = 3;
    selectedObjectIndex     = 0xFFFF;

    if (level < 1) {
        level = 1;
    } else if (level > levelCount) {
        level = levelCount;
    }

    std::array<char, 0x38C> levelBlock{};
    for (int i = 0; i < level; ++i) {
        if (readStructuredBlock(file, levelBlock.data()) < 0) {
            cleanFile(file);
            return 0;
        }
    }
    cleanFile(file);
    g_activeSpawnerCount = 0;

    g_levelRawText.assign(reinterpret_cast<char*>(g_levelHintText.data()));
    g_levelHintText    = std::string(reinterpret_cast<char*>(g_levelHintText.data() + 0x150));
    g_levelDisplayText = std::string(reinterpret_cast<char*>(g_levelHintText.data() + 0x2A0));

    std::array<char, 0x23 * 0x14> lineData{};
    for (int col = 0; col < 0x14; ++col) {
        loadLevelRow(col, lineData.data() + col * 0x23);
    }

    postLoadLevel();
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
    // g_activeSpawnerCount = 0
    g_activeSpawnerCount = 0;

    // copies de strings (équivalent rep movsw)
    safeCopy(g_levelName,           sizeof(g_levelName),           kDefaultLevelName);
    safeCopy(g_levelHintTextBuffer, sizeof(g_levelHintTextBuffer), kDefaultHintText);
    safeCopy(g_statusLineBuffer,    sizeof(g_statusLineBuffer),    kDefaultStatusLine);

    // 1) Remplir toute la grille en "Wall5" (0xFFF9)
    for (int row = 0; row < GRID_ROWS; ++row) {
        for (int col = 0; col < GRID_COLS; ++col) {
            gameGrid[row][col] = TILE_WALL5;
        }
    }

    // 2) Vider l'intérieur (rows 1..28, cols 1..18) en 0xFFFF
    // asm: col de 1 à 0x12 (18), row de 1 à 0x1C (28)
    for (int row = 1; row <= 28; ++row) {
        for (int col = 1; col <= 18; ++col) {
            gameGrid[row][col] = TILE_EMPTY;
        }
    }

    // valeurs finales
    exitCoordLeft  = TILE_WALL5; // 0xFFF9
    exitCoordRight = TILE_WALL5; // 0xFFF9
    exitState      = 0xFFFB;

    srcRow = 3;
    srcCol = 3;

    spawnRow = 3;
    spawnCol = 3;

    selectedTileValue   = 0xFFFE;
    selectionState      = (int)EntityType::Diamond;
    selectedEntityIndex = 0xFFFF; // asm: selectedObjectIndex
}

int decodeTile(char inputChar, std::uint16_t* outA, std::uint16_t* outB)
{
    if (!outA || !outB) return 0;

    const std::uint8_t ch = static_cast<std::uint8_t>(inputChar);

    for (std::size_t i = 0; ; ++i) {
        const DecodeTileEntry& e = g_decodeTileTable[i];

        // asm: cmp word ptr [bx+2C0h], 0FFFFh -> fin
        if (e.a == 0xFFFFu) {
            return 0;
        }

        // asm: mov al, [si+2C4h] ; cmp al, inputChar
        if (e.symbol == ch) {
            // asm: mov ax,[si+2C0] -> [di]
            *outA = e.a;
            // asm: mov ax,[si+2C2] -> [bx]
            *outB = e.b;
            return 1;
        }
    }
}

std::int16_t registerLevelChange(std::int16_t tileId, std::int16_t row, std::int16_t col)
{
    const std::uint16_t index = g_activeSpawnerCount;

    // asm: g_cellClickFlags[row][col] = index
    g_cellClickFlags[cellIndex(row, col)] = static_cast<std::int16_t>(index);

    // asm: stride 8, fields at 0x172E..0x1734
    changeList[index].tileId = static_cast<std::uint16_t>(tileId);
    changeList[index].row    = static_cast<std::uint16_t>(row);
    changeList[index].col    = static_cast<std::uint16_t>(col);
    changeList[index].speed  = 0;

    // asm: g_activeSpawnerCount++ ; return (old value)
    g_activeSpawnerCount = static_cast<std::uint16_t>(g_activeSpawnerCount + 1);

    return static_cast<std::int16_t>(index);
}

void pseudoRandomUpdate(uint32_t add)
{
    uint32_t seed =
        (static_cast<uint32_t>(randomSeedHigh) << 16) |
        randomSeedLow;

    // LCG style DOS
    seed = seed * 0x015A4E35u + 1u;

    seed += add;

    randomSeedHigh = static_cast<uint16_t>(seed >> 16);
    randomSeedLow  = static_cast<uint16_t>(seed & 0xFFFF);
}

void loadLevelRow(int col, const char* lineData)
{
    if (col >= 20 || !lineData)
        return;

    // Reset column
    for (int row = 0; row < 30; ++row)
        gameGrid[col][row] = 0xFFFF;

    const char* ptr = lineData;
    int row = 0;

    while (*ptr && row < 30)
    {
        uint16_t tileCode;
        uint16_t tileType;

        if (!decodeTile(*ptr++, &tileCode, &tileType))
        {
            row++;
            continue;
        }

        switch (tileType)
        {
            case 0:
                gameGrid[col][row] = 0xFFFF;
                break;

            case 1:
                gameGrid[col][row] = tileCode;
                break;

            case 2:
            {
                int changeIndex = registerLevelChange(col, row, tileCode);

                if (changeIndex >= 0)
                {
                    changeList[changeIndex].speed = generateChangeSpeed();
                }

                break;
            }

            case 3:
                gameGrid[col][row] = 0xFFFE;

                srcRow   = row;
                srcCol   = col;
                spawnCol = row;
                spawnRow = col;
                break;
        }

        row++;
    }
}

static inline void invalidateCell(std::int16_t& cell) {
    if (!isFixedTile(cell)) {
        if (cell >= 0) {
            markEntryInactive((int)cell);
        }
        cell = TILE_EXIT;
    }
}

int postLoadLevel()
{
    bool hasSelectionSentinel = false;
    for (int r = 0; r < GRID_ROWS && !hasSelectionSentinel; ++r) {
        for (int c = 0; c < GRID_COLS ; ++c) {
            if (g_gridMain[r][c] == TILE_SEL_SENTINEL) {
                hasSelectionSentinel = true;
                break;
            }
        }
    }
    if (!hasSelectionSentinel) {
        selectionState = (uint16_t)TILE_SEL_SENTINEL;
    }

    bool foundSpawn = false;
    int foundR = 0, foundC = 0;
    for (int r = 0; r < GRID_ROWS && !foundSpawn; ++r) {
        for (int c = 0; c < GRID_COLS ; ++c) {
            if (g_gridMain[r][c] == TILE_SPAWN) {
                foundSpawn = true;
                foundR = r;
                foundC = c;
                break;
            }
        }
    }

    if (!foundSpawn || foundR != srcRow || foundC != srcCol) {
        srcRow = 3;
        srcCol = 3;
        spawnRow = 3;
        spawnCol = 3;
        selectedTileValue = (uint16_t)TILE_SPAWN;
    }

    // C) invalidate 3 grilles
    for (int r = 0; r < GRID_ROWS; ++r)
        for (int c = 0; c < GRID_COLS; ++c)
            invalidateCell(g_gridMain[r][c]);

    for (int r = 0; r < GRID_ROWS; ++r)
        for (int c = 0; c < GRID_COLS ; ++c)
            invalidateCell(g_gridAuxA[r][c]);

    for (int r = 0; r < GRID_ROWS; ++r)
        for (int c = 0; c < GRID_COLS ; ++c)
            invalidateCell(g_gridAuxB[r][c]);

    finalizeLevelVisuals();
    return 1;
}

int replaceEntityIfTargetMatches(int index, int newRow, int newCol)
{
    if (newRow < 0 || newRow >= GRID_ROWS || newCol < 0 || newCol >= GRID_COLS)
        return 0;

    const int16_t targetIndex = g_entityIndexGrid[newRow][newCol];
    if (targetIndex < 0)
        return 0;

    auto& slots = g_gameState.entities;

    if (targetIndex >= static_cast<int16_t>(slots.size()) || index < 0 || index >= static_cast<int>(slots.size()))
        return 0;

    auto& target   = slots[static_cast<size_t>(targetIndex)];
    auto& attacker = slots[static_cast<size_t>(index)];

    if (target.entityType != kTargetRequiredState)
        return 0;

    const int oldRow = attacker.row;
    const int oldCol = attacker.col;

    target.entityType = kTargetClearedState;
    target.timer      = 0; // asm: [1734h] = 0

    moveAndRedrawEntity(targetIndex, newRow, newCol);
    drawRectangleFromGrid(oldRow, oldCol);
    markEntryInactive(index);

    return 1;
}

void markEntryInactive(int index) {
    if (index >= 0 && index < MAX_CHANGES) {
        changeList[index].tileId = 0x00FF;
    }
}

void finalizeLevelVisuals()
{
    int i = 0;

    while (i < g_activeSpawnerCount)
    {
        if (changeList[i].tileId == 0x00FF)
        {
            --g_activeSpawnerCount;
            for (int j = i; j < g_activeSpawnerCount; ++j)
            {
                changeList[j] = changeList[j + 1];
            }
            for (int row = 1; row < GRID_ROWS - 1; ++row)
            {
                for (int col = 1; col < GRID_COLS - 1; ++col)
                {
                    int16_t& val = g_entityIndexGrid[row][col];

                    if (val > i)
                        --val;
                }
            }
        }
        else
        {
            ++i;
        }
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
        drawRectangleFromGrid(srcRow, srcCol);

        // Restore cursor position
        srcRow = previousRow;
        srcCol = previousCol;

        if (0 <= srcRow && srcRow < GRID_ROWS && 0 <= srcCol && srcCol < GRID_COLS)
            gameGrid[srcRow][srcCol] = CELL_FLAG_SENTINEL; // 0xFFFE

        runTileSparkleEffect(1);
        hasLevelTransition = 0;
        return;
    }

    if (previousRow == srcRow && previousCol == srcCol)
        return;

    const int dLegacyRow = previousRow - srcRow;
    const int dLegacyCol = previousCol - srcCol;

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

int handlePendingBlock(std::int16_t rowIndex,
                       std::int16_t colIndex)
{
    // 1️⃣ Si déjà pending sur la même case → rien
    if (isPendingDraw &&
        pendingRow == rowIndex &&
        pendingCol == colIndex)
    {
        return 0;
    }

    // 2️⃣ Efface ancien rectangle
    maybeDrawPendingRectangle();

    pendingRow = rowIndex;
    pendingCol = colIndex;

    // 3️⃣ Distance absolue row
    std::int16_t deltaRow = rowIndex - srcRow;
    if (deltaRow < 0) deltaRow = -deltaRow;

    if (deltaRow <= 1)
    {
        // Distance absolue col
        std::int16_t deltaCol = colIndex - srcCol;
        if (deltaCol < 0) deltaCol = -deltaCol;

        if (deltaCol <= 1)
        {
            return 0;
        }
    }

    // 4️⃣ Vérifier si case vide
    if (gameGrid[pendingRow][pendingCol] == CELL_FLAG_EMPTY)
    {
        drawPendingBlock();
        isPendingDraw = true;
    }

    return 0;
}

int hasSpecialCell()
{
    for (int col = 0; col < GRID_COLS; ++col)
    {
        for (int row = 0; row < GRID_ROWS; ++row)
        {
            if (gameGrid[row][col] == CELL_FLAG_SENTINEL)
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
            cout << "alpha" << endl;
            if (entry.priority <= bestPriority)
            {
                cout << "bravo" << endl;
                bestPriority = entry.priority;
                best = &entry;
            }
        }

        if (!best)
            return;
        cout << "charly" << endl;
        uint8_t state = best->state;
        best->state = 0xFF;
        cout << "delta" << endl;
        cout << "callback addr = " << std::hex << best->callback << endl;
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
    auto& state = g_gameState;

    int row = state.entities[entityIndex].row;
    int col = state.entities[entityIndex].col;

    // ---- Case 1 : left empty + auxMap1282 == SENTINEL → move right
    if (state.leftEntityMap[row][col] == CELL_FLAG_EMPTY &&
        state.auxMap1282[row][col] == CELL_FLAG_SENTINEL)
    {
        moveAndRedrawEntity(entityIndex, row, col + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    // ---- Case 2 : right empty + auxMap127A == SENTINEL → move left
    if (state.rightEntityMap[row][col] == CELL_FLAG_EMPTY &&
        state.auxMap127A[row][col] == CELL_FLAG_SENTINEL)
    {
        moveAndRedrawEntity(entityIndex, row, col - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    // ---- Case 3 : bottom empty + spawner.state == 6 → move down
    if (state.bottomEntityMap[row][col] == CELL_FLAG_EMPTY)
    {
        int spawnerIndex = state.auxMap12CE[row][col];

        if (spawnerIndex >= 0 &&
            g_spawners[spawnerIndex].type == EntityType::MagnetHorizontal)
        {
            moveAndRedrawEntity(entityIndex, row + 1, col);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    // ---- Case 4 : top empty + spawner.state == 6 → move up
    if (state.topEntityMap[row][col] == CELL_FLAG_EMPTY)
    {
        int spawnerIndex = state.auxMap127A[row][col];

        if (spawnerIndex >= 0 &&
            g_spawners[spawnerIndex].type == EntityType::MagnetHorizontal)
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
    auto& state = g_gameState;

    int row = state.entities[entityIndex].row;
    int col = state.entities[entityIndex].col;

    // ---- Case 1 : bottom empty + auxMap12CE == SENTINEL → move down
    if (state.bottomEntityMap[row][col] == CELL_FLAG_EMPTY &&
        state.auxMap12CE[row][col] == CELL_FLAG_SENTINEL)
    {
        moveAndRedrawEntity(entityIndex, row + 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    // ---- Case 2 : top empty + auxMap12CE == SENTINEL → move up
    if (state.topEntityMap[row][col] == CELL_FLAG_EMPTY &&
        state.auxMap12CE[row][col] == CELL_FLAG_SENTINEL)
    {
        moveAndRedrawEntity(entityIndex, row - 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    // ---- Case 3 : left empty + auxMap1282 → spawner.state == 5 → move right
    if (state.leftEntityMap[row][col] == CELL_FLAG_EMPTY)
    {
        int auxIndex = state.auxMap1282[row][col];

        if (auxIndex >= 0 &&
            g_spawners[auxIndex].type == EntityType::MagnetVertical)
        {
            moveAndRedrawEntity(entityIndex, row, col + 1);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    // ---- Case 4 : right empty + auxMap127A → spawner.state == 5 → move left
    if (state.rightEntityMap[row][col] == CELL_FLAG_EMPTY)
    {
        int auxIndex = state.auxMap127A[row][col];

        if (auxIndex >= 0 &&
            g_spawners[auxIndex].type == EntityType::MagnetVertical)
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
            return g_gameState.auxTopRightEntityMap[row][col] == CELL_FLAG_EMPTY;
        };

        bool verticalEmpty   = isEmpty(curRow + dy, curCol);
        bool horizontalEmpty = isEmpty(curRow,       curCol + dx);

        if (verticalEmpty && horizontalEmpty) {
            // Correspond à la branche où les deux tests sont 0xFFFF,
            // puis comparaison des |cx| et |si|.
            // L’ASM termine toujours en gardant uniquement l’axe horizontal
            // dans ce cas (dy mis à 0, dx conservé).
            dy = 0;
        } else if (verticalEmpty) {
            // loc_3317 : la case (curRow+dy, curCol) vide,
            //           on met dx à 0 → on ne bouge que verticalement.
            dx = 0;
        } else if (horizontalEmpty) {
            // loc_332B : la case (curRow, curCol+dx) vide,
            //           on met dy à 0 → on ne bouge que horizontalement.
            dy = 0;
        } else {
            // loc_3348 : aucune des deux n’est vide → on finit par
            // mettre dy à 0 dans l’ASM, donc on ne bouge plus que
            // horizontalement (ou pas du tout si dx==0).
            dy = 0;
        }
    }

    // loc_334A : calcul de la case candidate (curRow + dy, curCol + dx)
    int candidateRow = curRow + dy;
    int candidateCol = curCol + dx;

    // Re-test final sur auxRightEntityMap (0x127E)
    auto isEmpty = [](int row, int col) -> bool {
        if (row < 0 || row >= GRID_ROWS ||
            col < 0 || col >= GRID_COLS)
            return false;
        return g_gameState.auxTopRightEntityMap[row][col] == CELL_FLAG_EMPTY;
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

const char* lookupText(uint16_t id) {
    switch (id) {
        case 0x29FA: return "???"; // à remplir quand tu dump les strings
        default:     return "<unknown text>";
    }
}

void setStatusTextFromId(uint16_t id) {
    setStatusText(lookupText(id));
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
            if (gameGrid[row][col] == (int)EntityType::Diamond) {
                ++count;
            }
        }
    }
    return count;
}

int renderLivesAndLevelInfo()
{
    char textBuffer[0x6C] = {0};
    uint8_t stringOverflowFlag = 0;

    // ===============================
    // MODE EDIT  (ancien loc_299B)
    // ===============================

    if (g_interactionMode == GameInteractionMode::PendingBlock)
    {
        int count = 0;

        for (int col = 0; col < 30; ++col)
        {
            for (int row = 0; row < 20; ++row)
            {
                if (gameGrid[row][col] == (int)EntityType::Diamond)
                    ++count;
            }
        }

        prepareAndCallProcessMainLoop(textBuffer, 0x4DC, count);

        int len = std::strlen(textBuffer);
        drawText(baseX + 5, baseY, textBuffer, len);

        prepareAndCallProcessMainLoop(textBuffer, 0x4ED, 0x2D5E);

        len = std::strlen(textBuffer);

        if (len > 0x19)
        {
            len = 0x19;
            stringOverflowFlag = 0;
        }

        drawText(baseX + 0x6E, baseY, textBuffer, len);

        return 1;
    }

    if (g_interactionMode != GameInteractionMode::NormalPlay)
        return 0;

    SDL_SetRenderDrawColor(g_renderer, 160, 160, 160, 255);

    SDL_FRect hudRect;
    hudRect.x = baseX;
    hudRect.y = baseY;
    hudRect.w = 0x46;
    hudRect.h = 0x11;

    SDL_RenderRect(g_renderer, &hudRect);

    int xOffsetLives = 0;

    for (int i = 0; i < remainingLives; ++i)
    {
        SDL_FRect dst;
        dst.x = baseX + xOffsetLives + 1;
        dst.y = baseY + 1;
        dst.w = 16;
        dst.h = 16;

        SDL_RenderTexture(g_renderer, g_kyeTexture, nullptr, &dst);

        xOffsetLives += 0x14;
    }

    prepareAndCallProcessMainLoop(textBuffer, 0x4B8, levelIndex);

    int len = std::strlen(textBuffer);
    drawText(baseX + 0x50, baseY, textBuffer, len);

    int count = 0;

    for (int col = 0; col < GRID_COLS; ++col)
    {
        for (int row = 0; row < GRID_ROWS; ++row)
        {
            if (gameGrid[row][col] == (int)EntityType::Diamond)
                ++count;
        }
    }

    prepareAndCallProcessMainLoop(textBuffer, 0x4C6, count);

    len = std::strlen(textBuffer);
    drawText(baseX + 0xA0, baseY, textBuffer, len);

    return 1;
}

void setStatusText(const std::string& text) {
    clearStatusLine(text.c_str());
}

void handleClickOnGridCell(std::uint32_t cellId)
{
    const std::int16_t row = static_cast<std::int16_t>(cellId & 0xFFFF);
    const std::int16_t col = static_cast<std::int16_t>((cellId >> 16) & 0xFFFF);

    if (g_gridMain[row][col] == CELL_FLAG_EMPTY)
    {
        const std::int16_t index = static_cast<std::int16_t>(g_currentNameIndex);
        const std::int16_t stride = 0x1A / 2;

        extern std::int16_t g_entryRowTable[];
        extern std::int16_t g_entryColTable[];
        extern std::int16_t g_entryEnabledTable[];

        const std::int16_t entryBase = static_cast<std::int16_t>(index * stride);

        if (g_entryEnabledTable[index] != 0)
        {
            executeCurrentEntryAction(
                g_entryRowTable[entryBase],
                g_entryColTable[entryBase],
                row,
                col
            );
            return;
        }
    }

    handleStandardCellClick(row, col);
}

int handleStandardCellClick(int row, int col)
{
    // ASM calcule l'offset: row*0x28 + col*2 sur un tableau word (0x28 = 40 bytes = 20 words)
    // => ça correspond à un grid [20 cols] stocké row-major en int16.handleClickOnGridCell
    std::int16_t& cell = g_gridAuxA[row][col];

    const std::int16_t di = cell; // copie (ASM garde DI pour le test sentinel après)

    // if (di > 0) { markEntryInactive(di); cell = 0xFFFF; } else { cell = 0xFFFF; }
    if (di > 0) {
        markEntryInactive(di);
        cell = CELL_FLAG_EMPTY;
    } else {
        cell = CELL_FLAG_EMPTY;
    }

    finalizeLevelVisuals();

    // si la valeur précédente était le sentinel spécial, on déclenche le handler
    if (di == CELL_FLAG_SENTINEL) {
        handleSpecialSentinelClick();
    }

    return 0; // ASM ne set pas AX explicitement avant retn; on renvoie 0 par convention C
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

void executeCurrentEntryAction(std::int16_t actionType,
                               std::int16_t tileId,
                               std::int16_t row,
                               std::int16_t col)
{
    const bool inBounds =
        (col > 0 && col < (GRID_COLS - 1)) &&
        (row > 0 && row < (GRID_ROWS - 1));

    const bool invalidPos = !inBounds;

    switch (actionType) {
    case 1:
        // ASM : écrit tileId dans la grille si la position est valide
        if (!invalidPos) {
            gameGrid[row][col] = static_cast<uint16_t>(tileId);
        }
        break;

    case 2:
        // ASM : registerLevelChange(tileId, row, col) si position valide
        if (!invalidPos) {
            registerLevelChange(tileId, row, col);
        }
        break;

    case 3:
        // ASM : pose un sentinel (0xFFFE), stocke srcRow/srcCol, puis handleSpecialSentinelClick
        if (!invalidPos) {
            gameGrid[row][col] = static_cast<uint16_t>(CELL_FLAG_SENTINEL);
            srcRow = row;
            srcCol = col;
            handleSpecialSentinelClick();
        }
        break;

    default:
        // autres valeurs : no-op, comme dans l’ASM
        break;
    }
}

void updateGridCell(int row, int col)
{
    if (!g_renderer)
        return;

    uint16_t tile = g_gameState.entityMap[row][col];

    if (tile > 0)
    {
        renderEntityToSdl(tile); // entités actives
    }
    else if (tile == 0xFFFE)
    {
        // Player / block special
        SDL_FRect dst{
            float(gridOriginX + col*cellWidth),
            float(gridOriginY + row*cellHeight),
            float(cellWidth),
            float(cellHeight)
        };
        SDL_RenderTexture(g_renderer, g_sheetKye.tex, nullptr, &dst);
    }
    else if (tile == 0xFFFF)
    {
        drawRectangleFromGrid(row, col); // case vide
    }
    else
    {
        renderWallTile(row, col, tile); // murs
    }
}

void renderWallTile(int row, int col, uint16_t tileValue)
{
    if (!g_renderer || !bitmap_wall)
        return;

    SDL_FRect dstRect{
        float(gridOriginX + col * cellWidth),
        float(gridOriginY + row * cellHeight),
        float(cellWidth),
        float(cellHeight)
    };

    SDL_FRect srcRect{0.f, 0.f, 16.f, 16.f};

    switch (tileValue)
    {
        case 0xFFFD: srcRect.x = 0x30; srcRect.y = 0x10; break;
        case 0xFFFC: srcRect.x = 0x40; srcRect.y = 0x10; break;
        case 0xFFFB: srcRect.x = 0x50; srcRect.y = 0x10; break;
        case 0xFFFA: srcRect.x = 0x60; srcRect.y = 0x10; break;
        case 0xFFF9: srcRect.x = 0x70; srcRect.y = 0x10; break;
        case 0xFFF8: srcRect.x = 0x80; srcRect.y = 0x10; break;
        case 0xFFF7: srcRect.x = 0x90; srcRect.y = 0x10; break;
        case 0xFFF6: srcRect.x = 0xA0; srcRect.y = 0x10; break;
        case 0xFFF5: srcRect.x = 0xB0; srcRect.y = 0x10; break;
        case 0xFFF4: srcRect.x = 0xC0; srcRect.y = 0x10; break;
        case (int)EntityType::Diamond: srcRect.x = 0xD0; srcRect.y = 0x10; break;
        case 0xFFF2: srcRect.x = 0xE0; srcRect.y = 0x10; break;
        case 0xFFF1: srcRect.x = 0xF0; srcRect.y = 0x10; break;
        case 0xFFF0: srcRect.x = 0x00; srcRect.y = 0x20; break;
        case 0xFFEF: srcRect.x = 0x10; srcRect.y = 0x20; break;
        default: return;
    }

    SDL_RenderTexture(g_renderer, bitmap_wall, &srcRect, &dstRect);
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

    for (std::int16_t i = 0; i < g_activeSpawnerCount && i < g_spawners.size(); ++i)
    {
        Spawner& spawner = g_spawners[i];

        const int dstY = spawner.row * cellHeight;
        const int dstX = spawner.col * cellWidth;

        const int srcX = 0x40 + (spawner.animFrame << 4);

        if (spawner.type == EntityType::Lava) // 0x1F
        {
            g_blockSheet.blit16(
                dstX,
                dstY,
                srcX,
                0x80
            );

            spawner.animFrame = (spawner.animFrame + 1) % 4;
        }
        else if (spawner.type == EntityType::Lava2) // 0x20
        {
            g_blockSheet.blit16(
                dstX,
                dstY,
                srcX,
                0x90
            );

            if (spawner.animFrame == 3)
                spawner.type = EntityType::Lava;

            spawner.animFrame = (spawner.animFrame + 1) % 4;
        }
    }
}

void drainPendingEvents() {}

void animateDiamonds()
{
    if (frameCounter % 10 != 0)
        return;

    for (int row = 0; row < GRID_ROWS; row++)
    {
        for (int col = 0; col < GRID_COLS; col++)
        {
            if (gameGrid[row][col] != (uint16_t)EntityType::Diamond)
                continue;

            int dstX = col * cellWidth;
            int dstY = row * cellHeight;

            int srcX = 0xC0;
            int srcY = 0;

            if ((std::rand() % 3) == 1)
                srcY = 16;

            g_blockSheet.blit16(
                dstX,
                dstY,
                srcX,
                srcY
            );
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
            uint16_t tile = gameGrid[row][col];

            int spriteY = -1;

            if (tile == (uint16_t)EntityType::OneWayLeftToRight ||
                tile == (uint16_t)EntityType::OneWayRightToLeft)
            {
                spriteY = 0xE0;
            }
            else if (tile == (uint16_t)EntityType::OneWayTopToBottom ||
                     tile == (uint16_t)EntityType::OneWayBottomToTop)
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

static inline bool isCellEmpty_asmLayout(int row, int col)
{
    return gameGrid[col][row] == 0xFFFF;
}

static void placeTileAndSpawnEntityIfEmpty_Core(
    int row, int col, int tileId,
    uint16_t* spawnCounterPtr
)
{
    if (!isCellEmpty_asmLayout(row, col))
        return;

    *spawnCounterPtr = 0;

    const int spawnedEntityIndex = registerLevelChange(tileId, row, col);
    moveAndRedrawEntity(spawnedEntityIndex, row, col);
}

void def_5AE1_tryPlaceAndSpawn(int row, int col, int tileId, uint16_t* spawnCounterPtr)
{
    placeTileAndSpawnEntityIfEmpty_Core(row, col, tileId, spawnCounterPtr);
}

void def_5BCD_tryPlaceAndSpawn(int row, int col, int tileId, uint16_t* spawnCounterPtr)
{
    placeTileAndSpawnEntityIfEmpty_Core(row, col, tileId, spawnCounterPtr);
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
        Spawner& sp = g_spawners[i];

        int row = sp.row;
        int col = sp.col;

        sp.animFrame++;

        int targetRow = row;
        int targetCol = col;
        EntityType spawnType = EntityType::None;

        switch(sp.type)
        {
            case EntityType::UnknownA1:
                spawnType = EntityType::ArrowRight;
                targetRow = row + 1;
                break;

            case EntityType::UnknownA2:
                spawnType = EntityType::ArrowUp;
                targetCol = col - 1;
                break;

            case EntityType::UnknownA3:
                spawnType = EntityType::ArrowLeft;
                targetRow = row - 1;
                break;

            case EntityType::UnknownA4:
                spawnType = EntityType::ArrowDown;
                targetCol = col + 1;
                break;

            case EntityType::Dispenser1:
                spawnType = EntityType::CurvedArrowRight;
                targetRow = row + 1;
                break;

            case EntityType::Dispenser2:
                spawnType = EntityType::CurvedArrowUp;
                targetCol = col - 1;
                break;

            case EntityType::Dispenser3:
                spawnType = EntityType::CurvedArrowLeft;
                targetRow = row - 1;
                break;

            case EntityType::Dispenser4:
                spawnType = EntityType::CurvedArrowDown;
                targetCol = col + 1;
                break;

            default:
                continue;
        }

        if (gameGrid[targetRow][targetCol] == CELL_FLAG_EMPTY)
        {
            int entityIndex = registerLevelChange(
                (uint16_t)spawnType,
                targetRow,
                targetCol
            );

            moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        }
    }

    finalizeLevelVisuals();
}

void trySpawnFromGroupBIfTargetEmpty(int targetRow, int targetCol, int spawnTileId, uint16_t& spawnDelayCounter)
{
    spawnAtIfEmpty(targetRow, targetCol, spawnTileId, spawnDelayCounter);
}

inline bool isGridCellEmpty(int targetRow, int targetCol)
{
    return gameGrid[targetCol][targetRow] == 0xFFFF;
}

void trySpawnFromGroupAIfTargetEmpty(int targetRow, int targetCol, int spawnTileId, uint16_t& spawnDelayCounter)
{
    spawnAtIfEmpty(targetRow, targetCol, spawnTileId, spawnDelayCounter);
}

inline void spawnAtIfEmpty(int targetRow, int targetCol, int spawnTileId, uint16_t& spawnDelayCounter)
{
    if (!isGridCellEmpty(targetRow, targetCol))
        return;

    spawnDelayCounter = 0;

    const int spawnedEntityIndex = registerLevelChange(spawnTileId, targetRow, targetCol);
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

    for (std::int16_t i = 0; i < g_activeSpawnerCount && i < g_spawners.size(); ++i)
    {
        Spawner& spawner = g_spawners[i];

        const int type = static_cast<int>(spawner.type);

        if (type < 0x0F || type > 0x13)
            continue;

        const int dstY = spawner.row * cellHeight;
        const int dstX = spawner.col * cellWidth;

        const int srcX = type << 4;
        const int srcY = spawner.animFrame << 4;

        g_blockSheet.blit16(
            dstX,
            dstY,
            srcX,
            srcY
        );

        spawner.animFrame = (spawner.animFrame + 1) % 4;
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
            previousRow = srcRow - 1;
            previousCol = srcCol;
            break;

        case SDLK_RIGHT:
            previousRow = srcRow + 1;
            previousCol = srcCol;
            break;

        case SDLK_UP:
            previousRow = srcRow;
            previousCol = srcCol - 1;
            break;

        case SDLK_DOWN:
            previousRow = srcRow;
            previousCol = srcCol + 1;
            break;

        // Pavé numérique (diagonales)
        case SDLK_KP_7: // up-left
            previousRow = srcRow - 1;
            previousCol = srcCol - 1;
            break;

        case SDLK_KP_1: // down-left
            previousRow = srcRow - 1;
            previousCol = srcCol + 1;
            break;

        case SDLK_KP_9: // up-right
            previousRow = srcRow + 1;
            previousCol = srcCol - 1;
            break;

        case SDLK_KP_3: // down-right
            previousRow = srcRow + 1;
            previousCol = srcCol + 1;
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

            drawRectangleFromGrid(srcRow, srcCol);

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

void renderFullWallLayerSdl()
{
    if (!g_renderer || !g_sheetStatics.tex)
        return;

    for (int row = 0; row < GRID_ROWS; ++row)
    {
        for (int col = 0; col < GRID_COLS; ++col)
        {
            uint16_t tileValue = g_gameState.entityMap[row][col];
            renderWallTile(row, col, tileValue);
        }
    }
}

void renderAllSpawnerEntities()
{
    for (int16_t i = 0; i < g_activeSpawnerCount; ++i) {
        renderEntityToSdl(i);
    }
}

void renderFrameByInteractionMode()
{
    const auto mode = g_interactionMode;

    if (mode != GameInteractionMode::NormalPlay &&
        mode != GameInteractionMode::PendingBlock) {
        return;
    }

    renderFullWallLayerSdl();
    renderAllSpawnerEntities();

    if (mode == GameInteractionMode::PendingBlock) {
        runTileSparkleEffect(0);
        return;
    }

    const int effectId = (g_levelJustLoadedFlag != 0) ? 1 : 0;
    g_levelJustLoadedFlag = 0;
    runTileSparkleEffect(effectId);
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
    if (g_hasDeviceContext)
    {
        advanceToNextLevelOrBlock();
    }

    g_hasDeviceContext = true;

    initializeLayoutRects();

    // Clear frame
    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderClear(g_renderer);
    renderHudAndFrame();
    SDL_RenderPresent(g_renderer);
    g_hasDeviceContext = false;
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

void processKyeCollision(int stepRow, int stepCol)
{
    if (stepRow != 0 && stepCol != 0)
        return;

    const int newRow = srcRow + stepRow;
    const int newCol = srcCol + stepCol;

    if (newRow < 0 || newRow >= GRID_ROWS ||
        newCol < 0 || newCol >= GRID_COLS)
        return;

    int16_t cell = gameGrid[newRow][newCol];

    // =========================================================
    // 2️⃣ Case vide
    // =========================================================
    if (cell == CELL_FLAG_EMPTY)
    {
        gameGrid[srcRow][srcCol] = CELL_FLAG_EMPTY;
        srcRow = newRow;
        srcCol = newCol;
        gameGrid[srcRow][srcCol] = CELL_FLAG_SENTINEL;
        return;
    }

    // =========================================================
    // 3️⃣ Tiles fixes spéciaux
    // =========================================================

    switch (cell)
    {
        case 0xFFF4:  // brick
            cell = CELL_FLAG_EMPTY;
            break;

        case (int)EntityType::Diamond:  // diamond
            cell = CELL_FLAG_EMPTY;
            --matchedEntryCount;
            break;

        case 0xFFF2:  // one-way →
            if (stepRow == 0 && stepCol == 1) break;
            return;

        case 0xFFF1:  // one-way ←
            if (stepRow == 0 && stepCol == -1) break;
            return;

        case 0xFFF0:  // one-way ↓
            if (stepRow == 1 && stepCol == 0) break;
            return;

        case 0xFFEF:  // one-way ↑
            if (stepRow == -1 && stepCol == 0) break;
            return;
    }

    if (cell >= 0)
    {
        const int entityIndex = cell;
        const int entityBase = entityIndex * 8;

        const int entityType =
            *reinterpret_cast<int16_t*>(
                reinterpret_cast<uint8_t*>(EntityTable) +
                entityBase + 0
            );

        if (entityType == (int)EntityType::Lava)
        {
            --remainingLives;
            renderLivesAndLevelInfo();
            return;
        }

        // 0x20 → bloquant
        if (entityType == 0x20)
            return;

        const int targetRow =
            *reinterpret_cast<int16_t*>(
                reinterpret_cast<uint8_t*>(EntityTable) +
                entityBase + 2
            ) + stepRow;

        const int targetCol =
            *reinterpret_cast<int16_t*>(
                reinterpret_cast<uint8_t*>(EntityTable) +
                entityBase + 4
            ) + stepCol;

        if (targetRow < 0 || targetRow >= GRID_ROWS ||
            targetCol < 0 || targetCol >= GRID_COLS)
            return;

        if (gameGrid[targetRow][targetCol] != CELL_FLAG_EMPTY)
            return;

        // déplacer entité
        gameGrid[targetRow][targetCol] = entityIndex;
        gameGrid[newRow][newCol] = CELL_FLAG_EMPTY;

        // déplacer Kye
        gameGrid[srcRow][srcCol] = CELL_FLAG_EMPTY;
        srcRow = newRow;
        srcCol = newCol;
        gameGrid[srcRow][srcCol] = CELL_FLAG_SENTINEL;

        return;
    }
}

// --- Common handler for “smart” mobile entities (type 0, 20–26, etc.) ---
void handleSmartEntityCommon(int entityIndex) {
    if (!tryMoveSmartEntity(entityIndex))
        return;
}

// --- Alternative handler for “smart” entities (type 50–59) ---
void handleSmartEntityAlt(int entityIndex) {
    if (!tryMoveSmartEntity(entityIndex))
        return;
}

void handleArrowUp(int entityIndex)
{
    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int newRow = pendingRow;
    const int newCol = pendingCol;

    const int targetIndex = g_gameState.rightEntityMap[newRow][newCol];

    if (targetIndex == CELL_FLAG_EMPTY)
    {
        moveAndRedrawEntity(entityIndex, newRow, newCol - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol - 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (targetIndex >= 0)
    {
        const EntityType targetType =
            static_cast<EntityType>(g_spawners[targetIndex].type);

        if (targetType == EntityType::DeflectorRight)
        {
            g_spawners[entityIndex].type = EntityType::ArrowRight;
            handleUnknownEntityType(entityIndex);
            return;
        }

        if (targetType == EntityType::DeflectorLeft)
        {
            g_spawners[entityIndex].type = EntityType::ArrowLeft;
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleArrowDown(int entityIndex)
{
    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int newRow = pendingRow;
    const int newCol = pendingCol;

    const int targetIndex = g_gameState.leftEntityMap[newRow][newCol];

    if (targetIndex == CELL_FLAG_EMPTY)
    {
        moveAndRedrawEntity(entityIndex, newRow, newCol + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol + 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (targetIndex >= 0)
    {
        const EntityType targetType =
            static_cast<EntityType>(g_spawners[targetIndex].type);

        if (targetType == EntityType::DeflectorLeft)
        {
            g_spawners[entityIndex].type = EntityType::ArrowLeft;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            handleUnknownEntityType(entityIndex);
            return;
        }

        if (targetType == EntityType::DeflectorRight)
        {
            g_spawners[entityIndex].type = EntityType::ArrowRight;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleArrowLeft(int entityIndex)
{
    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int newRow = pendingRow;
    const int newCol = pendingCol;

    const int targetIndex = g_gameState.topEntityMap[newRow][newCol];

    if (targetIndex == CELL_FLAG_EMPTY)
    {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow - 1, newCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (targetIndex >= 0)
    {
        const EntityType targetType = g_spawners[targetIndex].type;

        if (targetType == EntityType::DeflectorLeft)
        {
            g_spawners[entityIndex].type = EntityType::ArrowUp;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            handleUnknownEntityType(entityIndex);
            return;
        }

        if (targetType == EntityType::DeflectorRight)
        {
            g_spawners[entityIndex].type = EntityType::ArrowDown;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    handleUnknownEntityType(entityIndex);
}

void handleArrowRight(int entityIndex)
{
    auto& state = g_gameState;

    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int row = state.entities[entityIndex].row;
    int col = state.entities[entityIndex].col;

    int belowIndex = state.bottomEntityMap[row][col];

    if (belowIndex == CELL_FLAG_EMPTY)
    {
        moveAndRedrawEntity(entityIndex, row + 1, col);
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (replaceEntityIfTargetMatches(entityIndex, row + 1, col) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (belowIndex < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    EntityType targetState = g_spawners[belowIndex].type;

    if (targetState == EntityType::DeflectorLeft)
    {
        state.entities[entityIndex].entityType = 2;
        moveAndRedrawEntity(entityIndex, row, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (targetState == EntityType::DeflectorRight)
    {
        state.entities[entityIndex].entityType = 1;
        moveAndRedrawEntity(entityIndex, row, col);
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleUnknownEntityType(int entityIndex) {}

void handlePusherLeft(int entityIndex)
{
    auto& state = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    int newRow = state.entities[entityIndex].row;
    int newCol = state.entities[entityIndex].col;
    int topIndex = state.topEntityMap[newRow][newCol];

    if (topIndex == -1)
    {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }
    if (replaceEntityIfTargetMatches(entityIndex, newRow - 1, newCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    state.entities[entityIndex].entityType = 0x0A;

    moveAndRedrawEntity(entityIndex, newRow, newCol);
    topIndex = state.topEntityMap[newRow][newCol];

    if (topIndex < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    auto& target = state.entities[topIndex];

    int targetRow = target.row - 1;
    int targetCol = target.col;
    int auxIndex = state.rightEntityMap[targetRow][targetCol];

    if (auxIndex != -1)
    {
        int auxAction = state.entities[auxIndex].entityType;

        if (auxAction != (int)EntityType::Lava)
        {
            handleUnknownEntityType(entityIndex);
            return;
        }
    }
    if (replaceEntityIfTargetMatches(topIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }
    moveAndRedrawEntity(topIndex, targetRow, targetCol);

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
    auto& state = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int newRow = state.entities[entityIndex].row;
    int newCol = state.entities[entityIndex].col;

    int leftIndex = state.leftEntityMap[newRow][newCol];

    if (leftIndex == -1)
    {
        moveAndRedrawEntity(entityIndex, newRow, newCol - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol - 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    state.entities[entityIndex].entityType = 8;

    moveAndRedrawEntity(entityIndex, newRow, newCol);

    leftIndex = state.leftEntityMap[newRow][newCol];

    if (leftIndex < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    auto& target = state.entities[leftIndex];

    int targetRow = target.row;
    int targetCol = target.col - 1;

    int auxIndex = state.rightEntityMap[targetRow][targetCol];

    if (auxIndex != -1)
    {
        int auxAction = state.entities[auxIndex].entityType;

        if (auxAction != (int)EntityType::Lava)
        {
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    if (replaceEntityIfTargetMatches(leftIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    moveAndRedrawEntity(leftIndex, targetRow, targetCol);

    handleUnknownEntityType(entityIndex);
}


void handlePusherDown(int entityIndex)
{
    auto& state = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
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

    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol + 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    state.entities[entityIndex].entityType = 7;

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
        int auxAction = state.entities[auxIndex].entityType;

        if (auxAction != (int)EntityType::Lava)
        {
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    if (replaceEntityIfTargetMatches(rightIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    moveAndRedrawEntity(rightIndex, targetRow, targetCol);

    handleUnknownEntityType(entityIndex);
}

void handlePusherRight(int entityIndex)
{
    auto& state = g_gameState;

    if ((frameCounter % 5) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    int newRow = state.entities[entityIndex].row;
    int newCol = state.entities[entityIndex].col;

    int bottomIndex = state.bottomEntityMap[newRow][newCol];

    if (bottomIndex == -1)
    {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow + 1, newCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    state.entities[entityIndex].entityType = 9;

    moveAndRedrawEntity(entityIndex, newRow, newCol);

    bottomIndex = state.bottomEntityMap[newRow][newCol];

    if (bottomIndex < 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    auto& target = state.entities[bottomIndex];

    int targetRow = target.row + 1;
    int targetCol = target.col;

    int auxIndex = state.rightEntityMap[targetRow][targetCol];

    if (auxIndex != -1)
    {
        int auxAction = state.entities[auxIndex].entityType;

        if (auxAction != (int)EntityType::Lava)
        {
            handleUnknownEntityType(entityIndex);
            return;
        }
    }

    if (replaceEntityIfTargetMatches(bottomIndex, targetRow, targetCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    moveAndRedrawEntity(bottomIndex, targetRow, targetCol);

    handleUnknownEntityType(entityIndex);
}

void handleCurvedArrowUp(int entityIndex)
{
    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int newRow = pendingRow;
    const int newCol = pendingCol;

    const int leftIndex  = g_gameState.rightEntityMap[newRow][newCol];
    const int downIndex  = g_gameState.bottomEntityMap[newRow][newCol];
    const int upIndex    = g_gameState.topEntityMap[newRow][newCol];
    const int diagIndex  = g_gameState.auxTopRightEntityMap[newRow][newCol];

    if (leftIndex == CELL_FLAG_EMPTY)
    {
        moveAndRedrawEntity(entityIndex, newRow, newCol - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol - 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    bool allowDown =
        (downIndex == CELL_FLAG_EMPTY &&
         upIndex == CELL_FLAG_EMPTY);

    bool allowUp =
        (downIndex == CELL_FLAG_EMPTY &&
         diagIndex == CELL_FLAG_EMPTY);

    if (allowDown && allowUp)
    {
        const bool goDown = legacyRandomBit();

        const int targetRow = goDown
            ? (newRow + 1)
            : (newRow - 1);

        moveAndRedrawEntity(entityIndex, targetRow, newCol - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowDown)
    {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowUp)
    {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (leftIndex >= 0 &&
        g_spawners[leftIndex].type == EntityType::DeflectorRight)
    {
        g_spawners[entityIndex].type = EntityType::CurvedArrowRight;

        moveAndRedrawEntity(entityIndex, newRow, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (leftIndex >= 0 &&
        g_spawners[leftIndex].type == EntityType::DeflectorLeft)
    {
        g_spawners[entityIndex].type = EntityType::CurvedArrowLeft;

        moveAndRedrawEntity(entityIndex, newRow, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleCurvedArrowDown(int entityIndex)
{
    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int newRow = pendingRow;
    const int newCol = pendingCol;

    const int rightIndex = g_gameState.leftEntityMap[newRow][newCol];

    if (rightIndex == CELL_FLAG_EMPTY)
    {
        moveAndRedrawEntity(entityIndex, newRow, newCol + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol + 1) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int upIndex    = g_gameState.topEntityMap[newRow][newCol];
    const int downIndex  = g_gameState.bottomEntityMap[newRow][newCol];
    const int diagUp     = g_gameState.auxTopRightEntityMap[newRow][newCol];
    const int diagDown   = g_gameState.auxBottomRightEntityMap[newRow][newCol];

    const bool allowUp =
        (upIndex == CELL_FLAG_EMPTY && diagUp == CELL_FLAG_EMPTY);

    const bool allowDown =
        (downIndex == CELL_FLAG_EMPTY && diagDown == CELL_FLAG_EMPTY);

    if (allowUp && allowDown)
    {
        const bool goDown = legacyRandomBit();
        const int targetRow = goDown ? (newRow + 1)
                                     : (newRow - 1);

        moveAndRedrawEntity(entityIndex, targetRow, newCol + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowUp)
    {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowDown)
    {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (rightIndex >= 0 &&
        g_spawners[rightIndex].type == EntityType::DeflectorRight)
    {
        g_spawners[entityIndex].type = EntityType::CurvedArrowLeft;

        moveAndRedrawEntity(entityIndex, newRow, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (rightIndex >= 0 &&
        g_spawners[rightIndex].type == EntityType::DeflectorLeft)
    {
        g_spawners[entityIndex].type = EntityType::CurvedArrowRight;

        moveAndRedrawEntity(entityIndex, newRow, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleCurvedArrowLeft(int entityIndex)
{
    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int newRow = pendingRow;
    const int newCol = pendingCol;

    const int upIndex = g_gameState.auxTopRightEntityMap[newRow][newCol];

    if (upIndex == CELL_FLAG_EMPTY)
    {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow - 1, newCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int leftIndex  = g_gameState.rightEntityMap[newRow][newCol];
    const int rightIndex = g_gameState.leftEntityMap[newRow][newCol];
    const int diagLeft   = g_gameState.auxMap127A[newRow][newCol];
    const int diagRight  = g_gameState.auxMap1282[newRow][newCol];

    const bool allowLeft  = (leftIndex  == CELL_FLAG_EMPTY && diagLeft  == CELL_FLAG_EMPTY);
    const bool allowRight = (rightIndex == CELL_FLAG_EMPTY && diagRight == CELL_FLAG_EMPTY);

    if (allowLeft && allowRight)
    {
        const bool goRight = legacyRandomBit();
        const int targetCol = goRight ? (newCol + 1)
                                      : (newCol - 1);

        moveAndRedrawEntity(entityIndex, newRow - 1, targetCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowLeft)
    {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol - 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowRight)
    {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol + 1);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (upIndex >= 0 &&
        g_spawners[upIndex].type == EntityType::DeflectorRight)
    {
        g_spawners[entityIndex].type = EntityType::CurvedArrowUp;

        moveAndRedrawEntity(entityIndex, newRow, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (upIndex >= 0 &&
        g_spawners[upIndex].type == EntityType::DeflectorLeft)
    {
        g_spawners[entityIndex].type = EntityType::CurvedArrowDown;

        moveAndRedrawEntity(entityIndex, newRow, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleCurvedArrowRight(int entityIndex)
{
    if (tryMoveSmartEntity(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (canEntityMove(entityIndex) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int newRow = pendingRow;
    const int newCol = pendingCol;

    const int belowIndex =
        g_gameState.bottomEntityMap[newRow][newCol];

    if (belowIndex == CELL_FLAG_EMPTY)
    {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol);
        handleUnknownEntityType(entityIndex);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow + 1, newCol) != 0)
    {
        handleUnknownEntityType(entityIndex);
        return;
    }

    const int leftIndex =
        g_gameState.leftEntityMap[newRow][newCol];

    const int rightIndex =
        g_gameState.rightEntityMap[newRow][newCol];

    const int diagLeft =
        g_gameState.auxTopRightEntityMap[newRow][newCol];

    const int diagRight =
        g_gameState.auxBottomRightEntityMap[newRow][newCol];

    const bool condRange =
        (belowIndex >= 0 &&
         g_spawners[belowIndex].type >= EntityType::CurvedArrowUp &&
         g_spawners[belowIndex].type <= EntityType::CurvedArrowRight);

    const bool condExact =
        (belowIndex >= 0 &&
         g_spawners[belowIndex].type ==
         EntityType::RoundedPushableBrick);

    bool allowDiagRight = false;

    if (rightIndex == CELL_FLAG_EMPTY &&
        diagLeft  == CELL_FLAG_EMPTY)
    {
        if (belowIndex == static_cast<int>(EntityType::Wall7) ||
            belowIndex == static_cast<int>(EntityType::Wall4) ||
            belowIndex == static_cast<int>(EntityType::Wall8) ||
            condRange ||
            condExact)
        {
            allowDiagRight = true;
        }
    }

    bool allowDiagLeft = false;

    if (leftIndex == CELL_FLAG_EMPTY &&
        diagRight == CELL_FLAG_EMPTY)
    {
        if (belowIndex == static_cast<int>(EntityType::Wall1) ||
            belowIndex == static_cast<int>(EntityType::Wall4) ||
            belowIndex == static_cast<int>(EntityType::Wall2) ||
            condRange ||
            condExact)
        {
            allowDiagLeft = true;
        }
    }

    if (allowDiagRight && allowDiagLeft)
    {
        const bool goLeft = legacyRandomBit();
        const int targetCol =
            goLeft ? (newCol - 1)
                   : (newCol + 1);

        moveAndRedrawEntity(entityIndex,
                            newRow + 1,
                            targetCol);

        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowDiagRight)
    {
        moveAndRedrawEntity(entityIndex,
                            newRow + 1,
                            newCol + 1);

        handleUnknownEntityType(entityIndex);
        return;
    }

    if (allowDiagLeft)
    {
        moveAndRedrawEntity(entityIndex,
                            newRow + 1,
                            newCol - 1);

        handleUnknownEntityType(entityIndex);
        return;
    }

    if (belowIndex >= 0 &&
        g_spawners[belowIndex].type ==
        EntityType::DeflectorLeft)
    {
        g_spawners[entityIndex].type =
            EntityType::CurvedArrowDown;

        moveAndRedrawEntity(entityIndex,
                            newRow,
                            newCol);

        handleUnknownEntityType(entityIndex);
        return;
    }

    if (belowIndex >= 0 &&
        g_spawners[belowIndex].type ==
        EntityType::DeflectorRight)
    {
        g_spawners[entityIndex].type =
            EntityType::CurvedArrowUp;

        moveAndRedrawEntity(entityIndex,
                            newRow,
                            newCol);

        handleUnknownEntityType(entityIndex);
        return;
    }

    handleUnknownEntityType(entityIndex);
}

void handleMonsterEntityType(int entityIndex)
{
    if (checkAndHandleDeathCondition(entityIndex) != 0)
        return;

    if ((frameCounter % 3) != 0)
        return;

    int rowOffset = 0;
    int colOffset = 0;

    // 3️⃣ tryMoveSmartEntity
    if (tryMoveSmartEntity(entityIndex) != 0)
        return;

    // 4️⃣ can move ?
    if (canEntityMove(entityIndex) == 0)
    {
        // --- random branch 1 ---
        pseudoRandomUpdate(0x00008000u);
        if ((randomSeedLow & 1u) != 0u)
        {
            adjustSmartEntityTarget(
                entityIndex,
                srcRow,
                srcCol,
                &pendingRow,
                &pendingCol
            );
        }
        else
        {
            // --- random branch 2 ---
            pseudoRandomUpdate(0x00008000u);

            bool axisRow = (randomSeedLow & 1u) != 0u;

            pseudoRandomUpdate(0x00008000u);

            int offset = (randomSeedLow % 3) - 1; // -1,0,+1

            if (axisRow)
                rowOffset = offset;
            else
                colOffset = offset;
        }
    }

    int newRow = pendingRow + rowOffset;
    int newCol = pendingCol + colOffset;

    int cell = g_gameState.entityMap[newRow][newCol];

    bool cellFree =
        (cell == CELL_FLAG_EMPTY) ||
        (cell >= 0 &&
         g_spawners[cell].type == EntityType::Lava);

    if (cellFree)
    {
        pendingRow = newRow;
        pendingCol = newCol;
    }

    if (replaceEntityIfTargetMatches(entityIndex, pendingRow, pendingCol))
        return;

    moveAndRedrawEntity(entityIndex, pendingRow, pendingCol);
}

static constexpr std::array<EntityHandler, 56> ENTITY_HANDLERS = {{
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
    handleSmartEntityAlt       // 47
}};

void gameMainLoopTick()
{
    // frameCounter++
    ++frameCounter;

    if (frameCounter > 0x7D00)
        frameCounter = 0;

    updateLevelEntitiesEvery30Frames();

    int entityIndex = 0;

    while (entityIndex < g_activeSpawnerCount)
    {
        uint16_t entityType = EntityTable[entityIndex];

        if (entityType <= 0x3B)
        {
            ENTITY_HANDLERS[entityType](entityIndex);
        }
        else
        {
            handleUnknownEntityType(entityIndex);
        }

        ++entityIndex;
    }
}
