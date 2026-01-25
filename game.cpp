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
#include <cstring>
#include "file.h"
#include "error.h"
#include "util.h"
#include "time.h"
#include "graph.h"
#include "dialog.h"
#include "game.h"
#include "legacy.h"

using i16 = std::int16_t;

constexpr uint8_t STATUS_FREE = 0x00;
constexpr uint8_t STATUS_USED = 0xFF;

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
uint16_t exitCoordLeft = 0xFFF9;
uint16_t exitCoordRight = 0xFFF9;
uint16_t exitState = 0xFFFB;
HCURSOR g_mainCursor = nullptr;
extern SpawnerSoA g_spawners;
extern uint16_t g_notificationHandlerSlotWord[]; // 0,1, or legacy function pointer value
extern uint8_t  g_notificationHandlerParam[];    // per-slot byte parameter

HBRUSH brush_black = nullptr;
HBRUSH brush_white = nullptr;
HBRUSH brush_blue  = nullptr;
HBRUSH brush_green = nullptr;
HBRUSH brush_red   = nullptr;

HPEN pen_black = nullptr;
HPEN pen_gray  = nullptr;
HPEN pen_blue  = nullptr;

HBITMAP bitmap_kye   = nullptr;
HBITMAP bitmap_block = nullptr;
HBITMAP bitmap_wall  = nullptr;

using NotificationHandlerFn = void (*)(uint16_t eventCode, uint16_t handlerParam);
extern uintptr_t g_handlerOrStateTable[];   // base == 0x1122 (en words dans le binaire)
extern uint8_t  g_handlerParamTable[];     // base == 0x1134 (byte par index)

i16 baseX = 0;
i16 baseY = 0;

GameState g_gameState{};

uint16_t randomSeedLow  = 0x4E35;
uint16_t randomSeedHigh = 0x015A;

int previousCol       = 0;
int previousRow       = 0;
int g_isMouseCaptured  = 0;
int someGlobalCondition = 0;

static bool g_isRendering = false;

inline char g_levelFilePath[MAX_PATH]{};
char displayTextBuffer[512]    = {};
char g_levelHintTextBuffer[512]          = {};

extern std::string g_levelRawText;
extern std::string g_levelHintText;
extern std::string g_levelDisplayText;

HWND g_mainHwnd = nullptr;
HDC  g_windowDC = nullptr;

constexpr int kMaxLevelEntries = 32;

LevelEntry g_levelEntries[kMaxLevelEntries] = {};
int16_t currentEntryIndex = 0;

i16 specialCellStateFlag = 0;

HINSTANCE g_hInstance = nullptr;
HWND g_hdc   = nullptr;
HWND g_hdc2  = nullptr;
std::string g_nameInputBuffer;

std::string g_statusLineText;

static bool g_running = true;
static bool g_keyDownFlag = false;
extern HudCellEntry g_hudCells[];

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

void runScheduledCallbacks(std::vector<CallbackEntry>& table) {
    while (true) {
        int bestIndex = -1;
        uint8_t bestPriority = STATUS_USED;

        for (size_t i = 0; i < table.size(); ++i) {
            const CallbackEntry& entry = table[i];
            if (entry.status == STATUS_USED) continue;
            if (entry.priority < bestPriority) {
                bestPriority = entry.priority;
                bestIndex = static_cast<int>(i);
            }
        }

        if (bestIndex == -1) break; // plus de callbacks disponibles

        CallbackEntry& selected = table[bestIndex];
        selected.status = STATUS_USED;

        if (selected.func) {
            selected.func();
        }

        // continue la boucle, comme `jmp short sub_E7`
    }
}

void startPollingTimer() {
    // Démarre un timer de 100ms sans fenêtre ni fonction de rappel
    g_timerId = SetTimer(
        /* hWnd        */ nullptr,
        /* nIDEvent    */ 0,
        /* uElapse     */ 100,  // 100ms
        /* lpTimerFunc */ nullptr
    );

    if (g_timerId != 0) {
        g_timerActive = true;
    } else {
        // Gestion d'erreur éventuelle
        MessageBox(nullptr, L"Échec du démarrage du timer", L"Erreur", MB_ICONERROR);
    }
}

void advanceToNextLevelOrBlock()
{
    if (g_hasDeviceContext == 0) {
        return;
    }

    g_hasDeviceContext = 0;
}

void updateLevelEntitiesEvery30Frames() {
    if (frameCounter % 30 != 0) return;

    for (int i = 0; i < g_activeSpawnerCount; ++i) {
        auto& entry = g_gameState.entities[i];

        if (entry.actionCode < 0x32 || entry.actionCode > 0x3B) {
            continue;
        }

        int row = entry.row;
        int col = entry.col;

        if (entry.actionCode > 0x32) {
            --entry.actionCode;
            moveAndRedrawEntity(i, row, col);  // Possibly triggers animation or state shift
        } else {
            drawRectangleFromGrid(row, col);
            markEntryInactive(i);
        }
    }

    finalizeLevelVisuals();
}

void moveAndRedrawEntity(int entityIndex, int newRow, int newCol) {
    auto& entity = g_gameState.entities[entityIndex];

    // 1. Effacer ancienne position
    drawRectangleFromGrid(entity.row, entity.col);

    // 2. Mettre à jour la position logique
    entity.row = newRow;
    entity.col = newCol;

    // 3. Mettre à jour la grille logique (si lookup rapide)
    int offset = (entity.row * 40) + (entity.col * 2); // 0x28 = 40 en décimal, *2 = word addressing
    if (offset < static_cast<int>(entityGrid.size())) {
        entityGrid[offset] = entityIndex;
    }

    // 4. Afficher l’entité à la nouvelle position
    renderEntityToWindow(entityIndex);
}


void showNewLevelDialog() {
    // Blocage de la boucle principale
    bool waiting = true;
    SDL_Event e;

    // Dessiner un message de "Nouveau niveau"
    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_INFORMATION,
        "Niveau suivant",
        "Appuyez sur OK pour commencer.",
        gWindow
    );

    // Attente manuelle si on ne veut pas de boîte native :
    /*
    while (waiting) {
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN) {
                waiting = false;
            }
            if (e.type == SDL_QUIT) {
                waiting = false;
                SDL_Quit();
                exit(0);
            }
        }
        SDL_Delay(16);
    }
    */
}

bool tryMoveSmartEntity(int entityIndex) {
    auto& entity = g_gameState.entities[entityIndex];
    int row = entity.row;
    int col = entity.col;
    int offset = row * 40 + col * 2;

    // 1. Tentative à droite
    if (entityGrid[offset] == -1) {
        int rightValue = rightMap[row][col];
        if (rightValue >= 0) {
            int targetIndex = rightValue;
            if (g_gameState.entities[targetIndex].actionCode == 5) {
                moveAndRedrawEntity(entityIndex, row, col + 1);
                return true;
            }
        }
    }

    // 2. Tentative à gauche
    if (leftGrid[row][col] == -1) {
        int leftValue = leftMap[row][col];
        if (leftValue >= 0) {
            int targetIndex = leftValue;
            if (g_gameState.entities[targetIndex].actionCode == 5) {
                moveAndRedrawEntity(entityIndex, row, col - 1);
                return true;
            }
        }
    }

    // 3. Tentative en bas
    if (downGrid[row][col] == -1) {
        int downValue = downMap[row][col];
        if (downValue >= 0) {
            int targetIndex = downValue;
            if (g_gameState.entities[targetIndex].actionCode == 6) {
                moveAndRedrawEntity(entityIndex, row + 1, col);
                return true;
            }
        }
    }

    // 4. Tentative en haut
    if (upGrid[row][col] == -1) {
        int upValue = upMap[row][col];
        if (upValue >= 0) {
            int targetIndex = upValue;
            if (g_gameState.entities[targetIndex].actionCode == 6) {
                moveAndRedrawEntity(entityIndex, row - 1, col);
                return true;
            }
        }
    }

    return false;
}

int canEntityMove(int entityIndex) {
    int row = entityRowTable[entityIndex];
    int col = entityColTable[entityIndex];

    int index;
    int type;

    // Droite
    index = entityMap[row][col + 1];
    if (index >= 0 && entityTypeTable[index] == 5)
        return 1;

    // Gauche
    index = entityMap[row][col - 1];
    if (index >= 0 && entityTypeTable[index] == 5)
        return 1;

    // Bas
    index = entityMap[row + 1][col];
    if (index >= 0 && entityTypeTable[index] == 6)
        return 1;

    // Haut
    index = entityMap[row - 1][col];
    if (index >= 0 && entityTypeTable[index] == 6)
        return 1;

    return 0;
}


bool checkAndHandleDeathCondition(int changeIndex) {
    const LevelChange& entry = changeList[changeIndex];
    int row = entry.row;
    int col = entry.col;

    bool isPlayerThere =
        g_state.bottomEntityMap[row][col] == 0xFFFE ||
        gameGrid[row][col] == 0xFFFE ||
        g_state.topEntityMap[row][col] == 0xFFFE ||
        g_state.bottomEntityMap[row][col] == 0xFFFE;

    if (isPlayerThere) {
        --remainingLives;
        updateLivesDisplay();     // sub_337F
        triggerDeathEffect();     // sub_2847
        return true;
    }

    return false;
}

void updateLivesDisplay() {
    hasLevelTransition = 1;

    previousRow = spawnCol;
    previousCol = spawnRow;

    int x1 = spawnCol - 1;
    int y1 = spawnRow + 1;
    int scanStep = 2;
    int x2 = spawnCol + 1;
    int y2 = spawnRow - 1;
    int iteration = 1;

    while (iteration < 5) {
        for (int i = 0; i < scanStep; ++i, ++y1) {
            previousRow = x1;
            previousCol = y1;
            if (gameGrid[x1][y1] == 0xFFFF) return;
        }

        for (int i = 0; i < scanStep; ++i, --x2) {
            previousRow = x2;
            previousCol = y1;
            if (gameGrid[x2][y1] == 0xFFFF) return;
        }

        for (int i = 0; i < scanStep; ++i, --y2) {
            previousRow = x2;
            previousCol = y2;
            if (gameGrid[x2][y2] == 0xFFFF) return;
        }

        for (int i = 0; i < scanStep; ++i, ++x1) {
            previousRow = x1;
            previousCol = y2;
            if (gameGrid[x1][y2] == 0xFFFF) return;
        }

        --x1;
        ++y1;
        scanStep += 2;
        ++x2;
        --y2;
        ++iteration;
    }

    previousRow = row;
    previousCol = col;
}

int loadLevelByIndex(int level)
{
    if (!fileAccessEnabled) {
        return 0;
    }

    FileLike* file = openAndPrepareFileFromSlot(g_selectedFilePath, "r");
    if (!file) {
        showMessage(g_selectedFilePath, "Cannot open file: ");
        resetLevelStateMemory();
        fileAccessEnabled = false;
        return;
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

void resetLevelStateMemory() {
    g_activeSpawnerCount = 0;

    std::memcpy(loadedLevelName, reinterpret_cast<const void*>(0x045A), 3 * sizeof(uint16_t));
    std::memcpy(loadedLevelHint, reinterpret_cast<const void*>(0x0460), 4 * sizeof(uint16_t));
    std::memcpy(displayTextBuffer, reinterpret_cast<const void*>(0x0468), 7 * sizeof(uint16_t));

    for (int row = 0; row < 20; ++row) {
        for (int col = 0; col < 30; ++col) {
            g_state.bottomEntityMap[row][col] = 0xFFF9;
        }
    }

    // Reset de la grille logique (états) : g_state.bottomEntityMap[19][29]
    for (int row = 0; row < 19; ++row) {
        for (int col = 0; col < 29; ++col) {
            g_state.bottomEntityMap[row][col] = 0xFFFF;
        }
    }

    // Réinitialisation des métadonnées de niveau
    exitCoordLeft     = 0xFFF9;
    exitCoordRight    = 0xFFF9;
    exitState         = 0xFFFB;

    cursorRow         = 3;
    cursorCol         = 3;
    spawnCol          = 3;
    spawnRow          = 3;

    selectedTileValue = 0xFFFE;
    selectionState    = 0xFFF3;
    selectedEntityIndex = 0xFFFF;
}

bool decodeTile(char inputChar, uint16_t& outCode, uint16_t& outParam) {
    for (int i = 0; ; ++i) {
        const TileMapping& entry = tileMap[i];
        if (entry.tileCode == 0xFFFF)  // end of list
            return false;

        if (entry.symbol == static_cast<uint8_t>(inputChar)) {
            outCode = entry.tileCode;
            outParam = entry.param;
            return true;
        }
    }
}

bool registerLevelChange(uint16_t tileId, uint16_t row, uint16_t col) {
    if (g_activeSpawnerCount >= 0x258)
        return false;

    // 2D grid version marker
    g_state.rightEntityMap[row][col] = g_activeSpawnerCount;

    // Register into linear changeList
    LevelChange& entry = changeList[g_activeSpawnerCount];
    entry.tileId = tileId;
    entry.row = row;
    entry.col = col;
    entry.extra = 0;

    ++g_activeSpawnerCount;
    return true;
}

void loadLevelRow(int columnIndex, const char* lineData) {
    if (columnIndex >= 20 || !lineData) return;

    uint16_t* gridPtr = &gameGrid[columnIndex][0];

    // Réinitialise toute la colonne de la grille principale à 0xFFFF
    for (int row = 0; row < 30; ++row) {
        gridPtr[row] = 0xFFFF;
    }

    const char* linePtr = lineData;
    int row = 0;

    while (*linePtr != '\0' && row < 30) {
        char tileType = *linePtr++;
        uint16_t tileCode = 0;
        uint16_t tileId = 0;

        if (!decodeTile(tileType, tileCode, tileId)) {
            continue;
        }

        switch (tileCode) {
            case 0: // Empty tile
                gridPtr[row] = 0xFFFF;
                break;

            case 1: // Static tile
                gridPtr[row] = tileId;
                break;

            case 2: { // Special tile (RNG or registered tile)
                int changeIndex = registerLevelChange(tileId, row, columnIndex);

                if (tileId >= 0x0F && tileId <= 0x1B) {
                    pseudoRandomUpdate();

                    // Remplace shiftLeft64(randomSeedLow, randomSeedHigh, 2)
                    uint64_t seed = (static_cast<uint64_t>(randomSeedHigh) << 16) | randomSeedLow;
                    seed <<= 2;
                    randomSeedHigh = static_cast<uint16_t>(seed >> 16);
                    randomSeedLow = static_cast<uint16_t>(seed & 0xFFFF);

                    levelChangeTable[changeIndex] = static_cast<uint16_t>(randomSeedHigh & 0x7FFF);
                } else if (tileId == 0x17 + (row % 4)) {
                    registerLevelChange(0x17 + (row % 4), row, columnIndex);
                } else if (tileId == 0x1B + (row % 4)) {
                    registerLevelChange(0x1B + (row % 4), row, columnIndex);
                } else {
                    registerLevelChange(tileId, row, columnIndex);
                }
                break;
            }

            case 3: // Player spawn point
                gridPtr[row] = 0xFFFE;
                playerRow = row;
                playerCol = columnIndex;
                spawnRow = row;
                spawnCol = columnIndex;
                break;

            default:
                break;
        }

        ++row;
    }
}

static inline void invalidateIfIndex(int16_t& cell) {
    const int16_t v = cell;
    if (v < kIndexMin || v > kIndexMax) {
        if (v >= 0) {
            markEntryInactive(v);
        }
        cell = kTileInactiveSentinel;
    }
}

int postLoadLevel() {
    int countSel = 0;
    for (int r = 0; r < kGridRows && countSel == 0; ++r) {
        for (int c = 0; c < kGridCols; ++c) {
            if (g_gridMain[r][c] == kTileSelectionSentinel) {
                ++countSel;
                break;
            }
        }
    }
    if (countSel == 0) {
        selectionState = kTileSelectionSentinel;
    }

    int foundSelected = 0;
    int foundRow = 0;
    int foundCol = 0;
    for (int r = 0; r < kGridRows && foundSelected == 0; ++r) {
        for (int c = 0; c < kGridCols; ++c) {
            if (g_gridMain[r][c] == kTileSelectedSentinel) {
                foundSelected = 1;
                foundRow = r;
                foundCol = c;
                break;
            }
        }
    }

    if (foundSelected == 0 || foundRow != srcRow || foundCol != srcCol) {
        srcRow = 3;
        srcCol = 3;
        spawnRow = 3;
        spawnCol = 3;
        selectedTileValue = kTileSelectedSentinel;
    }

    for (int c = 0; c < kGridCols; ++c) {
        for (int r = 0; r < kGridRows; ++r) {
            invalidateIfIndex(g_gridMain[r][c]);
        }
    }
    for (int r = 0; r < kGridRows; ++r) {
        for (int c = 0; c < kGridCols; ++c) {
            invalidateIfIndex(g_gridAuxA[r][c]);
        }
    }
    for (int r = 0; r < kGridRows; ++r) {
        for (int c = 0; c < kGridCols; ++c) {
            invalidateIfIndex(g_gridAuxB[r][c]);
        }
    }

    finalizeLevelVisuals();
    return 1;
}

int replaceEntityIfTargetMatches(int index, int newRow, int newCol) {
    const int16_t cell = g_entityIndexGrid[newRow][newCol];
    if (cell < 0) {
        return 0;
    }

    const int16_t targetEntityIndex = cell;
    if (g_entitySlots[targetEntityIndex].state != kTargetRequiredState) {
        return 0;
    }

    const int16_t oldRow = g_entitySlots[static_cast<int16_t>(index)].row;
    const int16_t oldCol = g_entitySlots[static_cast<int16_t>(index)].col;

    g_entitySlots[targetEntityIndex].state = kTargetClearedState;
    g_entitySlots[targetEntityIndex].extra = 0;

    moveAndRedrawEntity(static_cast<int16_t>(targetEntityIndex),
                        static_cast<int16_t>(newRow),
                        static_cast<int16_t>(newCol));

    drawRectangleFromGrid(oldRow, oldCol);

    markEntryInactive(static_cast<int16_t>(index));
    return 1;
}

void markEntryInactive(int index) {
    if (index >= 0 && index < MAX_CHANGES) {
        changeList[index].tileId = 0x00FF;
    }
}

void finalizeLevelVisuals() {
    int i = 0;
    while (i < g_activeSpawnerCount) {
        if (changeList[i].tileId == 0x00FF) {
            if (g_activeSpawnerCount < 1) break;
            --g_activeSpawnerCount;

            // Décale tous les éléments suivants
            for (int j = i; j < g_activeSpawnerCount; ++j) {
                changeList[j] = changeList[j + 1];
            }

            // Corrige les indices dans la table
            for (int y = 1; y < 0x13; ++y) {
                for (int x = 1; x < 0x1D; ++x) {
                    int& val = levelChangeTable[y * 0x28 + x];  // base à 0x12A8
                    if (val > i)
                        --val;
                }
            }
        } else {
            ++i;
        }
    }
}

void handleDialogClose() {
    if (hasLevelTransition != 0) {
        runEffect(2); // Effet visuel/sonore pour fermeture de dialogue
        drawRectangleFromGrid(row, col);

        row = previousRow;
        col = previousCol;

        // Positionne le joueur à sa précédente position
        g_state.bottomEntityMap[row][col] = 0xFFFE;

        runEffect(1); // Peut-être un effet de confirmation
        hasLevelTransition = 0;
        return;
    }

    // Si aucune transition et clic à la même position : rien à faire
    if (previousRow == row && previousCol == col) {
        return;
    }

    // Calcule les directions de déplacement
    int deltaRow = previousRow - row;
    int deltaCol = previousCol - col;

    int directionRow = (deltaRow > 0) ? 1 : (deltaRow < 0 ? -1 : 0);
    int directionCol = (deltaCol > 0) ? 1 : (deltaCol < 0 ? -1 : 0);

    // Applique la direction de mouvement
    processDialogMovement(directionRow, directionCol);
}

int handlePointClick(int x, int y) {
    if (isPointInRect(x, y)) {
        if (someGlobalCondition == 0) {
            initializeWindowHandleIfNeeded();
            releaseDialogResources();
        }
    }
    return 0;
}

void handlePendingBlock(int rowIndex, int colIndex) {
    if (isPendingDraw && pendingRow == rowIndex && pendingCol == colIndex) {
        return;
    }

    maybeDrawPendingRectangle();
    pendingRow = rowIndex;
    pendingCol = colIndex;

    int dRow = std::abs(rowIndex - row);
    int dCol = std::abs(colIndex - col);
    if (dRow <= 1 && dCol <= 1) {
        return;
    }

    int offset = rowIndex * 0x28 + (colIndex << 1); // 0x28 = row stride
    uint16_t value = *reinterpret_cast<uint16_t*>(0x127E + offset);
    if (value == 0xFFFF) {
        drawPendingBlock();
        isPendingDraw = 1;
    }
}

bool hasSpecialCell() {
    for (int y = 0; y < GameState::GRID_ROWS; ++y) {
        for (int x = 0; x < GameState::GRID_COLS; ++x) {
            if (gameGrid[y][x] == 0xFFFE) {
                return true;
            }
        }
    }
    return false;
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

static void drainPendingEvents()
{
    while (g_pendingEventCount != 0)
    {
        --g_pendingEventCount;
        g_eventHandlers[g_pendingEventCount]();
    }
}

void handleEngineEvent(int exitCode, int skipExit, int skipPreDrain)
{
    if (skipPreDrain == 0) {
        drainPendingEvents();
        processCallbackQueueFromEngineEvent();
        if (g_validateInternalBufferCallback) {
            g_validateInternalBufferCallback();
        }
    }

    if (skipExit != 0) {
        return;
    }

    if (skipPreDrain == 0) {
        configureFileMode();
        invokeExternalCallback();
    }

    cleanupAndTerminate(exitCode);
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

static CallbackQueueEntry* selectHighestPriorityEntry(CallbackQueueEntry* begin, CallbackQueueEntry* end)
{
    uint8_t bestPriority = 0;
    CallbackQueueEntry* best = end;

    for (auto* e = begin; e < end; ++e) {
        if (e->state != 0xFF && e->priority >= bestPriority) {
            bestPriority = e->priority;
            best = e;
        }
    }

    return best;
}

void processCallbackQueue(CallbackQueueEntry* begin, CallbackQueueEntry* end)
{
    for (;;) {
        CallbackQueueEntry* e = selectHighestPriorityEntry(begin, end);
        if (e == end) return;

        CallbackFn cb = e->fn;
        e->state = 0xFF;

        if (cb) {
            cb();
        }
    }
}

void runLegacyCallbackQueue(CallbackQueueEntry* begin, CallbackQueueEntry* end)
{
    for (;;) {
        uint8_t bestPriority = 0xFF;
        CallbackQueueEntry* best = end;

        for (auto* e = begin; e < end; ++e) {
            if (e->state != 0xFF && e->priority <= bestPriority) {
                bestPriority = e->priority;
                best = e;
            }
        }

        if (best == end) return;

        CallbackFn cb = best->fn;
        best->state = 0xFF;

        if (cb) {
            cb();
        }
    }
}

void bufferCopyCallback(char** bufferPtrRef, const char* data, int length) {
    std::memcpy(*bufferPtrRef, data, length);
    *bufferPtrRef += length;
    **bufferPtrRef = '\0';
}


int prepareAndCallProcessMainLoop(char* outputBuffer, int value, int type) {
    char buffer[256] = {};
    char* outputPtr = buffer;
    uint8_t someType = 0x01;

    // Appel principal
    processMainLoop(bufferCopyCallback, &outputPtr, 42, &someType);

    return 0; // La version assembleur ne retourne rien, mais tu peux adapter si nécessaire
}

void handleMoveLeft(int entityIndex, int newRow, int newCol) {
    if (tryMoveSmartEntity(entityIndex))
        return; // le déplacement intelligent a échoué → rien à faire

    if (canEntityMove(entityIndex))
        return; // bloqué → fin

    // Vérifie la cellule à gauche
    int leftCol = newCol - 1;
    int targetEntityId = g_gameState.rightEntityMap[newRow][newCol]; // 0x127C dans l’ASM

    if (targetEntityId == -1) {
        moveAndRedrawEntity(entityIndex, newRow, leftCol);
        return;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow, leftCol))
        return;

    // Vérifie le type de l'entité à droite
    if (targetEntityId >= 0) {
        int neighborType = g_gameState.entities[targetEntityId].actionCode;

        // Si voisin de type 0x14 → force le type actuel à 4
        if (neighborType == 0x14) {
            g_gameState.entities[entityIndex].actionCode = 4;
            return;
        }

        // Si voisin de type 0x15 → force le type actuel à 3
        if (neighborType == 0x15) {
            g_gameState.entities[entityIndex].actionCode = 3;
            return;
        }
    }
}

int getAuxTopRightMapValue(int row, int col)
{
    if (row < 0 || row >= GameState::GRID_ROWS ||
        col < 0 || col >= GameState::GRID_COLS) {
        return CELL_EMPTY; // 0xFFFF
    }
    return g_gameState.auxTopRightEntityMap[row][col];
}

int getAuxBottomRightMapValue(int row, int col)
{
    if (row < 0 || row >= GameState::GRID_ROWS ||
        col < 0 || col >= GameState::GRID_COLS) {
        return CELL_EMPTY;
    }
    return g_gameState.auxBottomRightEntityMap[row][col];
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


void handleMoveRight(int entityIndex, int newRow, int newCol) {
    // 1️⃣ tentative de déplacement “intelligent”
    if (tryMoveSmartEntity(entityIndex))
        return; // échec, on s’arrête

    // 2️⃣ sinon, test si l’entité peut bouger
    if (canEntityMove(entityIndex))
        return;

    // 3️⃣ Vérifie la cellule à droite
    int rightCol = newCol + 1;
    int targetEntityId = g_gameState.entityMap[newRow][newCol]; // 0x1280 dans l’ASM

    // Si vide : déplacement direct à droite
    if (targetEntityId == -1) {
        moveAndRedrawEntity(entityIndex, newRow, rightCol);
        return;
    }

    // 4️⃣ Sinon, tente de remplacer la cible
    if (replaceEntityIfTargetMatches(entityIndex, newRow, rightCol))
        return;

    // 5️⃣ Vérifie le type de l’entité à droite
    if (targetEntityId >= 0) {
        int neighborType = g_gameState.entities[targetEntityId].actionCode;

        // Si type 0x14 → force le type courant à 3 et se déplace
        if (neighborType == 0x14) {
            g_gameState.entities[entityIndex].actionCode = 3;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }

        // Si type 0x15 → force le type courant à 4 et se déplace
        if (neighborType == 0x15) {
            g_gameState.entities[entityIndex].actionCode = 4;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }
    }
}


void handleMoveUp(int entityIndex, int newRow, int newCol) {
    // 1️⃣ tentative de déplacement “intelligent”
    if (tryMoveSmartEntity(entityIndex))
        return; // échec → stop

    // 2️⃣ test si le mouvement est possible
    if (canEntityMove(entityIndex))
        return;

    // 3️⃣ vérifie la cellule au-dessus
    int aboveRow = newRow - 1;
    int targetEntityId = g_gameState.topEntityMap[newRow][newCol]; // 0x1256

    // Si la case est vide → déplacement direct
    if (targetEntityId == -1) {
        moveAndRedrawEntity(entityIndex, aboveRow, newCol);
        return;
    }

    // 4️⃣ sinon, tente de remplacer la cible
    if (replaceEntityIfTargetMatches(entityIndex, aboveRow, newCol))
        return;

    // 5️⃣ vérifie le type du voisin en dessous (logique inverse)
    if (targetEntityId >= 0) {
        int neighborType = g_gameState.entities[targetEntityId].actionCode;

        // si voisin type 0x14 → force type 1 et se déplace
        if (neighborType == 0x14) {
            g_gameState.entities[entityIndex].actionCode = 1;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }

        // si voisin type 0x15 → force type 2 et se déplace
        if (neighborType == 0x15) {
            g_gameState.entities[entityIndex].actionCode = 2;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }
    }
}

void handleMoveDown(int entityIndex, int newRow, int newCol) {
    // 1️⃣ tentative de déplacement “intelligent”
    if (tryMoveSmartEntity(entityIndex))
        return;

    // 2️⃣ test si le mouvement est possible
    if (canEntityMove(entityIndex))
        return;

    // 3️⃣ vérifie la cellule en dessous
    int belowRow = newRow + 1;
    int targetEntityId = g_gameState.bottomEntityMap[newRow][newCol]; // 0x12A6

    // Si la case est vide → déplacement direct
    if (targetEntityId == -1) {
        moveAndRedrawEntity(entityIndex, belowRow, newCol);
        return;
    }

    // 4️⃣ sinon, tente de remplacer la cible
    if (replaceEntityIfTargetMatches(entityIndex, belowRow, newCol))
        return;

    // 5️⃣ vérifie le type du voisin en dessous
    if (targetEntityId >= 0) {
        int neighborType = g_gameState.entities[targetEntityId].actionCode;

        // Si voisin type 0x14 → force type 2 et se déplace
        if (neighborType == 0x14) {
            g_gameState.entities[entityIndex].actionCode = 2;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }

        // Si voisin type 0x15 → force type 1 et se déplace
        if (neighborType == 0x15) {
            g_gameState.entities[entityIndex].actionCode = 1;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }
    }
}


int gameMainLoop() {
    ++frameCounter;
    if (frameCounter > 0x7D00)
        frameCounter = 0;

    updateLevelEntitiesEvery30Frames();

    for (int entityIndex = 0; entityIndex < g_activeSpawnerCount; ++entityIndex) {
        auto& entity = g_gameState.entities[entityIndex];
        int row  = entity.row;
        int col  = entity.col;

        switch (entity.actionCode) {
            case 0x00:
                handleSmartEntityCommon(entityIndex);
                break;
                handleSmartEntityAlt(entityIndex);
                break;
            case 0x01: // gauche
                handleMoveLeft(entityIndex, row, col);
                break;
            case 0x02: // droite
                handleMoveRight(entityIndex, row, col);
                break;
            case 0x03: // haut
                handleMoveUp(entityIndex, row, col);
                break;
            case 0x04: // bas
                handleMoveDown(entityIndex, row, col);
                break;
            case 0x05:
                handleStickyFollower(entityIndex, row, col);
                break;
            case 0x06:
                handleStickyFollower2(entityIndex, row, col);
                break;
            case 0x08:
                handleSmartRightPusher(entityIndex, row, col);
                break;
            case 0x09:
                handleSmartUpPusher(entityIndex, row, col);
                break;
            case 0x0A:
                handleSmartDownPusher(entityIndex, row, col);
                break;
            case 0x0B: // 11
                handleSmartLeftCrawler(entityIndex, row, col);
                break;
            case 0x0C: // 12
                handleSmartRightCrawler(entityIndex, row, col);
                break;
            case 0x0D: // 13
                handleSmartUpCrawler(entityIndex, row, col);
                break;
            case 0x0E: // 14
                handleSmartDownCrawler(entityIndex, row, col);
                break;

            default:
                // handleUnknownEntityType(entityIndex);
                break;
        }
    }

    return 0;
}

int getAuxBottomMapValue(int row, int col); // correspond à [base+12A4h]
int getAuxTopMapValue(int row, int col);    // correspond à [base+1254h]

// Action interne 0x0B (handleEntityType11Loc dans l’asm)
void handleSmartLeftCrawler(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    // 1) Premier essai de mouvement “intelligent”
    if (tryMoveSmartEntity(entityIndex) != 0) {
        return; // déjà géré ailleurs
    }

    // 2) Vérifier si l’entité est autorisée à se déplacer
    if (canEntityMove(entityIndex) != 0) {
        return;
    }

    const int newRow = row;
    const int newCol = col;

    // 3) Regarder la case “latérale” (ASM: [base+127Ch])
    int sideIndex = state.rightEntityMap[newRow][newCol]; // di dans l’ASM

    if (sideIndex == -1) {
        // Case libre => déplacement simple vers la gauche
        moveAndRedrawEntity(entityIndex, newRow, newCol - 1);
        return;
    }

    // 4) Essayer de remplacer ce qu’il y a à gauche si ça matche une règle
    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol - 1)) {
        return;
    }

    // --- 5) Lecture des voisins verticaux / “parallèles” ---

    // En dessous (map +12A6h)
    int belowIndex = state.bottomEntityMap[newRow][newCol]; // var_8

    // Au-dessus (map +1256h)
    int aboveIndex = state.topEntityMap[newRow][newCol];    // dx

    // Deux autres maps parallèles (offsets +12A4h et +1254h)
    int auxBelow = getAuxBottomMapValue(newRow, newCol);    // var_A
    int auxAbove = getAuxTopMapValue(newRow, newCol);       // var_C

    // --- 6) Tests sur le type de la chose en “di” (sideIndex) ---

    bool sideInRange0Bto0E = false;
    bool sideIsType16      = false;

    if (sideIndex >= 0) {
        const auto& sideEntity = state.entities[sideIndex];
        int sideType = sideEntity.actionCode;

        if (sideType >= 0x0B && sideType <= 0x0E) {
            sideInRange0Bto0E = true;
        }
        if (sideType == 0x16) {
            sideIsType16 = true;
        }
    }

    int flagRange0Bto0E = sideInRange0Bto0E ? 1 : 0; // cx
    int flagType16      = sideIsType16      ? 1 : 0; // bx

    // --- 7) Première condition composée (dxFlag) ---

    int dxFlag = 0;
    {
        bool cond = false;

        // aboveIndex == 0xFFFF, auxAbove == 0xFFFF → libres
        if (aboveIndex == -1 && auxAbove == -1) {
            // di == 0xFFFD || 0xFFFC || 0xFFFA
            bool diIsSpecialWall =
                (sideIndex == 0xFFFD) ||
                (sideIndex == 0xFFFC) ||
                (sideIndex == 0xFFFA);

            if (diIsSpecialWall ||
                flagRange0Bto0E != 0 ||
                flagType16      != 0) {
                cond = true;
            }
        }

        dxFlag = cond ? 1 : 0;
    }

    // --- 8) Deuxième condition composée (bxFlagFinal) ---

    int bxFlagFinal = 0;
    {
        bool cond = false;

        // belowIndex == 0xFFFF, auxBelow == 0xFFFF → libres
        if (belowIndex == -1 && auxBelow == -1) {
            // di == 0xFFFB || 0xFFFC || 0xFFF8
            bool diIsOtherWall =
                (sideIndex == 0xFFFB) ||
                (sideIndex == 0xFFFC) ||
                (sideIndex == 0xFFF8);

            if (diIsOtherWall ||
                flagRange0Bto0E != 0 ||
                flagType16      != 0) {
                cond = true;
            }
        }

        bxFlagFinal = cond ? 1 : 0;
    }
 bool condRandomMove = (bxFlagFinal != 0) && (dxFlag != 0);

    if (condRandomMove) {
        // Mouvement “aléatoire” vertical en plus du déplacement à gauche
        pseudoRandomUpdate();
        bool goUp = (randomSeedLow & 1u) != 0;

        int targetRow = goUp ? (newRow - 1) : (newRow + 1);
        moveAndRedrawEntity(entityIndex, targetRow, newCol - 1);
        return;
    }

    // Sinon, si bxFlagFinal != 0: mouvement bas-gauche
    if (bxFlagFinal != 0) {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol - 1);
        return;
    }

    // Sinon, si dxFlag != 0: mouvement haut-gauche
    if (dxFlag != 0) {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol - 1);
        return;
    }

    // --- 10) Sinon, adaptation du type de l’entité en “di” ---

    if (sideIndex >= 0) {
        auto& sideEntity = state.entities[sideIndex];
        int& sideType    = sideEntity.actionCode;

        if (sideType == 0x14) {
            // devient 0x0E
            sideType = 0x0E;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }

        if (sideType == 0x15) {
            // devient 0x0D
            sideType = 0x0D;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }
    }
}

void handleSmartUpCrawler(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    // 1. Mouvement "intelligent" de base
    if (tryMoveSmartEntity(entityIndex) != 0)
        return;

    // 2. Vérifier possibilité de mouvement
    if (canEntityMove(entityIndex) != 0)
        return;

    const int newRow = row;
    const int newCol = col;

    // 3. Lire la carte du dessus : [base+1256h]
    int aboveIndex = state.topEntityMap[newRow][newCol];
    if (aboveIndex == -1) {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol);
        return;
    }

    // 4. Tenter un remplacement sur la case au-dessus
    if (replaceEntityIfTargetMatches(entityIndex, newRow - 1, newCol))
        return;

    // 5. Lecture des cartes environnantes
    int rightIndex  = state.rightEntityMap[newRow][newCol];   // 0x127C
    int centerIndex = state.entityMap[newRow][newCol];        // 0x1280
    int leftIndex   = state.leftEntityMap[newRow][newCol];    // 0x1254
    int bottomIndex = state.bottomEntityMap[newRow][newCol];  // 0x1258

    // 6. Identifier le type de l'entité présente au-dessus
    bool centerIn11to14 = false; // types 0x0B–0x0E
    bool centerIs16     = false; // type 0x16

    if (aboveIndex >= 0) {
        int t = state.entities[aboveIndex].actionCode;
        if (t >= 0x0B && t <= 0x0E)
            centerIn11to14 = true;
        if (t == 0x16)
            centerIs16 = true;
    }

    bool curvedOrSpecial = centerIn11to14 || centerIs16;

    // Codes spéciaux du décor
    const int TILE_A = 0xFFF5;
    const int TILE_B = 0xFFF6;
    const int TILE_C = 0xFFF7;
    const int TILE_D = 0xFFF8;
    const int TILE_E = 0xFFFB;
    const int TILE_F = 0xFFFC;
    const int TILE_G = 0xFFFA;

    // 7. Conditions de diagonales haut-gauche / haut-droite
    bool condUpLeft = false;
    bool condUpRight = false;

    // (Bloc équivalent à 3E71–3E9A et 3E9C–3EC6)
    // Si centre vide sur entityMap + bottom vide sur bottomMap,
    // et certains types particuliers
    if (centerIndex == -1 && bottomIndex == -1) {
        if (aboveIndex == TILE_A || aboveIndex == TILE_D || aboveIndex == TILE_B ||
            curvedOrSpecial)
            condUpLeft = true;
    }

    if (rightIndex == -1 && leftIndex == -1) {
        if (aboveIndex == TILE_E || aboveIndex == TILE_D || aboveIndex == TILE_F ||
            curvedOrSpecial)
            condUpRight = true;
    }

    // 8. Deux directions possibles → aléatoire
    if (condUpLeft && condUpRight) {
        uint16_t rnd = pseudoRandomUpdate();
        bool goLeft = (rnd & 1u) != 0;
        moveAndRedrawEntity(entityIndex,
                            newRow - 1,
                            newCol + (goLeft ? -1 : +1));
        return;
    }

    // Seulement haut-gauche
    if (condUpLeft) {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol - 1);
        return;
    }

    // Seulement haut-droite
    if (condUpRight) {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol + 1);
        return;
    }

    // 9. Interaction avec déflecteurs (comme à la fin du bloc)
    if (aboveIndex >= 0) {
        auto& self = state.entities[entityIndex];
        int t = state.entities[aboveIndex].actionCode;

        if (t == 0x14) {
            self.actionCode = 0x0B; // devient type 11
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }
        if (t == 0x15) {
            self.actionCode = 0x0C; // devient type 12
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }
    }
}

void handleSmartRightCrawler(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    // 1) Mouvement "smart" générique
    if (tryMoveSmartEntity(entityIndex) != 0) {
        return;
    }

    // 2) Vérifier si l’entité est autorisée à se déplacer
    if (canEntityMove(entityIndex) != 0) {
        return;
    }

    const int newRow = row;
    const int newCol = col;

    // 3) Regarder la case "devant" sur la carte principale : 0x1280 → entityMap
    //    (c’est le "centre", pas encore la case à droite ; la logique du main loop
    //     fait que newRow/newCol représentent déjà la position de test)
    int centerIndex = state.entityMap[newRow][newCol]; // di dans l’ASM

    // 0xFFFF → vide : déplacement simple vers la droite
    if (centerIndex == -1) {
        moveAndRedrawEntity(entityIndex, newRow, newCol + 1);
        return;
    }

    // 4) La case à droite n’est pas vide : tenter de remplacer la cible à droite
    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol + 1)) {
        return;
    }

    // 5) Lecture des cartes parallèles autour de (newRow,newCol)
    //
    //   var_8  ← [base+1256h] → topEntityMap
    //   dx     ← [base+12A6h] → bottomEntityMap
    //   var_A  ← [base+1258h] → "auxTopRight" (carte auxiliaire n°1)
    //   var_C  ← [base+12A8h] → "auxBottomRight" (carte auxiliaire n°2)
    //
    // Pour l’instant je les nomme via helpers que tu pourras implémenter
    // comme tu l’as fait pour getAuxBottomMapValue / getAuxTopMapValue.
    int topIndex       = state.topEntityMap[newRow][newCol];        // var_8
    int bottomIndex    = state.bottomEntityMap[newRow][newCol];     // dx
    int auxTopRight    = getAuxTopRightMapValue(newRow, newCol);    // var_A, 0x1258
    int auxBottomRight = getAuxBottomRightMapValue(newRow, newCol); // var_C, 0x12A8

    // 6) Flags sur le type de ce qu’il y a "au centre" (centerIndex)
    bool centerIs11to14 = false; // types 0x0B–0x0E
    bool centerIs16     = false; // type  0x16

    if (centerIndex >= 0) {
        int t = state.entities[centerIndex].actionCode;
        if (t >= 0x0B && t <= 0x0E) {
            centerIs11to14 = true;
        }
        if (t == 0x16) {
            centerIs16 = true;
        }
    }

    bool centerIsCurvedOrRounded = centerIs11to14 || centerIs16;

    // Codes murs spéciaux (mêmes constantes que dans le reste de ton projet)
    const int TILE_WALL9 = 0xFFF5; // '9'
    const int TILE_WALL8 = 0xFFF6; // '8'
    const int TILE_WALL7 = 0xFFF7; // '7'
    const int TILE_WALL6 = 0xFFF8; // '6'
    const int TILE_WALL4 = 0xFFFA; // '4'

    // Ensemble utilisé pour la diagonale BAS-DROITE
    bool centerInDownSet =
        (centerIndex == TILE_WALL9) ||
        (centerIndex == TILE_WALL8) ||
        (centerIndex == TILE_WALL6);

    // Ensemble utilisé pour la diagonale HAUT-DROITE
    bool centerInUpSet =
        (centerIndex == TILE_WALL7) ||
        (centerIndex == TILE_WALL8) ||
        (centerIndex == TILE_WALL4);

    // 7) Deux grosses conditions : "bas-droite intéressant" et "haut-droite intéressant"

    // Bloc 1 (4061–4088 en ASM) :
    // bottomIndex == 0xFFFF && auxBottomRight == 0xFFFF &&
    //    ( center ∈ {FFF5,FFF6,FFF8} || type 0x0B–0x0E || type 0x16 )
    bool condDownRight = false;
    if (bottomIndex == -1 && auxBottomRight == -1) {
        if (centerInDownSet || centerIsCurvedOrRounded) {
            condDownRight = true;
        }
    }

    // Bloc 2 (408C–40B4 en ASM) :
    // topIndex == 0xFFFF && auxTopRight == 0xFFFF &&
    //    ( center ∈ {FFF7,FFF6,FFFA} || type 0x0B–0x0E || type 0x16 )
    bool condUpRight = false;
    if (topIndex == -1 && auxTopRight == -1) {
        if (centerInUpSet || centerIsCurvedOrRounded) {
            condUpRight = true;
        }
    }

    // 8) Si les deux diagonales sont possibles → choix aléatoire haut/bas
    if (condUpRight && condDownRight) {
        uint16_t rnd = pseudoRandomUpdate(); // l’ASM fait un micmac mais tire en gros un bit
        bool goUp = (rnd & 1u) != 0;

        int targetRow = goUp ? (newRow - 1) : (newRow + 1);
        int targetCol = newCol + 1;
        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        return;
    }

    // Seulement diagonale haut-droite
    if (condUpRight) {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol + 1);
        return;
    }

    // Seulement diagonale bas-droite
    if (condDownRight) {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol + 1);
        return;
    }

    // 9) Sinon, interaction avec un déflecteur occupant la case centrale
    if (centerIndex >= 0) {
        auto& self = state.entities[entityIndex];
        int  centerType = state.entities[centerIndex].actionCode;

        if (centerType == 0x14) {
            // déflecteur 'a' → cette entité devient type 0x0D
            self.actionCode = 0x0D;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }

        if (centerType == 0x15) {
            // déflecteur 'c' → cette entité devient type 0x0E
            self.actionCode = 0x0E;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }
    }

    // Sinon : on sort sans mouvement supplémentaire (équiv. à handleUnknownEntityTypeLoc qui retourne)
}

void handleSmartDownCrawler(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    // 1) Mouvement “smart” générique
    if (tryMoveSmartEntity(entityIndex) != 0)
        return;

    // 2) Vérifier si l’entité a le droit de bouger
    if (canEntityMove(entityIndex) != 0)
        return;

    int newRow = row;
    int newCol = col;

    // 3) Case juste en dessous : map 0x12A6 → bottomEntityMap
    int belowIndex = state.bottomEntityMap[newRow][newCol]; // di

    // -1 => libre : on descend tout droit
    if (belowIndex == -1) {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol);
        return;
    }

    // Sinon, tenter de remplacer la cible en dessous
    if (replaceEntityIfTargetMatches(entityIndex, newRow + 1, newCol))
        return;

    // 4) Lecture des autres cartes autour de (newRow,newCol)
    // var_8 ← [1280h] → entityMap (centre)
    // dx    ← [127Ch] → rightEntityMap
    // var_A ← [12A8h] → carte auxiliaire "bas-droite"
    // var_C ← [12A4h] → carte auxiliaire "bas-gauche"
    int centerIndex    = state.entityMap[newRow][newCol];           // var_8
    int rightIndex     = state.rightEntityMap[newRow][newCol];      // dx
    int auxDownRight   = getAuxBottomRightMapValue(newRow, newCol); // var_A
    int auxDownLeft    = getAuxBottomMapValue(newRow, newCol);      // var_C

    // 5) Flags sur le type de ce qu’il y a en dessous (belowIndex)
    bool belowIn0Bto0E = false; // types 0x0B–0x0E
    bool belowIs16     = false; // type  0x16

    if (belowIndex >= 0) {
        int t = state.entities[belowIndex].actionCode;
        if (t >= 0x0B && t <= 0x0E)
            belowIn0Bto0E = true;
        if (t == 0x16)
            belowIs16 = true;
    }

    bool belowIsCurvedOrRounded = belowIn0Bto0E || belowIs16;

    // Valeurs mur spéciales (0xFFF* interprétées en signé)
    const int TILE_FFF7 = static_cast<int16_t>(0xFFF7);
    const int TILE_FFFA = static_cast<int16_t>(0xFFFA);
    const int TILE_FFF6 = static_cast<int16_t>(0xFFF6);
    const int TILE_FFFD = static_cast<int16_t>(0xFFFD);
    const int TILE_FFFC = static_cast<int16_t>(0xFFFC);

    // --- 6) Deux grandes conditions (diag bas-gauche / bas-droite) ---

    // cond1 (bloc 4061–4088) :
    // rightIndex == 0xFFFF && auxDownLeft == 0xFFFF &&
    //   ( below ∈ {FFF7, FFFA, FFF6} || 0B–0E || 16 )
    bool canDiagDownLeft = false;
    if (rightIndex == -1 && auxDownLeft == -1) {
        bool belowIsWallSet1 =
            (belowIndex == TILE_FFF7) ||
            (belowIndex == TILE_FFFA) ||
            (belowIndex == TILE_FFF6);

        if (belowIsWallSet1 || belowIsCurvedOrRounded)
            canDiagDownLeft = true;
    }

    // cond2 (bloc 408C–40B4) :
    // centerIndex == 0xFFFF && auxDownRight == 0xFFFF &&
    //   ( below ∈ {FFFD, FFFA, FFFC} || 0B–0E || 16 )
    bool canDiagDownRight = false;
    if (centerIndex == -1 && auxDownRight == -1) {
        bool belowIsWallSet2 =
            (belowIndex == TILE_FFFD) ||
            (belowIndex == TILE_FFFA) ||
            (belowIndex == TILE_FFFC);

        if (belowIsWallSet2 || belowIsCurvedOrRounded)
            canDiagDownRight = true;
    }

    // --- 7) Décision de mouvement ---

    // Les deux diagonales possibles → choisir aléatoirement
    if (canDiagDownLeft && canDiagDownRight) {
        uint16_t rnd = pseudoRandomUpdate();
        bool goRight = (rnd & 1u) != 0;

        int targetCol = goRight ? (newCol + 1) : (newCol - 1);
        int targetRow = newRow + 1;

        moveAndRedrawEntity(entityIndex, targetRow, targetCol);
        return;
    }

    // Uniquement bas-droite
    if (canDiagDownRight) {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol + 1);
        return;
    }

    // Uniquement bas-gauche
    if (canDiagDownLeft) {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol - 1);
        return;
    }

    // --- 8) Interaction avec les déflecteurs situés en dessous ---

    if (belowIndex >= 0) {
        auto& self      = state.entities[entityIndex];
        int  belowType  = state.entities[belowIndex].actionCode;

        if (belowType == 0x14) {
            // Déflecteur 'a' : ce crawler devient type 0x0C (smart-droite)
            self.actionCode = 0x0C;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }

        if (belowType == 0x15) {
            // Déflecteur 'c' : ce crawler devient type 0x0B (smart-gauche)
            self.actionCode = 0x0B;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return;
        }
    }

    // Sinon : rien de plus à faire dans ce handler
}

void handleSmartRightPusher(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    // 1) Ne s’exécute qu’une fois toutes les 5 frames
    if (frameCounter % 5 != 0) {
        return;
    }

    // 2) Mouvement “smart” générique
    if (tryMoveSmartEntity(entityIndex) != 0)
        return;

    // 3) Vérifier si l’entité a le droit de bouger
    if (canEntityMove(entityIndex) != 0)
        return;

    int newRow = row;
    int newCol = col;

    // 4) Regarder ce qu’il y a sur la case actuelle dans la map principale (0x1280)
    int hereIndex = state.entityMap[newRow][newCol];
    if (hereIndex == -1) {
        // Case libre dans cette map → on essaie juste d’avancer vers la droite
        moveAndRedrawEntity(entityIndex, newRow, newCol + 1);
        return;
    }

    // 5) Sinon, essayer de remplacer ce qu’il y a à droite
    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol + 1)) {
        // quelque chose a été remplacé / géré, on s’arrête là
        return;
    }

    // 6) Changer notre propre type en 7 (via [bp+var_10] dans l’ASM)
    state.entities[entityIndex].actionCode = 7;
    // Et redessiner à la même position (mise à jour des maps, etc.)
    moveAndRedrawEntity(entityIndex, newRow, newCol);

    // 7) Relire la map principale à notre position
    int pushedIndex = state.entityMap[newRow][newCol];
    if (pushedIndex < 0) {
        // Rien d’exploitable ici
        return;
    }

    // 8) Récupérer l’entité à pousser (di)
    auto& target = state.entities[pushedIndex];

    // Nouvelle position pour cette entité : même ligne, colonne + 1
    newRow = target.row;
    newCol = target.col + 1;

    // 9) Vérifier la case (newRow, newCol) dans la grille 0x127E (gameGrid)
    //    - 0xFFFF → vide : OK
    //    - sinon, on veut soit une entité type 0x1F à cet endroit
    int16_t gridVal = static_cast<int16_t>(gameGrid[newRow][newCol]);

    if (gridVal != -1) { // != 0xFFFF
        if (gridVal < 0) {
            // Valeur négative non 0xFFFF → on laisse tomber
            return;
        }

        int gridEntityIndex = gridVal;
        if (gridEntityIndex < 0 ||
            gridEntityIndex >= static_cast<int>(state.entities.size())) {
            return;
        }

        auto& gridEntity = state.entities[gridEntityIndex];

        // En ASM : cmp word ptr [bx+172Eh], 1Fh
        if (gridEntity.actionCode != 0x1F) {
            // Pas le type attendu → on ne pousse pas
            return;
        }
    }

    // 10) Essayer de remplacer ce qu’il y a en (newRow, newCol) par pushedIndex
    if (replaceEntityIfTargetMatches(pushedIndex, newRow, newCol)) {
        // remplacé, pas besoin de moveAndRedraw en plus (comme dans l’ASM)
        return;
    }

    // 11) Sinon, on déplace effectivement l’entité poussée
    moveAndRedrawEntity(pushedIndex, newRow, newCol);
}

void handleSmartUpPusher(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    // 1) Ne s’exécute qu’une fois toutes les 5 frames
    if (frameCounter % 5 != 0) {
        return;
    }

    // 2) Mouvement “smart” générique
    if (tryMoveSmartEntity(entityIndex) != 0)
        return;

    // 3) Vérifier si l’entité a le droit de bouger
    if (canEntityMove(entityIndex) != 0)
        return;

    int newRow = row;
    int newCol = col;

    // 4) Regarder la case au-dessus dans topEntityMap (0x1256)
    int aboveIndex = state.topEntityMap[newRow][newCol];

    // 0xFFFF → -1 → libre : on monte tout droit
    if (aboveIndex == -1) {
        moveAndRedrawEntity(entityIndex, newRow - 1, newCol);
        return;
    }

    // 5) Sinon, tenter de remplacer ce qu’il y a au-dessus
    if (replaceEntityIfTargetMatches(entityIndex, newRow - 1, newCol)) {
        return;
    }

    // 6) Rien remplacé → changer notre propre type en 0x0A
    //    (mov word ptr [bp+var_10], 0Ah dans l’ASM)
    state.entities[entityIndex].actionCode = 0x0A;

    // Redessiner à la même position
    moveAndRedrawEntity(entityIndex, newRow, newCol);

    // 7) Relire la topEntityMap à cette position : entité qu’on va pousser
    int pushedIndex = state.topEntityMap[newRow][newCol];
    if (pushedIndex < 0) {
        // Rien au-dessus, on s’arrête là
        return;
    }

    if (pushedIndex >= static_cast<int>(state.entities.size())) {
        return; // sécurité
    }

    auto& pushed = state.entities[pushedIndex];

    // Nouvelle position pour cette entité : une case plus haut
    int targetRow = pushed.row - 1;
    int targetCol = pushed.col;

    // 8) Vérifier la grille principale (0x127E / gameGrid) à cette nouvelle position
    int16_t gridVal = static_cast<int16_t>(gameGrid[targetRow][targetCol]);

    if (gridVal != -1) { // != 0xFFFF (vide)
        if (gridVal < 0) {
            // Valeur négative non 0xFFFF → on ne touche pas
            return;
        }

        int gridEntityIndex = gridVal;
        if (gridEntityIndex < 0 ||
            gridEntityIndex >= static_cast<int>(state.entities.size())) {
            return;
        }

        auto& gridEntity = state.entities[gridEntityIndex];

        // En ASM : cmp word ptr [bx+172Eh], 1Fh
        if (gridEntity.actionCode != 0x1F) {
            // La case plus haut contient quelque chose de non autorisé
            return;
        }
    }

    // 9) Essayer de remplacer ce qu’il y a à (targetRow, targetCol) par pushedIndex
    if (replaceEntityIfTargetMatches(pushedIndex, targetRow, targetCol)) {
        // remplacé, pas besoin de moveAndRedrawEntity suppl.
        return;
    }

    // 10) Sinon, déplacer effectivement cette entité vers le haut
    moveAndRedrawEntity(pushedIndex, targetRow, targetCol);
}

void handleSmartDownPusher(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    // 1) Ne s’exécute qu’une fois toutes les 5 frames
    if (frameCounter % 5 != 0) {
        return;
    }

    // 2) Mouvement “smart” générique
    if (tryMoveSmartEntity(entityIndex) != 0)
        return;

    // 3) Vérifier si l’entité a le droit de bouger
    if (canEntityMove(entityIndex) != 0)
        return;

    int newRow = row;
    int newCol = col;

    // 4) Regarder la case en dessous (bottomEntityMap = 0x12A6)
    int belowIndex = state.bottomEntityMap[newRow][newCol];

    // 0xFFFF → -1 → libre : on descend tout droit
    if (belowIndex == -1) {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol);
        return;
    }

    // 5) Sinon, tenter de remplacer la case en dessous
    if (replaceEntityIfTargetMatches(entityIndex, newRow + 1, newCol)) {
        return;
    }

    // 6) Rien remplacé → changer notre propre type en 0x09
    //    (mov word ptr [bp+var_10], 9 dans l’ASM)
    state.entities[entityIndex].actionCode = 0x09;

    // Redessiner à la même position
    moveAndRedrawEntity(entityIndex, newRow, newCol);

    // 7) Relire bottomEntityMap à cette position : entité qu’on va pousser
    int pushedIndex = state.bottomEntityMap[newRow][newCol];
    if (pushedIndex < 0 ||
        pushedIndex >= static_cast<int>(state.entities.size())) {
        return;
    }

    auto& pushed = state.entities[pushedIndex];

    // Nouvelle position pour cette entité : une case plus bas
    int targetRow = pushed.row + 1;
    int targetCol = pushed.col;

    // 8) Vérifier la grille principale (gameGrid / 0x127E) à cette nouvelle position
    int16_t gridVal = static_cast<int16_t>(gameGrid[targetRow][targetCol]);

    if (gridVal != -1) {        // != 0xFFFF (vide)
        if (gridVal < 0) {      // < 0 → obstacle non-entité, on arrête
            return;
        }

        int gridEntityIndex = gridVal;
        if (gridEntityIndex < 0 ||
            gridEntityIndex >= static_cast<int>(state.entities.size())) {
            return;
        }

        auto& gridEntity = state.entities[gridEntityIndex];

        // En ASM : cmp word ptr [bx+172Eh], 1Fh
        if (gridEntity.actionCode != 0x1F) {
            // La case de destination contient quelque chose de non autorisé
            return;
        }
    }

    // 9) Essayer de remplacer ce qu’il y a à (targetRow, targetCol)
    if (replaceEntityIfTargetMatches(pushedIndex, targetRow, targetCol)) {
        return;
    }

    // 10) Sinon, déplacer effectivement cette entité vers le bas
    moveAndRedrawEntity(pushedIndex, targetRow, targetCol);
}

void handleStickyFollower(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    const int r = row;
    const int c = col;

    // On va utiliser -1 et -2 comme dans l'ASM (0xFFFF et 0xFFFE)
    constexpr int EMPTY  = -1; // 0xFFFF
    constexpr int SENTINEL = -2; // 0xFFFE

    // --- 1) Cas "aller à droite" ---
    //
    // if entityMap[r][c] == 0xFFFF && entityMap[r][c+1] == 0xFFFE:
    //     move (r, c+1)
    if (c + 1 < GameState::GRID_COLS) {
        if (state.entityMap[r][c] == EMPTY &&
            state.entityMap[r][c + 1] == SENTINEL) {
            moveAndRedrawEntity(entityIndex, r, c + 1);
            return;
        }
    }

    // --- 2) Cas "aller à gauche" via rightEntityMap ---
    //
    // if rightEntityMap[r][c] == 0xFFFF && rightEntityMap[r][c-1] == 0xFFFE:
    //     move (r, c-1)
    if (c - 1 >= 0) {
        if (state.rightEntityMap[r][c] == EMPTY &&
            state.rightEntityMap[r][c - 1] == SENTINEL) {
            moveAndRedrawEntity(entityIndex, r, c - 1);
            return;
        }
    }

    // --- 3) Cas "aller en bas" si une entité type 6 est en bas-gauche (map 0x12CE) ---
    //
    // if bottomEntityMap[r][c] == 0xFFFF
    //   && map12CE[r][c] >= 0
    //   && entities[ map12CE[r][c] ].actionCode == 6:
    //       move (r+1, c)
    //
    // Ici, on suppose que ta map à 0x12CE est stockée dans bottomLeftEntityTable[]
    // sous forme aplatie (r * GRID_COLS + c).
    int idx12CE = -1;
    {
        int mapVal = -1;

        // Si tu stockes vraiment 40x40 là, il faut s'assurer que le vector a la bonne taille.
        int flatIndex = r * GameState::GRID_COLS + c;
        if (flatIndex >= 0 &&
            flatIndex < static_cast<int>(state.bottomLeftEntityTable.size())) {
            mapVal = state.bottomLeftEntityTable[flatIndex];
        }

        idx12CE = mapVal;
    }

    if (state.bottomEntityMap[r][c] == EMPTY &&
        idx12CE >= 0 &&
        idx12CE < static_cast<int>(state.entities.size()) &&
        state.entities[idx12CE].actionCode == 6) {

        moveAndRedrawEntity(entityIndex, r + 1, c);
        return;
    }

    // --- 4) Cas "aller en haut" si une entité type 6 est à gauche (map 0x122E) ---
    //
    // if topEntityMap[r][c] != 0xFFFF → on sort
    if (state.topEntityMap[r][c] != EMPTY) {
        return;
    }

    // if leftEntityMap[r][c] < 0 → on sort
    int leftIdx = state.leftEntityMap[r][c];
    if (leftIdx < 0 ||
        leftIdx >= static_cast<int>(state.entities.size())) {
        return;
    }

    // if entities[leftIdx].actionCode != 6 → on sort
    if (state.entities[leftIdx].actionCode != 6) {
        return;
    }

    // Sinon on monte d'une case
    moveAndRedrawEntity(entityIndex, r - 1, c);
}


void handleStickyFollower2(int entityIndex, int row, int col)
{
    auto& state = g_gameState;

    const int r = row;
    const int c = col;

    // --- 1) Essai : descendre ---
    //
    // if bottomEntityMap[r][c] == 0xFFFF
    //   && auxMap12CE[r][c] == 0xFFFE:
    //       move (r+1, c)
    if (r + 1 < GameState::GRID_ROWS) {
        if (state.bottomEntityMap[r][c] == CELL_EMPTY &&
            g_gameState.auxMap12CE[r][c] == CELL_SENTINEL)
        {
            moveAndRedrawEntity(entityIndex, r + 1, c);
            return;
        }
    }

    // --- 2) Essai : monter ---
    //
    // if topEntityMap[r][c] == 0xFFFF
    //   && leftEntityMap[r][c] == 0xFFFE:
    //       move (r-1, c)
    if (r - 1 >= 0) {
        if (state.topEntityMap[r][c] == CELL_EMPTY &&
            state.leftEntityMap[r][c] == CELL_SENTINEL)
        {
            moveAndRedrawEntity(entityIndex, r - 1, c);
            return;
        }
    }

    // --- 3) Essai : aller à droite si un type 5 est repéré sur la table 0x1282 ---
    //
    // if entityMap[r][c] == 0xFFFF
    //   && auxMap1282[r][c] >= 0
    //   && entities[ auxMap1282[r][c] ].actionCode == 5:
    //       move (r, c+1)
    if (c + 1 < GameState::GRID_COLS) {
        if (state.entityMap[r][c] == CELL_EMPTY) {
            int idx = g_gameState.auxMap1282[r][c];
            if (idx >= 0 && idx < static_cast<int>(state.entities.size())) {
                if (state.entities[idx].actionCode == 5) {
                    moveAndRedrawEntity(entityIndex, r, c + 1);
                    return;
                }
            }
        }
    }

    // --- 4) Essai : aller à gauche si un type 5 est repéré sur la table 0x127A ---
    //
    // if rightEntityMap[r][c] == 0xFFFF
    //   && auxMap127A[r][c] >= 0
    //   && entities[ auxMap127A[r][c] ].actionCode == 5:
    //       move (r, c-1)
    if (c - 1 >= 0) {
        if (state.rightEntityMap[r][c] == CELL_EMPTY) {
            int idx = g_gameState.auxMap127A[r][c];
            if (idx >= 0 && idx < static_cast<int>(state.entities.size())) {
                if (state.entities[idx].actionCode == 5) {
                    moveAndRedrawEntity(entityIndex, r, c - 1);
                    return;
                }
            }
        }
    }

    // Sinon : rien, on tombe dans le "default" du switch (handleUnknownEntityTypeLoc côté ASM)
}

void adjustSmartEntityTarget(int entityIndex,
                             int srcRow,
                             int srcCol,
                             int* targetRow,
                             int* targetCol)
{
    // Position actuelle de l’entité (0x1730 / 0x1732)
    int curRow = g_gameState.entities[entityIndex].row;
    int curCol = g_gameState.entities[entityIndex].col;

    // Valeurs de base : la cible devient au départ la position actuelle
    if (targetRow) *targetRow = curRow;
    if (targetCol) *targetCol = curCol;

    // Direction vers la source (saturée à -1 / 0 / +1)
    int dy = 0;
    if (srcRow > curRow)      dy =  1;
    else if (srcRow < curRow) dy = -1;

    int dx = 0;
    if (srcCol > curCol)      dx =  1;
    else if (srcCol < curCol) dx = -1;

    // Si pas de diagonale (dx == 0 ou dy == 0), on ne fait que le test final,
    // comme dans l’ASM qui saute directement à loc_334A.
    if (dy != 0 && dx != 0) {
        // On va regarder les cases “voisines” sur la map 0x127E
        // (auxRightEntityMap ici).

        auto isEmpty = [](int row, int col) -> bool {
            // À adapter selon ta représentation de la grille
            if (row < 0 || row >= g_gameState.GRID_ROWS ||
                col < 0 || col >= g_gameState.GRID_COLS)
                return false;
            return g_gameState.auxTopRightEntityMap[row][col] == CELL_EMPTY;
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
        if (row < 0 || row >= g_gameState.GRID_ROWS ||
            col < 0 || col >= g_gameState.GRID_COLS)
            return false;
        return g_gameState.auxTopRightEntityMap[row][col] == CELL_EMPTY;
    };

    if (isEmpty(candidateRow, candidateCol)) {
        if (targetRow) *targetRow = candidateRow;
        if (targetCol) *targetCol = candidateCol;
    }
}

void handleSmartRandomEntityTypes15to19(
    int entityIndex,
    int& newRow,
    int& newCol,
    int srcRow,
    int srcCol
) {
    // 1) D’abord, vérifier si la condition de “mort”/destruction s’applique
    // checkAndHandleDeathCondition(si) → renvoie non-zéro si l’ennemi doit mourir
    if (checkAndHandleDeathCondition(entityIndex) != 0) {
        return; // on laisse le caller tomber dans le default
    }

    // 2) N’agir que toutes les 3 frames : frameCounter % 3 == 0
    if ((frameCounter % 3) != 0) {
        return;
    }

    int dy = 0;      // correspond à DI dans l’ASM (offset vertical)
    int dx = 0;      // correspond à [bp+var_6] (offset horizontal)

    // 3) D’abord tenter un déplacement “intelligent” classique
    if (tryMoveSmartEntity(entityIndex) != 0) {
        return;
    }

    // 4) Si la case ciblée est bloquée, on ne fait ensuite que la partie
    //    “remplacement/move” plus bas, sans la dérive aléatoire.
    if (canEntityMove(entityIndex) != 0) {
        // sa cible actuelle (newRow/newCol) reste telle quelle
        // et on saute à la phase “replace/move”
        goto APPLY_AND_MOVE;
    }

    // À partir d’ici, on a le droit de bouger, et on introduit
    // un peu d’aléatoire.

    // 5) Premier hasard : parfois on “recalibre” la cible vers le joueur
    {
        // Dans l’ASM : pseudoRandomUpdate → divide64_unsigned → comparaison avec 1
        // Ici : simplification : tirage binaire sur 0/1.
        uint16_t r = pseudoRandomUpdate();
        if ((r & 1u) == 1u) {
            // Coin flip == 1 : recalcule la cible “intelligente”
            adjustSmartEntityTarget(
                entityIndex,
                srcRow,
                srcCol,
                &newRow,
                &newCol
            );
            // On ne fait pas de dérive aléatoire dans ce cas-là,
            // on passe directement à la phase “replace/move”.
            goto APPLY_AND_MOVE;
        }
    }

    // 6) Sinon, deuxième hasard : on va choisir un petit offset
    //    aléatoire de -1, 0 ou +1 soit en X soit en Y.

    {
        // Dans l’ASM, il y a deux branches quasi identiques :
        // - soit on remplit DI (dy), soit var_6 (dx) avec une valeur dans [-1, 1].
        // Ici, on simplifie : on choisit aléatoirement de modifier X ou Y.
        bool changeRow = (pseudoRandomUpdate() & 1u) != 0;

        auto randomOffset = []() -> int {
            // 0,1,2 → -1,0,+1
            uint16_t r = pseudoRandomUpdate();
            int v = static_cast<int>(r % 3);
            return v - 1;
        };

        if (changeRow) {
            dy = randomOffset();  // -1, 0, +1
            dx = 0;
        } else {
            dy = 0;
            dx = randomOffset();
        }
    }

    // 7) On teste la case (newRow + dy, newCol + dx) via la map auxiliaire 0x127E :
    //    - si CELL_EMPTY → OK
    //    - si index >= 0 et entity.type == 0x1F → OK
    //    - sinon on garde la cible d’origine (newRow/newCol)

    {
        int candidateRow = newRow + dy;
        int candidateCol = newCol + dx;

        // Optionnel : bornes de la grille si tu les as
        if (candidateRow >= 0 && candidateRow < GameState::GRID_ROWS &&
            candidateCol >= 0 && candidateCol < GameState::GRID_COLS) {

            // 0x127E : “auxRightEntityMap” (ou nom équivalent que tu as donné)
            int auxVal = g_gameState.auxTopRightEntityMap[candidateRow][candidateCol];

            bool ok = false;

            if (auxVal == CELL_EMPTY) {
                ok = true;
            } else if (auxVal >= 0) {
                const auto& e = g_gameState.entities[auxVal];
                if (e.actionCode == 0x1F) {
                    // Type 0x1F autorisé à être “écrasé”
                    ok = true;
                }
            }

            if (ok) {
                newRow = candidateRow;
                newCol = candidateCol;
            }
        }
    }

APPLY_AND_MOVE:
    // 8) Phase générique : remplacer l’entité de destination si besoin,
    //    puis déplacer/redessiner.

    if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol) != 0) {
        // Target “spéciale” → on laisse le caller aller au handler par défaut
        return;
    }

    moveAndRedrawEntity(entityIndex, newRow, newCol);
}

static inline i16 tileRandomCoord16()
{
  const uint16_t r = pseudoRandomUpdate();
  return static_cast<i16>((r >> 11) & 0x000F);
}

static void renderSparkleTileAndPresent(i16 pixelCount)
{
  auto dcSrc = LegacyGfxBackend::createCompatibleDC(g_hdc2);
  auto bmpTmp = LegacyGfxBackend::createCompatibleBitmap(g_hdc2, 16, 16);
  auto dcTmp = LegacyGfxBackend::createCompatibleDC(g_hdc2);

  LegacyGfxBackend::selectObject(dcTmp, bmpTmp);
  LegacyGfxBackend::selectObject(dcSrc, bitmap_kye);

  LegacyGfxBackend::bitBlt(
    dcTmp, 0, 0, 16, 16,
    dcSrc, 0, 0, 0x00CC0020u
  );

  for (i16 i = 0; i < pixelCount; ++i) {
    const i16 x = tileRandomCoord16();
    const i16 y = tileRandomCoord16();
    LegacyGfxBackend::setPixel(dcTmp, x, y, 0x00FFFFFFu);
  }

  const i16 dstY = static_cast<i16>(srcRow * cellHeight);
  const i16 dstX = static_cast<i16>(srcCol * cellWidth);

  LegacyGfxBackend::bitBlt(
    static_cast<LegacyGfxBackend::DcHandle>(g_hdc2),
    dstX, dstY, 16, 16,
    dcTmp, 0, 0, 0x00CC0020u
  );

  LegacyGfxBackend::deleteDC(dcSrc);
  LegacyGfxBackend::deleteDC(dcTmp);
  LegacyGfxBackend::deleteObject(bmpTmp);
}

void runTileSparkleEffectSdl(i16 effectId)
{
  if (effectId == 1) {
    for (i16 sparkleCount = 0x0100; sparkleCount >= 0; sparkleCount = static_cast<i16>(sparkleCount - 0x0010)) {
      renderSparkleTileAndPresent(sparkleCount);
    }
    return;
  }

  if (effectId == 2) {
    for (i16 sparkleCount = 0; sparkleCount < 0x0100; sparkleCount = static_cast<i16>(sparkleCount + 0x0010)) {
      renderSparkleTileAndPresent(sparkleCount);
    }
    return;
  }

  auto dc = LegacyGfxBackend::createCompatibleDC(g_hdc2);

  const i16 dstY = static_cast<i16>(srcRow * cellHeight);
  const i16 dstX = static_cast<i16>(srcCol * cellWidth);

  LegacyGfxBackend::selectObject(dc, bitmap_kye);

  LegacyGfxBackend::bitBlt(
    static_cast<LegacyGfxBackend::DcHandle>(g_hdc2),
    dstX, dstY, 16, 16,
    dc, 0, 0, 0x00CC0020u
  );

  LegacyGfxBackend::deleteDC(dc);
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

            // handlePendingBlock(rowIndex, colIndex)
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

            handleClickOnGridCell(rowIndex, colIndex);
            initializeWindowHandleIfNeeded();
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
    for (int row = 0; row < GameState::GRID_ROWS ; ++row) {
        for (int col = 0; col < GameState::GRID_COLS ; ++col) {
            if (gameGrid[row][col] == 0xFFF3u) {
                ++count;
            }
        }
    }
    return count;
}

int renderLivesAndLevelInfo()
{
    GameInteractionMode mode = g_interactionMode;
    if (mode != GameInteractionMode::PendingBlock &&
        mode != GameInteractionMode::NormalPlay)
    {
        return 0;
    }

    char textBuffer[0x6C] = {0};

    if (mode == GameInteractionMode::PendingBlock) {
        LegacyGfxBackend::selectObject(g_hdc2, pen_gray);

        drawRectangle(
            baseX,
            baseY,
            static_cast<i16>(baseX + 0x46),
            static_cast<i16>(baseY + 0x11));

        LegacyGfxBackend::DcHandle tempMemoryDC =
            LegacyGfxBackend::createCompatibleDC(g_hdc2);
        LegacyGfxBackend::selectObject(tempMemoryDC, bitmap_kye);

        i16 xOffsetLives = 0;
        for (int i = 0; i < remainingLives; ++i) {
            i16 destX = static_cast<i16>(baseX + xOffsetLives + 1);
            i16 destY = static_cast<i16>(baseY + 1);

            LegacyGfxBackend::bitBlt(
                g_hdc2,
                destX,
                destY,
                0x10,
                0x10,
                tempMemoryDC,
                0,
                0,
                SRCCOPY);

            xOffsetLives = static_cast<i16>(xOffsetLives + 0x14);
        }

        LegacyGfxBackend::deleteDC(tempMemoryDC);
        LegacyGfxBackend::selectObject(g_hdc2, pen_black);

        prepareAndCallProcessMainLoop(textBuffer, 0x4B8, levelIndex);
        int len = static_cast<int>(std::strlen(textBuffer));
        drawTextAt(static_cast<i16>(baseX + 0x50), baseY, textBuffer, len);

        int diamondCount = 0;
        for (int row = 0; row < GameState::GRID_ROWS ; ++row) {
            for (int col = 0; col < GameState::GRID_COLS ; ++col) {
                if (gameGrid[row][col] == 0xFFF3u) {
                    ++diamondCount;
                }
            }
        }

        prepareAndCallProcessMainLoop(textBuffer, 0x4C6, diamondCount);
        len = static_cast<int>(std::strlen(textBuffer));
        drawTextAt(static_cast<i16>(baseX + 0xA0), baseY, textBuffer, len);
    } else {
        int diamondCount = 0;
        for (int row = 0; row < GameState::GRID_ROWS ; ++row) {
            for (int col = 0; col < GameState::GRID_COLS ; ++col) {
                if (gameGrid[row][col] == 0xFFF3u) {
                    ++diamondCount;
                }
            }
        }

        prepareAndCallProcessMainLoop(textBuffer, 0x4DC, diamondCount);
        int len = static_cast<int>(std::strlen(textBuffer));
        drawTextAt(static_cast<i16>(baseX + 5), baseY, textBuffer, len);

        prepareAndCallProcessMainLoop(textBuffer, 0x4ED, 0x2D5E);
        len = static_cast<int>(std::strlen(textBuffer));
        if (len > 0x19) {
            len = 0x19;
        }

        drawTextAt(static_cast<i16>(baseX + 0x6E), baseY, textBuffer, len);
    }

    return 0;
}

void setStatusText(const std::string& text) {
    clearStatusLine(text.c_str());
}

void handleClickOnGridCell(int16_t row, int16_t col)
{
    if (row < 0 || row >= GameState::GRID_ROWS || col < 0 || col >= GameState::GRID_COLS) {
        return;
    }

    const uint16_t cellValue = gameGrid[row][col];

    if (cellValue == 0xFFFFu) {  // CELL_EMPTY
        if (!hasLevelList) {
            return;
        }

        if (!isCurrentEntryActionActive()) {
            return;
        }
        const LevelEntry& entry = g_levelEntries[currentEntryIndex];

        executeCurrentEntryAction(entry.mode,
                                  entry.tileId,
                                  static_cast<i16>(row),
                                  static_cast<i16>(col));
        return;
    }
    handleStandardCellClick(row, col);
}


void handleStandardCellClick(i16 row, i16 col)
{
    if (row < 0 || row >= GameState::GRID_ROWS || col < 0 || col >= GameState::GRID_COLS) {
        return;
    }
    const uint16_t cellValue      = gameGrid[row][col];
    const i16 signedValue    = static_cast<i16>(cellValue); // pour le test > 0
    const uint16_t originalValue  = cellValue;                   // on garde la valeur avant modification

    if (signedValue > 0) {
        markEntryInactive(signedValue);
        gameGrid[row][col] = 0xFFFFu;   // cellule vidée
    } else {
        gameGrid[row][col] = 0xFFFFu;
    }
    finalizeLevelVisuals();
    if (originalValue == 0xFFFEu) {
        handleSpecialSentinelClick();
    }
}

void handleSpecialSentinelClick()
{
    const bool hasSpecial = hasSpecialCell();
    specialCellStateFlag = hasSpecial ? 0 : 1;

    if (!g_hdc2) {
        return;
    }

    RECT clientRect{};
    GetClientRect(static_cast<HWND>(g_hdc2), &clientRect);

    InvalidateRect(static_cast<HWND>(g_hdc2), &clientRect, FALSE);
    UpdateWindow(static_cast<HWND>(g_hdc2));
}

inline int cellIndex(i16 row, i16 col)
{
    return row * GameState::GRID_COLS + col;
}

void executeCurrentEntryAction(i16 actionType,
                               i16 tileId,
                               i16 row,
                               i16 col)
{
    const bool inBounds =
        (col > 0 && col < (GameState::GRID_COLS - 1)) &&
        (row > 0 && row < (GameState::GRID_ROWS - 1));

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

void updateGridCell(i16 row, i16 col)
{
    LegacyGfxBackend::DcHandle tempDc =
        LegacyGfxBackend::createCompatibleDC(g_hdc2);
    if (!tempDc) {
        return;
    }

    const i16 cellValue = static_cast<i16>(gameGrid[row][col]);

    if (cellValue >= 0) {
        LegacyGfxBackend::selectObject(tempDc, bitmap_block);

        renderEntityToWindow(static_cast<int16_t>(cellValue));
    }
    else if (cellValue == CELL_FLAG_SENTINEL) {
        LegacyGfxBackend::selectObject(tempDc, bitmap_kye);

        const i16 destY = static_cast<i16>(row * cellHeight);
        const i16 destX = static_cast<i16>(col * cellWidth);

        LegacyGfxBackend::blitToWindow(
            g_hdc2,
            destX,
            destY,
            static_cast<i16>(cellWidth),
            static_cast<i16>(cellHeight),
            tempDc,
            0,
            0,
            static_cast<uint32_t>(SRCCOPY) 
        );
    }
    else if (cellValue == CELL_FLAG_EMPTY) {
        drawRectangleFromGrid(row, col);
    }
    else {
        LegacyGfxBackend::selectObject(tempDc, bitmap_wall);
        renderWallTile(row, col, tempDc);
    }
    LegacyGfxBackend::deleteDC(tempDc);
}

void renderWallTile(i16 row, i16 col, LegacyGfxBackend::DcHandle srcDc)
{
    const i16 rawValue = static_cast<i16>(gameGrid[row][col]);

    if (rawValue > static_cast<i16>(0xFFFD)) {
        return;
    }
    if (rawValue < static_cast<i16>(0xFFEF)) {
        return;
    }

    const i16 destY = static_cast<i16>(row * cellHeight);
    const i16 destX = static_cast<i16>(col * cellWidth);

    i16 srcX = 0;
    constexpr i16 srcY = 0;
    constexpr i16 tileSize = 0x10;
    constexpr uint32_t ROP_SRCCOPY = 0x00CC0020; 

    if (rawValue <= static_cast<i16>(0xFFFD) &&
        rawValue >= static_cast<i16>(0xFFF5))
    {
        const i16 absVal =
            (rawValue < 0) ? static_cast<i16>(-rawValue) : rawValue;
        const i16 absFFFD =
            (static_cast<i16>(0xFFFD) < 0)
                ? static_cast<i16>(-static_cast<i16>(0xFFFD))
                : static_cast<i16>(0xFFFD);

        const i16 delta = static_cast<i16>(absVal - absFFFD); // 0..8
        srcX = static_cast<i16>(0x30 + (delta << 4));         // 0x30 + 16*delta
    }
    else if (rawValue == static_cast<i16>(0xFFF4)) {
        // 0xFFF4 : brique jaune / spéciale -> source à X=0
        srcX = 0x00;
    }
    else if (rawValue == static_cast<i16>(0xFFF3)) {
        // 0xFFF3 : diamant -> source à X=0xC0
        srcX = 0x00C0;
    }
    else if (rawValue == static_cast<i16>(0xFFF2) ||
             rawValue == static_cast<i16>(0xFFF1))
    {
        // 0xFFF2 / 0xFFF1 : flèches/bloqueurs horizontaux -> X=0xE0
        srcX = 0x00E0;
    }
    else if (rawValue == static_cast<i16>(0xFFF0) ||
             rawValue == static_cast<i16>(0xFFEF))
    {
        // 0xFFF0 / 0xFFEF : flèches/bloqueurs verticaux -> X=0xD0
        srcX = 0x00D0;
    }
    else {
        // Autre valeur du range négatif 0xFFEF..0xFFFD : en pratique,
        // l’ASM tombe ici et sort -> on ne dessine rien.
        return;
    }

    // 4) On blitte la tuile du DC source (bitmap_wall déjà sélectionné)
    LegacyGfxBackend::blitToWindow(
        g_hdc2,
        destX,
        destY,
        tileSize,
        tileSize,
        srcDc,
        srcX,
        srcY,
        ROP_SRCCOPY
    );
}

bool isCurrentEntryActionActive()
{
    if (!hasLevelList) {
        return false;
    }

    if (currentEntryIndex < 0 || currentEntryIndex >= kMaxLevelEntries) {
        return false;
    }

    const LevelEntry& entry = g_levelEntries[currentEntryIndex];
    return entry.isActive != 0;
}

void cancelPendingInteraction()
{
    if (g_isMouseCaptured != 0) {
        ::ReleaseCapture();
        g_isMouseCaptured = 0;
    }

    if (g_interactionMode == GameInteractionMode::PendingBlock) {
        g_hasPendingModal = 0;
        matchedEntryCount  = 0;
        maybeDrawPendingRectangle();
    }
}


int showToolboxWindowAndRefreshHUD()
{
    static bool toolboxCreated = false;

    if (!toolboxCreated) {
        WNDCLASSA wc{};
        wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wc.lpfnWndProc   = ToolboxWndProc;
        wc.cbClsExtra    = 0;
        wc.cbWndExtra    = 0;
        wc.hInstance     = g_hInstance;
        wc.hIcon         = nullptr;
        wc.hCursor       = nullptr;
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH));
        wc.lpszMenuName  = nullptr;
        wc.lpszClassName = "Kye-Tools";

        if (!RegisterClassA(&wc)) {
            return 0;
        }

        g_hdc2 = CreateWindowA(
            "Kye-Tools",
            "Kye-Tools",
            0x80C0,
            0,
            0x32,
            0x5A,
            0x52,
            g_hdc2,
            nullptr,
            g_hInstance,
            nullptr
        );

        if (!g_hdc2) {
            return 0;
        }

        toolboxCreated = true;
    }

    handleSpecialSentinelClick();

    ShowWindow(g_hdc2, SW_SHOW);

    if (g_currentNameIndex >= 0 &&
        g_currentNameIndex < static_cast<int>(g_nameTable.size()))
    {
        g_currentName = g_nameTable[static_cast<std::size_t>(g_currentNameIndex)];
    }

    initializeWindowHandleIfNeeded();
    renderLivesAndLevelInfo();
    releaseDialogResources();

    return 1;
}

enum class InteractionMode : uint8_t { Run = 0, Edit = 1 };

static bool handleMainMenuCommand(uint32_t wParamPacked /*, HWND hwnd */) {
    const uint16_t commandId = static_cast<uint16_t>(wParamPacked >> 16);

    switch (commandId) {
        case 0x0066: { // 'f' Quit
            PostQuitMessage(0);
            return true;
        }

        case 0x0065: { // 'e' Restart from level 1
            levelIndex = 1;
            loadLevelByIndex(1);
            matchedEntryCount = 0;
            clearStatusLine(g_statusLineTextBuffer);
            invalidateAndRedrawMainWindow();
            return true;
        }

        case 0x00C9: { // Restart (or abort edit)
            if (g_interactionMode == InteractionMode::Run) {
                loadLevelByIndex(levelIndex);
                clearStatusLine(g_statusLineTextBuffer);
                matchedEntryCount = 0;
                invalidateAndRedrawMainWindow();
                return true;
            }

            // Leaving edit mode (abort edit)
            g_interactionMode = InteractionMode::Run;
            hideSecondaryWindow();
            restoreRunModeMenus();
            enableMenuItemRestartFromLevel1(false); // id 0x65
            loadLevelByIndex(levelIndex);
            clearStatusLine(g_statusLineTextBuffer);
            matchedEntryCount = 0;
            g_timerActive = true;
            invalidateAndRedrawMainWindow();
            initializeWindowSize();
            return true;
        }

        case 0x00CA: { // Goto level... (run) / Edit Hint (edit)
            if (g_interactionMode == InteractionMode::Run) {
                g_timerActive = false;

                // showNameInputDialog writes into g_nameInputBuffer (0x2B90) seeded from default (0x0087)
                seedNameInputDefault();
                showNameInputDialog();
                strupr16(g_nameInputBuffer);

                const int match = findMatchingLineInFile(g_nameInputBuffer);
                if (match > 0) {
                    levelIndex = match;
                    loadLevelByIndex(levelIndex);
                    matchedEntryCount = 0;
                    clearStatusLine(g_statusLineTextBuffer);
                    invalidateAndRedrawMainWindow();
                } else if (!isEmptyString(g_nameInputBuffer)) {
                    showMessage(/*caption*/0x88, /*text*/0x91);
                }

                g_timerActive = true;
                return true;
            }

            // Edit hint
            char tempText[0x50];
            copyString(tempText, g_statusLineTextBuffer);
            if (handleInputDialog(tempText, /*label*/0x9F) != 0) return true; // canceled
            copyString(g_statusLineTextBuffer, tempText);
            clearStatusLine(g_statusLineTextBuffer);
            return true;
        }

        case 0x00CB: { // File... (run) / Edit Name (edit)
            if (g_interactionMode == InteractionMode::Run) {
                showOpenFileDialogAndBuildFullPath(g_selectedLevelPath /*0x1A0*/);
                loadLevelByIndex(levelIndex);
                clearStatusLine(g_statusLineTextBuffer);
                updateNextLevelMenuItem();
                matchedEntryCount = 0;
                invalidateAndRedrawMainWindow();
                initializeWindowSize();
                return true;
            }

            // Edit name
            char tempText[0x50];
            copyString(tempText, g_levelName /*0x2A9A*/);
            if (handleInputDialog(tempText, /*label*/0xB3) != 0) return true; // canceled
            if (strlen16(tempText) >= 0x14) return true; // reject if too long (>=20)
            strupr16(tempText);
            copyString(g_levelName, tempText);
            return true;
        }

        case 0x00CC: { // Toggle Edit/Run
            if (g_interactionMode == InteractionMode::Run) {
                levelIndex = 1;
                loadLevelByIndex(1);

                if (levelCount > 1) {
                    showMessage(/*caption*/0xC8, /*text*/0xE7);
                    return true;
                }

                g_interactionMode = InteractionMode::Edit;
                g_timerActive = false;
                showToolboxWindowAndRefreshHUD();
                setEditModeMenus();
                enableMenuItemRestartFromLevel1(true); // id 0x65
                invalidateAndRedrawMainWindow();
                initializeWindowSize();
                return true;
            }

            // Edit -> Run (commit)
            g_interactionMode = InteractionMode::Run;
            hideSecondaryWindow();
            restoreRunModeMenus();
            enableMenuItemRestartFromLevel1(false); // id 0x65
            generateFileFromMappedData();
            loadLevelByIndex(levelIndex);
            clearStatusLine(g_statusLineTextBuffer);
            matchedEntryCount = 0;
            g_timerActive = true;
            invalidateAndRedrawMainWindow();
            initializeWindowSize();
            return true;
        }

        case 0x012D: { // option toggle (checkmark)
            g_optionToggleFlag = !g_optionToggleFlag;
            checkMenuItem(0x012D, g_optionToggleFlag);
            return true;
        }

        case 0x0385: { // Help
            WinHelp(g_hwnd, "kyehelp.hlp", 3, 0);
            return true;
        }
        case 0x0386: { // Help (other topic)
            WinHelp(g_hwnd, g_helpTopicString /*unk_847D*/, 4, 0);
            return true;
        }
        case 0x038D: { // What?
            showWhatDialog();
            return true;
        }

        default:
            return false;
    }
}


int hideSecondaryWindow()
{
    if (g_hdc2 != nullptr) {
        ShowWindow(g_hdc2, SW_HIDE);
    }
    return 1;
}

inline void toUpperInPlace(std::string& s)
{
    for (char& c : s) {
        unsigned char ch = static_cast<unsigned char>(c);
        c = static_cast<char>(std::toupper(ch));
    }
}

void updateNextLevelMenuItem()
{
    HMENU menu = GetMenu(g_hdc);
    if (!menu) {
        return;
    }

    if (levelCount == 1) {
        EnableMenuItem(menu, 0xCC, MF_BYCOMMAND | MF_GRAYED);
    } else {
        EnableMenuItem(menu, 0xCC, MF_BYCOMMAND | MF_ENABLED);
    }
}

int showMessageById(uint16_t messageId, uint16_t captionId)
{
    const char* message = getUiStringById(messageId);
    const char* caption = getUiStringById(captionId);

    if (!message) {
        message = "";
    }
    if (!caption) {
        caption = "";
    }

    return MessageBoxA(g_hdc, message, caption, MB_OK | MB_ICONINFORMATION);
}

void appendSuffixToPath(char* basePath, std::size_t baseCapacity, const char* suffix)
{
    if (!basePath || baseCapacity == 0) {
        return;
    }

    std::size_t len = ::strnlen(basePath, baseCapacity);

    if (len > 0) {
        const char last = basePath[len - 1];
        if (last != '\\' && last != ':') {
            if (len + 1 < baseCapacity) {
                basePath[len] = '\\';
                ++len;
                basePath[len] = '\0';
            }
        }
    }

    if (!suffix) {
        return;
    }

    if (len >= baseCapacity - 1) {
        return;
    }

    const std::size_t remaining = baseCapacity - len;
    std::strncpy(basePath + len, suffix, remaining - 1);
    basePath[baseCapacity - 1] = '\0';
}

static OpenFileDialogResult runOpenFileDialogModal(/* ui deps */) {
    OpenFileDialogResult r;
    // ...
    // r.accepted = true/false
    // r.selectedDir = ...
    // r.filename = ...
    return r;
}

static bool showOpenFileDialogAndBuildFullPath(std::string& outFullPath) {
    const OpenFileDialogResult r = runOpenFileDialogModal();
    if (!r.accepted) return false;

    outFullPath = joinPath(r.selectedDir, r.filename);
    return true;
}


void showOpenFileDialogInternal()
{
    g_openFileDialogAccepted = false;
    g_openFileDialogPath[0] = '\0';

    OPENFILENAMEA ofn{};
    char fileBuffer[260] = {0};

    ofn.lStructSize     = sizeof(ofn);
    ofn.hwndOwner       = g_hdc;
    ofn.hInstance       = g_hInstance;
    ofn.lpstrFilter     = "Tous les fichiers\0*.*\0\0";
    ofn.lpstrFile       = fileBuffer;
    ofn.nMaxFile        = sizeof(fileBuffer);
    ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle      = "Ouvrir un fichier Kye";

    if (GetOpenFileNameA(&ofn)) {
        std::strncpy(g_openFileDialogPath, fileBuffer, sizeof(g_openFileDialogPath) - 1);
        g_openFileDialogPath[sizeof(g_openFileDialogPath) - 1] = '\0';
        g_openFileDialogAccepted = true;
    }
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
    int localX = x;
    int localY = y;

    // En dehors de la zone clickable → juste mettre le curseur et sortir
    if (!isPointInRect(localX, localY)) {
        SetCursor(g_mainCursor);
        return;
    }

    // Si on n’est pas en mode « jeu normal », idem : juste curseur
    if (g_interactionMode != GameInteractionMode::NormalPlay) {
        SetCursor(g_mainCursor);
        return;
    }

    // Dans la zone et en mode NormalPlay → curseur spécial + calcul de case
    SetCursor(g_mainCursor);

    int rowIndex = localX / cellHeight;  // SI
    int colIndex = localY / cellWidth;   // DI

    // Si aucun matchedEntryCount en cours, on ne fait rien de plus
    if (matchedEntryCount == 0) {
        return;
    }

    // Sinon on finalise le « pending block »
    handlePendingBlock(rowIndex, colIndex);
    previousRow = rowIndex;
    previousCol = colIndex;
    g_hasPendingModal = 1;
}


void initMainCursor()
{
    if (!g_mainCursor) {
        // À adapter si tu veux un autre type de curseur
        g_mainCursor = g_cursorArrow(nullptr, IDC_ARROW);
    }
}

void updateAnimatedBlocksSprites()
{
    if ((frameCounter % 5) != 0) {
        return;
    }

    LegacyGfxBackend::DcHandle tempDC =
        LegacyGfxBackend::createCompatibleDC(g_hdc);
    if (!tempDC) {
        return;
    }

    LegacyGfxBackend::selectObject(tempDC, bitmap_block);

    for (uint16_t index = 0; index < g_activeSpawnerCount; ++index) {
        uint16_t type = g_gameState.entityTypes[index];
        if (type != 0x1F && type != 0x20) {
            continue;
        }

        uint16_t row = g_gameState.entityRows[index];
        uint16_t col = g_gameState.entityCols[index];

        int destY = static_cast<int>(row) * cellHeight;
        int destX = static_cast<int>(col) * cellWidth;

        uint16_t phase = g_gameState.entityAnimationPhase[index];
        int srcX = (static_cast<int>(phase) << 4) + 0x40;
        int srcY = (type == 0x1F) ? 0x80 : 0x90;

        LegacyGfxBackend::bitBlt(
            g_hdc,
            static_cast<i16>(destX),
            static_cast<i16>(destY),
            0x10,
            0x10,
            tempDC,
            static_cast<i16>(srcX),
            static_cast<i16>(srcY),
            SRCCOPY
        );

        if (type == 0x20 && phase == 3) {
            g_gameState.entityTypes[index] = 0x1F;
        }

        phase = static_cast<uint16_t>((phase + 1) % 4);
        g_gameState.entityAnimationPhase[index] = phase;
    }

    LegacyGfxBackend::deleteDC(tempDC);
}

LRESULT WndProc(void* hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    const int16_t x = LOWORD_(lParam);
    const int16_t y = HIWORD_(lParam);

    switch (msg)
    {
        // 0x0111
        case 0x0111: // WM_COMMAND
            // asm: return DX:AX = handleMainMenuCommand(wParam, lParam)
            return handleMainMenuCommand(wParam, lParam);

        // < 0x0111
        case 0x000F: // WM_PAINT
            // asm: push dx ; call sub_35C
            sub_35C(x);
            return 0;

        case 0x0001: // (probablement WM_CREATE)
            // asm: jmp loc_D60 => return 0
            return 0;

        case 0x0002: // WM_DESTROY
            sub_10C0();
            PostQuitMessage(0);
            return 0;

        case 0x0005: // WM_SIZE
            // asm: DefWindowProc, return direct
            return DefWindowProcA(hWnd, msg, wParam, lParam);

        // 0x0100 / 0x0101
        case 0x0100: // WM_KEYDOWN
            word_836E = 1;
            sub_573(wParam, lParam);
            return 0;

        case 0x0101: // WM_KEYUP
            word_836E = 0;
            return 0;

        // > 0x0111
        case 0x0113: // WM_TIMER
            if (g_timerActive != 0)
            {
                advanceToNextLevelOrBlock();
                updateLevelVisualsAndAnimations();
                g_timerActive = 1; // exactement comme l'asm (même si “étrange”)
            }
            return 0;

        // Souris
        case 0x0200: // WM_MOUSEMOVE
            handlePendingInteractionFinalize(y, x, hWnd);
            return 0;

        case 0x0201: // WM_LBUTTONDOWN
            handleGameClick(wParam, y, x, hWnd);
            return 0;

        case 0x0202: // WM_LBUTTONUP
            cancelPendingInteraction(y, x, hWnd);
            return 0;

        case 0x0203: // WM_LBUTTONDBLCLK
            handlePointClick(wParam, y, x, hWnd);
            return 0;

        case 0x0204: // WM_RBUTTONDOWN
            handlePendingInteractionClick(wParam, y, x, hWnd);
            return 0;

        default:
            // asm: loc_D4C => DefWindowProc(...)
            return DefWindowProcA(hWnd, msg, wParam, lParam);
    }
}

void animateDiamondsEvery10Frames()
{
    if ((frameCounter % 10) != 0)
        return;

    HDC memDC = CreateCompatibleDC(reinterpret_cast<HDC>(g_hdc));
    SelectObject(memDC, bitmap_wall);

    constexpr uint16_t TILE_DIAMOND = 0xFFF3;

    for (int row = 0; row < GameState::GRID_COLS ; ++row)
    {
        for (int col = 0; col < GameState::GRID_ROWS ; ++col)
        {
            if (gameGrid[col][row] != TILE_DIAMOND)
                continue;

            const int dstY = row * cellHeight;
            const int dstX = col * cellWidth;

            int srcY = 0;

            const int16_t r16 = pseudoRandomUpdate();
            const uint32_t r32 = static_cast<uint16_t>(r16);
            const uint64_t prod = multiply32x32to64(r32, 3u);
            const uint16_t q = divide64_unsigned(prod);

            if (q == 1)
                srcY = 0x10;

            BitBlt(
                reinterpret_cast<HDC>(g_hdc),
                dstX, dstY,
                16, 16,
                memDC,
                0xC0, srcY,
                0x00CC0020 
            );
        }
    }

    DeleteDC(memDC);
}

void animateOneWayTilesEvery4Frames()
{
    if ((frameCounter % 4) != 0)
        return;

    HDC memDC = CreateCompatibleDC(reinterpret_cast<HDC>(g_hdc));
    SelectObject(memDC, bitmap_wall);

    // Toggle frame (asm: word_907A + var_4)
    int srcY = 0;
    if (g_oneWayAnimPhase != 0)
    {
        srcY = 0x00;
        g_oneWayAnimPhase = 0;
    }
    else
    {
        srcY = 0x10;
        g_oneWayAnimPhase = 1;
    }

    constexpr uint16_t TILE_ONEWAY_LTR = 0xFFF2;
    constexpr uint16_t TILE_ONEWAY_RTL = 0xFFF1;
    constexpr uint16_t TILE_ONEWAY_TTB = 0xFFF0;
    constexpr uint16_t TILE_ONEWAY_BTT = 0xFFEF;

    constexpr int kTileW = 16;
    constexpr int kTileH = 16;
    constexpr uint32_t kRopSrcCopy = 0x00CC0020;

    for (int row = 0; row < GameState::GRID_COLS; ++row)
    {
        for (int col = 0; col < GameState::GRID_ROWS ; ++col) 
        {
            const uint16_t tile = gameGrid[col][row];

            if (tile == TILE_ONEWAY_LTR || tile == TILE_ONEWAY_RTL)
            {
                const int dstY = row * cellHeight;
                const int dstX = col * cellWidth;

                BitBlt(
                    reinterpret_cast<HDC>(g_hdc),
                    dstX, dstY,
                    kTileW, kTileH,
                    memDC,
                    0xE0, srcY,
                    kRopSrcCopy
                );
            }
            else if (tile == TILE_ONEWAY_TTB || tile == TILE_ONEWAY_BTT)
            {
                const int dstY = row * cellHeight;
                const int dstX = col * cellWidth;

                BitBlt(
                    reinterpret_cast<HDC>(g_hdc),
                    dstX, dstY,
                    kTileW, kTileH,
                    memDC,
                    0xD0, srcY,
                    kRopSrcCopy
                );
            }
        }
    }

    DeleteDC(memDC);
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

void tickSpawnersEvery7Frames()
{
    if ((frameCounter % 7) != 0)
    {
        finalizeLevelVisuals();
        return;
    }

    for (int spawnerIndex = 0; spawnerIndex < g_activeSpawnerCount; ++spawnerIndex)
    {
        const int byteOffset = spawnerIndex * 8;

        uint16_t& spawnerPhase = *reinterpret_cast<uint16_t*>(
            reinterpret_cast<uint8_t*>(g_spawners.spawnerPhaseBase) + byteOffset);

        uint16_t& spawnDelayCounter = *reinterpret_cast<uint16_t*>(
            reinterpret_cast<uint8_t*>(g_spawners.spawnDelayCounterBase) + byteOffset);

        const int baseRow = *reinterpret_cast<uint16_t*>(
            reinterpret_cast<uint8_t*>(g_spawners.spawnerBaseRowBase) + byteOffset);

        const int baseCol = *reinterpret_cast<uint16_t*>(
            reinterpret_cast<uint8_t*>(g_spawners.spawnerBaseColBase) + byteOffset);

        const bool isGroupA = (spawnerPhase >= 0x17 && spawnerPhase <= 0x1A);
        const bool isGroupB = (spawnerPhase >= 0x1B && spawnerPhase <= 0x1E);
        if (!isGroupA && !isGroupB)
            continue;

        if (isGroupA)
        {
            ++spawnerPhase;
            if (spawnerPhase > 0x1A) spawnerPhase = 0x17;
        }
        else
        {
            ++spawnerPhase;
            if (spawnerPhase > 0x1E) spawnerPhase = 0x1B;
        }

        moveAndRedrawEntity(spawnerIndex, baseRow, baseCol);

        if (spawnDelayCounter < baseCol)
        {
            ++spawnDelayCounter;
            continue;
        }

        int targetRow = baseRow;
        int targetCol = baseCol;
        int spawnTileId = 0;

        if (isGroupA)
        {
            switch (spawnerPhase)
            {
                case 0x17: spawnTileId = 4;  targetRow = baseRow + 1; targetCol = baseCol;     break;
                case 0x18: spawnTileId = 1;  targetRow = baseRow;     targetCol = baseCol - 1; break;
                case 0x19: spawnTileId = 3;  targetRow = baseRow - 1; targetCol = baseCol;     break;
                case 0x1A: spawnTileId = 2;  targetRow = baseRow;     targetCol = baseCol + 1; break;
                default: break;
            }
            trySpawnFromGroupAIfTargetEmpty(targetRow, targetCol, spawnTileId, spawnDelayCounter);
        }
        else
        {
            switch (spawnerPhase)
            {
                case 0x1B: spawnTileId = 0x0E; targetRow = baseRow + 1; targetCol = baseCol;     break;
                case 0x1C: spawnTileId = 0x0B; targetRow = baseRow;     targetCol = baseCol - 1; break;
                case 0x1D: spawnTileId = 0x0D; targetRow = baseRow - 1; targetCol = baseCol;     break;
                case 0x1E: spawnTileId = 0x0C; targetRow = baseRow;     targetCol = baseCol + 1; break;
                default: break;
            }
            trySpawnFromGroupBIfTargetEmpty(targetRow, targetCol, spawnTileId, spawnDelayCounter);
        }
    }

    finalizeLevelVisuals();
}

void trySpawnFromGroupBIfTargetEmpty(int targetRow, int targetCol, int spawnTileId, uint16_t& spawnDelayCounter)
{
    spawnAtIfEmpty(targetRow, targetCol, spawnTileId, spawnDelayCounter);
}

static inline bool isGridCellEmpty(int targetRow, int targetCol)
{
    return gameGrid[targetCol][targetRow] == 0xFFFF;
}

void trySpawnFromGroupAIfTargetEmpty(int targetRow, int targetCol, int spawnTileId, uint16_t& spawnDelayCounter)
{
    spawnAtIfEmpty(targetRow, targetCol, spawnTileId, spawnDelayCounter);
}

static inline void spawnAtIfEmpty(int targetRow, int targetCol, int spawnTileId, uint16_t& spawnDelayCounter)
{
    if (!isGridCellEmpty(targetRow, targetCol))
        return;

    spawnDelayCounter = 0;

    const int spawnedEntityIndex = registerLevelChange(spawnTileId, targetRow, targetCol);
    moveAndRedrawEntity(spawnedEntityIndex, targetRow, targetCol);
}

void updateLevelVisualsAndAnimations()
{
    renderSpawnerSpritesEvery3Frames();
    animateDiamondsEvery10Frames();
    animateOneWayTilesEvery4Frames();
    tickSpawnersEvery7Frames();
    updateAnimatedBlocksSprites();
}

void renderSpawnerSpritesEvery3Frames()
{
    if ((frameCounter % 3) != 0)
        return;

    HDC wndDC = GetDC(g_hdc);
    if (!wndDC) return;

    HDC memDC = CreateCompatibleDC(wndDC);
    if (!memDC) { ReleaseDC(g_hdc, wndDC); return; }

    HGDIOBJ oldBmp = SelectObject(memDC, bitmap_block);

    auto* base = reinterpret_cast<uint8_t*>(g_spawners.spawnerPhaseBase);       // 0x172E
    auto* rowB = reinterpret_cast<uint8_t*>(g_spawners.spawnerBaseRowBase);     // 0x1730
    auto* colB = reinterpret_cast<uint8_t*>(g_spawners.spawnerBaseColBase);     // 0x1732
    auto* frmB = reinterpret_cast<uint8_t*>(g_spawners.spawnDelayCounterBase);  // 0x1734

    for (int spawnerIndex = 0; spawnerIndex < g_activeSpawnerCount; ++spawnerIndex)
    {
        const int byteOffset = spawnerIndex * 8;

        const uint16_t spawnerPhase = *reinterpret_cast<uint16_t*>(base + byteOffset);
        if (spawnerPhase < 0x0F || spawnerPhase > 0x13)
            continue;

        const int spawnerRow = *reinterpret_cast<uint16_t*>(rowB + byteOffset);
        const int spawnerCol = *reinterpret_cast<uint16_t*>(colB + byteOffset);
        uint16_t& spawnerAnimFrame = *reinterpret_cast<uint16_t*>(frmB + byteOffset);

        const int dstY = spawnerRow * cellHeight;
        const int dstX = spawnerCol * cellWidth;

        const int srcX = static_cast<int>(spawnerPhase) << 4;
        const int srcY = static_cast<int>(spawnerAnimFrame) << 4;

        BitBlt(wndDC, dstX, dstY, 16, 16, memDC, srcX, srcY, SRCCOPY);

        spawnerAnimFrame = static_cast<uint16_t>((spawnerAnimFrame + 1) & 3);
    }

    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(g_hdc, wndDC);
}

void handleDirectionalHotkeyAndAdvanceLevel(uint16_t key)
{
    if (g_interactionMode != GameInteractionMode::NormalPlay) // asm: ==0
        return;

    Delta delta{};
    if (!decodeDirectionalHotkey(key, delta))
        return;

    previousRow = static_cast<int>(srcRow) + delta.dRow;
    previousCol = static_cast<int>(srcCol) + delta.dCol;

    g_hasPendingModal = 1;

    tickLevelFlow();
    updateLevelVisualsAndAnimations();
}

static bool decodeDirectionalHotkey(uint16_t key, Delta& out)
{
    switch (key)
    {
        case '!': out = { +1,  0 }; return true; // 33
        case '"': out = { +1, +1 }; return true; // 34
        case '#': out = { +1, -1 }; return true; // 35
        case '$': out = { -1, -1 }; return true; // 36
        case '%': out = { -1,  0 }; return true; // 37
        case '&': out = {  0, +1 }; return true; // 38
        case '\'':out = {  0, -1 }; return true; // 39
        case '(': out = { -1, +1 }; return true; // 40
        default:  return false;
    }
}


static inline const char* ptr16_to_cstr(uint16_t off)
{
    if (off == 0x29FA) return displayTextBuffer;
    return "";
}



void tickLevelFlow()
{
    g_timerActive = false;

    initializeWindowHandleIfNeeded();

    if (g_hasPendingModal != 0)
        handleDialogClose();

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
            ++levelIndex;
        }

        loadLevelByIndex(levelIndex);
        showNewLevelDialog();

        matchedEntryCount = 0;

        clearStatusLine("");

        InvalidateRect(g_mainWindow, nullptr, TRUE);
        UpdateWindow(g_mainWindow);
    }
    else
    {
        gameMainLoop();

        if (remainingLives < 0)
        {
            runTileSparkleEffectSdl(2);
            drawRectangleFromGrid(srcRow, srcCol);
            showGameOverDialog();

            matchedEntryCount = 0;
            loadLevelByIndex(levelIndex);
            clearStatusLine("");

            InvalidateRect(g_mainWindow, nullptr, TRUE);
            UpdateWindow(g_mainWindow);
        }
        else if (hasLevelTransition != 0)
        {
            handleDialogClose();
        }

        advanceToNextLevelOrBlock();
    }

    g_timerActive = true;
}

void updateDisplayString(const char* str) {
    if (!str) str = "";
    const std::size_t cap = static_cast<std::size_t>(stringBufferCapacity);
    if (cap == 0) return;
    std::strncpy(stringBuffer, str, cap - 1);
    stringBuffer[cap - 1] = '\0';
    g_needsRedraw = true;
}

template <typename T>
static inline void gdiDelete(T& obj) noexcept
{
    if (!obj) return;
    DeleteObject(reinterpret_cast<HGDIOBJ>(obj));
    obj = nullptr;
}

void destroyGdiResources() noexcept
{
    gdiDelete(brush_black);
    gdiDelete(brush_white);
    gdiDelete(brush_blue);
    gdiDelete(brush_green);
    gdiDelete(brush_red);
    gdiDelete(pen_black);
    gdiDelete(pen_gray);
    gdiDelete(pen_blue);
    gdiDelete(bitmap_kye);
    gdiDelete(bitmap_block);
    gdiDelete(bitmap_wall);
}

static void clearStatusLine(const char* str)
{
    if (!str) {
        g_statusLineBuffer[0] = '\0';
    } else {
        std::strcpy(g_statusLineBuffer, str);
    }
    InvalidateRect(g_mainWindow, nullptr, TRUE);
}

void renderFullWallLayerSdl()
{
    HDC wndDC = GetDC(g_hdc);
    if (!wndDC) return;

    HDC memDC = CreateCompatibleDC(wndDC);
    if (!memDC) { ReleaseDC(g_hdc, wndDC); return; }

    HGDIOBJ oldObj = SelectObject(memDC, bitmap_wall);

    for (int row = 0; row < 30; ++row)
    {
        for (int col = 0; col < 20; ++col)
        {
            renderWallTile(row, col, memDC);
        }
    }

    SelectObject(memDC, oldObj);
    DeleteDC(memDC);
    ReleaseDC(g_hdc, wndDC);
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
        runTileSparkleEffectSdl(0);
        return;
    }

    const int effectId = (g_levelJustLoadedFlag != 0) ? 1 : 0;
    g_levelJustLoadedFlag = 0;
    runTileSparkleEffectSdl(effectId);
}

static void dispatchEventWithDefaults(int16_t eventCode)
{
    handleEngineEvent(eventCode, /*param0=*/0, /*param1=*/1);
}

void updateSlotAndDigitsFromCounter(
    uint32_t counterLow,                 // arg_0
    uint32_t counterHigh,                // arg_2 (mais en pratique on reconstruit 64b)
    SlotState& slot,                // *arg_4 (si)
    OutDigits& outDigits            // *arg_6 (di)
)
{
    // Rebuild 64-bit value from 16-bit halves as in original calling convention:
    // Ici on suppose que counterLow/High viennent déjà “alignés” (sinon adapte à tes wrappers).
    uint64_t counter = (static_cast<uint64_t>(counterHigh) << 16) | (static_cast<uint64_t>(counterLow) & 0xFFFFu);

    loadSpeedSettingFromEnvVar();

    // (speedMultiplierHigh:Low) + 0x12CE:A600
    constexpr uint32_t kSpeedOffsetLow  = 0xA600u;
    constexpr uint32_t kSpeedOffsetHigh = 0x12CEu;

    const uint64_t speed = (static_cast<uint64_t>(speedMultiplierHigh) << 16) | static_cast<uint64_t>(speedMultiplierLow);
    const uint64_t offset = (static_cast<uint64_t>(kSpeedOffsetHigh) << 16) | static_cast<uint64_t>(kSpeedOffsetLow);

    counter = counter - (speed + offset);

    outDigits.zero = 0;
    {
        int64_t qSigned = 0;
        outDigits.c = divmodSigned_u8(static_cast<int64_t>(counter), 60, qSigned);

        uint64_t qUnsigned = 0;
        (void)divmodUnsigned_u8(counter, 60, qUnsigned);
        counter = qUnsigned;

        outDigits.a = divmodSigned_u8(static_cast<int64_t>(counter), 60, qSigned);

        (void)divmodUnsigned_u8(counter, 60, qUnsigned);
        counter = qUnsigned;
    }

    constexpr uint32_t kDivA = 0x88F8u;

    {
        uint64_t q = 0;
        (void)divmodUnsigned_u8(counter, kDivA, q);

        constexpr uint16_t kBaseAdd = 0x07BC;
        const uint16_t scaled = static_cast<uint16_t>((static_cast<uint16_t>(q & 0xFFFFu) << 2) + kBaseAdd);
        slot.base = scaled;

        int64_t qSigned = 0;
        (void)divmodSigned_u8(static_cast<int64_t>(counter), kDivA, qSigned);
        counter = static_cast<uint64_t>(qSigned);
    }
    constexpr uint32_t kCmp = 0x2250u;
    constexpr uint32_t kDivB = 0x2238u;

    if (static_cast<int64_t>(counter) >= 0 && geUnsigned64(counter, kCmp))
    {
        counter -= kCmp;
        slot.base = static_cast<uint16_t>(slot.base + 1);

        uint64_t q = 0;
        (void)divmodUnsigned_u8(counter, kDivB, q);
        slot.base = static_cast<uint16_t>(slot.base + static_cast<uint16_t>(q & 0xFFFFu));

        int64_t qSigned = 0;
        (void)divmodSigned_u8(static_cast<int64_t>(counter), kDivB, qSigned);
        counter = static_cast<uint64_t>(qSigned);
    }

    if (speedFallbackUsed != 0)
    {
        constexpr uint32_t kDiv24 = 24u;
        constexpr uint16_t kPosBias = 0xF84E;

        int64_t qS = 0;
        (void)divmodSigned_u8(static_cast<int64_t>(counter), kDiv24, qS);

        uint64_t qU = 0;
        (void)divmodUnsigned_u8(counter, kDiv24, qU);

        const uint16_t pos = static_cast<uint16_t>(slot.base + kPosBias);
        if (canPlaceEntityAtPosition(pos))
        {
            counter += 1;
        }
    }
    {
        constexpr uint32_t kDiv24 = 24u;

        int64_t qS = 0;
        outDigits.b = divmodSigned_u8(static_cast<int64_t>(counter), kDiv24, qS);

        uint64_t qU = 0;
        (void)divmodUnsigned_u8(counter, kDiv24, qU);
        counter = qU + 1;
    }

    if ((slot.base & 3u) == 0)
    {
        constexpr uint64_t k60 = 60;

        if (static_cast<int64_t>(counter) >= 0 && counter > k60)
        {
            counter -= 1;
        }
        else if (counter == k60)
        {
            slot.index = 2;
            slot.value = 0x1D;
            return;
        }
    }

    // default path
    slot.index = 0;

    // emulate:
    // while (table[index] <= counter) { counter -= table[index]; index++; }
    // then index++ and value = low(counter)
    while (true)
    {
        const int idx = static_cast<int>(slot.index);
        const int8_t step = g_thresholdTable[idx];
        const int64_t step64 = static_cast<int64_t>(step);

        // compare (step as signed 16/32) with counter as signed 64 in asm (cbw/cwd then cmp dx:ax vs arg2:arg0)
        if (step64 < 0) {
            // si table contient du négatif, on évite boucle infinie (sûreté)
            break;
        }

        if (static_cast<uint64_t>(step64) <= counter)
        {
            counter -= static_cast<uint64_t>(step64);
            slot.index = static_cast<uint8_t>(slot.index + 1);
            continue;
        }

        // loc_7B2C behavior: inc index; slot.value = low byte of remaining
        slot.index = static_cast<uint8_t>(slot.index + 1);
        slot.value = static_cast<uint8_t>(counter & 0xFFu);
        break;
    }
}

void handlePackedMessage(uint32_t message)
{
    const uint16_t msgLow    = static_cast<uint16_t>(message & 0xFFFFu);
    const uint16_t eventCode = static_cast<uint16_t>((message >> 16) & 0xFFFFu);

    showFileMessage(msgLow);
    dispatchEventWithDefaults(eventCode);
}

void dispatchNotificationByCode(uint16_t code)
{
    extern const uint16_t kCodes[6];

    uint16_t eventCode = 1;
    uint16_t messageToken = 0x123C;

    bool matched = false;
    for (int i = 0; i < 6; ++i) {
        if (kCodes[i] == code) { matched = true; break; }
    }

    if (!matched) {
        handlePackedMessage({ messageToken, eventCode });
        return;
    }

    handlePackedMessage({ messageToken, eventCode });
}

static bool handleOpenFileDialogEvent(
    OpenDialogState& st,
    DialogMsg msg,
    uint16_t wParam // “command id”: OK=1, Cancel=2, list=0x194/0x195, etc.
)
{
    switch (msg) {
        case DialogMsg::InitDialog: {
            // initOpenFileDialogDirectoryLists(hwnd)
            // + set "*.kye" + focus
            ensureDefaultMask(st);
            st.filenameEdit = st.selectedMask;
            return false; // fidèle: retourne 0 sur WM_INITDIALOG
        }

        case DialogMsg::Command: {
            if (wParam == 2) { // Cancel
                st.accepted = false;
                return true;
            }

            if (wParam == 1) { // OK
                // GETDLGITEMTEXT(edit) -> st.filenameEdit déjà à jour côté UI
                if (containsWildcard(st.filenameEdit)) {
                    // équiv splitPathDirAndName + refresh lists
                    // (ici, on ne fait que marquer "pas accepté" et laisser UI naviguer)
                    st.accepted = false;
                    // initOpenFileDialogDirectoryLists(...)
                    return true;
                } else {
                    // appendDefaultExtensionIfMissing(ds:050C, ds:0699)
                    // derivedDefaultExt doit être ".kye" ou "kye" selon ce que tu copies en 0x699
                    appendDefaultExtensionIfMissing(st.filenameEdit, st.derivedDefaultExt.empty() ? "kye" : st.derivedDefaultExt);
                    st.accepted = true;
                    return true;
                }
            }

            if (wParam == 0x0194 || wParam == 0x0195) {
                // DLGDIRSELECT + update edit + selection
                // Ici: placeholder -> côté UI, tu mettras filenameEdit selon la sélection
                return true;
            }

            return false;
        }
    }

    return false;
}

void handleEvent(const SDL_Event& e) {
    switch (e.type) {
        case SDL_EVENT_QUIT:
            destroyGdiResources();
            g_running = false;
            break;

        case SDL_EVENT_KEY_DOWN:
            g_keyDownFlag = 1;
            handleDirectionalHotkeyAndAdvanceLevel(
                (uint16_t)e.key.keysym.sym,
                0
            );
            break;

        case SDL_EVENT_KEY_UP:
            g_keyDownFlag = 0;
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                handleGameClick(0, e.button.x, e.button.y, 0);
            } else if (e.button.button == SDL_BUTTON_RIGHT) {
                handlePendingInteractionClick(0, e.button.x, e.button.y, 0);
            }
            break;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                cancelPendingInteraction(e.button.x, e.button.y, 0);
            }
            break;

        case SDL_EVENT_MOUSE_MOTION:
            handlePendingInteractionFinalize(e.motion.x, e.motion.y, 0);
            break;

        default:
            break;
    }
}

void drawHudCellFromIndex(
    SDL_Renderer* renderer,
    const HudTextures& tex,
    int16_t cellIndex
) {
    const int16_t row = cellIndex / 0x10;
    const int16_t col = cellIndex % 0x10;

    const int cellLeft = row * 20;
    const int cellTop  = col * 20;

    const bool selected = (cellIndex == g_currentNameIndex);

    SDL_Rect cellRect{ cellLeft, cellTop, 20, 20 };
    drawFrame(renderer, cellRect, selected);

    const HudCellEntry& entry = g_hudCells[cellIndex];

    // équivalent: if ([+6BC] == 0) => computeHudCellRectFromIndex(...)
    if (entry.hasData == 0) {
        computeHudCellRectFromIndex(renderer, cellIndex);
        return;
    }

    const int dstX = cellLeft + 2;
    const int dstY = cellTop + 2;
    SDL_Rect dst{ dstX, dstY, 16, 16 };

    switch (entry.type) {
        case 0:
            computeHudCellRectFromIndex(renderer, cellIndex);
            return;

        case 1: {
            SDL_Rect src = computeWallSrcRect(entry.value);
            SDL_RenderTexture(renderer, tex.wall, &src, &dst);
            return;
        }

        case 2: {
            SDL_Rect src = computeBlockSrcRect(entry.value);
            SDL_RenderTexture(renderer, tex.block, &src, &dst);
            return;
        }

        case 3: {
            SDL_Rect src{ 0, 0, 16, 16 };
            SDL_RenderTexture(renderer, tex.kye, &src, &dst);
            return;
        }

        default:
            return;
    }
}

void copyMappedStringForCode(uint16_t code)
{
    const uint16_t idx = static_cast<uint16_t>(code - 0x81);

    const char* selected = nullptr;
    switch (idx)
    {
        case 0:  selected = g_str_1169; break;
        case 1:  selected = g_str_1171; break;
        case 2:  selected = g_str_117A; break;
        case 3:  selected = g_str_1189; break;
        case 4:  selected = g_str_1192; break;
        case 5:  selected = g_str_119C; break;
        case 6:  selected = g_str_11A4; break;
        // idx 7..8: default
        case 9:  selected = g_str_11AF; break;
        case 10: selected = g_str_11BE; break;
        case 11: selected = g_str_11CE; break;
        default:
            selected = nullptr;
            break;
    }

    if (selected != nullptr)
    {
        copyString(g_messageBuffer_114A, selected);
        return; // <-- très probablement manquant/masqué dans le listing IDA
    }

    handlePackedMessage(g_packedMessage_113A, /*arg=*/3);
}

int dispatchValue(uint16_t value) {
    const uint16_t index = findIndexInTable(value);
    if (index == 0xFFFFu) {
        return 1; // not found / error
    }

    const uintptr_t entry = g_handlerOrStateTable[index];

    if (entry == 1u) {
        return 0; // explicitly disabled / already handled / ignore
    }

    if (entry == 0u) {
        if (value == 8u) {
            copyMappedStringForCode(0x8Cu);
        } else {
            dispatchNotificationByCode(value);
        }
        return 0;
    }

    // Otherwise: treat as callback and disarm (one-shot)
    g_handlerOrStateTable[index] = 0u;

    const uint16_t param = static_cast<uint16_t>(g_handlerParamTable[index]);
    auto* handler = reinterpret_cast<HandlerFn>(entry);
    handler(param, value);

    return 0;
}

i16 handleNotificationOrCallbackByCode(uint16_t eventCode)
{
    const i16 slotIndex = findIndexInTable(eventCode);
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

i16 triggerNotification_0x0016()
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

void processCallbackQueueFromEngineEvent() {
  // en ASM: processCallbackQueue(0x127C, 0x127C)
  // ici: placeholders
  processCallbackQueue(/*begin*/nullptr, /*end*/nullptr);
}
