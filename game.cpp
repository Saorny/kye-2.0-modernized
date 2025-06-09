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

constexpr uint8_t STATUS_FREE = 0x00;
constexpr uint8_t STATUS_USED = 0xFF;

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

void loadNextLevelOrBlock() {
    g_timerActive = false;
    initializeWindowHandleIfNeeded();

    if (hasPendingDialogBox) {
        handleDialogClose();
    }

    if (hasEntryList) {
        if (currentLevelIndex >= totalLevelCount) {
            showFinalDialog();
            currentLevelIndex = 1;
        } else {
            showLevelDoneDialog();
            ++currentLevelIndex;
        }

        loadLevelByIndex(currentLevelIndex);
        showNewLevelDialog();
        matchingEntryCount = 0;

        updateDisplayString("LEVEL_STRING"); // Remplacer 0x29FA par une vraie chaîne
        invalidateWindow();
    }
    else {
        sub_3684();

        if (remainingLives < 0) {
            runEffect(2);  // effet sonore ou visuel : "échec"
            drawRectangleFromGrid(row, col);
            showGameOverDialog();

            matchingEntryCount = 0;
            loadLevelByIndex(currentLevelIndex);
            updateDisplayString("LEVEL_STRING");
            invalidateWindow();
        }

        if (levelTransitionFlag) {
            handleDialogClose();
        }
    }

    releaseDialogResources();
    g_timerActive = true;
}

void updateLevelEntitiesEvery30Frames() {
    if (frameCounter % 30 != 0) return;

    for (int i = 0; i < currentLevelStateVersion; ++i) {
        auto& entry = g_gameState.entities[i];

        if (entry.type < 0x32 || entry.type > 0x3B) {
            continue;
        }

        int row = entry.row;
        int col = entry.col;

        if (entry.type > 0x32) {
            --entry.type;
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
            if (g_gameState.entities[targetIndex].type == 5) {
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
            if (g_gameState.entities[targetIndex].type == 5) {
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
            if (g_gameState.entities[targetIndex].type == 6) {
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
            if (g_gameState.entities[targetIndex].type == 6) {
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
    levelTransitionFlag = 1;

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

    // Fallback : aucune case vide trouvée
    previousRow = row;
    previousCol = col;
}

void loadLevelByIndex(int levelIndex) {
    if (!fileAccessEnabled)
        return;

    FileLike* file = openAndPrepareFileFromSlot("level.dat", "rt");
    if (!file) {
        showMessage("level.dat", "Unable to open level file.");
        resetLevelStateMemory();
        fileAccessEnabled = false;
        return;
    }

    resetAndSeekFile(file, 0);
    
    char lineBuffer[80] = {};
    readLineToBuffer(file, lineBuffer, 0x4F);
    totalLevelCount = parseSignedDecimalString(lineBuffer);
    
    hasEntryList = false;
    hasPendingDialogBox = false;
    levelLoadState = 1;
    levelTransitionFlag = 0;
    remainingLives = 3;
    selectedObjectIndex = -1;

    // Clamp le niveau demandé
    if (levelIndex < 1 || levelIndex > totalLevelCount)
        levelIndex = 1;

    // On lit autant de blocs que nécessaire
    for (int i = 0; i < levelIndex; ++i) {
        char structuredBlock[0x350] = {};
        readStructuredBlock(file, structuredBlock);  // lit le bloc de 3 lignes + 20 éléments
        if (i + 1 == levelIndex) {
            // Traitement du bloc voulu

            std::memcpy(&g_levelName[0], &structuredBlock[0x000], 83);   // → 2A9A
            std::memcpy(&g_levelHint[0], &structuredBlock[0x0CD], 83);   // → 2A4A
            std::memcpy(&displayTextBuffer[0], &structuredBlock[0x154], 83); // → 29FA

            // Traite les 20 objets du niveau
            for (int j = 0; j < 20; ++j) {
                const char* lineData = &structuredBlock[0x1A3 + col * 0x23];
                
                loadLevelRow(col, lineData);  // ← correspond à sub_1DF3
            }

            postLoadLevel(); // ← sub_1A37 (probablement nettoyage ou affichage)
        }
    }

    cleanFile(file);
}

void resetLevelStateMemory() {
    currentLevelStateVersion = 0;

    // Copie des valeurs par défaut (noms, indices, labels ?)
    std::memcpy(loadedLevelName, reinterpret_cast<const void*>(0x045A), 3 * sizeof(uint16_t));
    std::memcpy(loadedLevelHint, reinterpret_cast<const void*>(0x0460), 4 * sizeof(uint16_t));
    std::memcpy(displayTextBuffer, reinterpret_cast<const void*>(0x0468), 7 * sizeof(uint16_t));

    // Reset de la grille de jeu principale : g_state.bottomEntityMap[20][30]
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
    if (currentLevelStateVersion >= 0x258)
        return false;

    // 2D grid version marker
    g_state.rightEntityMap[row][col] = currentLevelStateVersion;

    // Register into linear changeList
    LevelChange& entry = changeList[currentLevelStateVersion];
    entry.tileId = tileId;
    entry.row = row;
    entry.col = col;
    entry.extra = 0;

    ++currentLevelStateVersion;
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

void postLoadLevel() {
    bool hasSelection = false;
    for (int y = 0; y < ROWS && !hasSelection; ++y) {
        for (int x = 0; x < COLS; ++x) {
            if (g_state.rightEntityMap[y][x] == 0xFFF3) {
                hasSelection = true;
                break;
            }
        }
    }

    if (!hasSelection) {
        selectionState = 0xFFF3;
    }

    bool hasPlayer = false;
    int foundRow = -1, foundCol = -1;
    for (int y = 0; y < ROWS && !hasPlayer; ++y) {
        for (int x = 0; x < COLS; ++x) {
            if (g_state.rightEntityMap[y][x] == 0xFFFE) {
                hasPlayer = true;
                foundRow = y;
                foundCol = x;
                break;
            }
        }
    }

    if (!hasPlayer || foundRow != row || foundCol != col) {
        row = col = spawnRow = spawnCol = 3;
        selectedTileValue = 0xFFFE;
    }

    for (int y = 0; y < ROWS; ++y) {
        for (int x = 0; x < COLS; ++x) {
            int& val = g_state.rightEntityMap[y][x];
            if ((val >= 0xFFF5 && val <= 0xFFFD) || val == 0 || static_cast<int16_t>(val) < 0) {
                if (val > 0) markEntryInactive(val);
                val = 0xFFF9;
            }
        }
    }

    // Nettoyage 2 : levelChangeTable (base = 0x12A4)
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 18; ++x) {
            int& val = levelChangeTable[y * 0x28 + x];
            if ((val >= 0xFFF5 && val <= 0xFFFD) || val == 0 || val < 0) {
                if (val > 0) markEntryInactive(val);
                val = 0xFFF9;
            }
        }
    }

    // Nettoyage 3 : gameGrid (0x127E à 0x12A6)
    for (int y = 0; y < ROWS; ++y) {
        for (int x = 0; x < COLS; ++x) {
            uint16_t& val = gameGrid[y][x];
            if ((val >= 0xFFF5 && val <= 0xFFFD) || val == 0 || static_cast<int16_t>(val) < 0) {
                if (val > 0) markEntryInactive(val);
                val = 0xFFF9;
            }
        }
    }

    // Nettoyage 4 : g_state.bottomEntityMap (0x1706 à 0x172E)
    for (int y = 0; y < 19; ++y) {
        for (int x = 0; x < 15; ++x) {
            int& val = g_state.bottomEntityMap[y][x];
            if ((val >= 0xFFF5 && val <= 0xFFFD) || val == 0 || static_cast<int16_t>(val) < 0) {
                if (val > 0) markEntryInactive(val);
                val = 0xFFF9;
            }
        }
    }

    finalizeLevelVisuals();
}

bool replaceEntityIfTargetMatches(uint16_t index, uint16_t newRow, uint16_t newCol) {
    const int rowOffset = newRow * COLS;
    const int offset = rowOffset + newCol;

    int16_t currentEntity = gameGrid[newRow][newCol];
    if (currentEntity < 0)
        return false;

    // Vérifie si l'entité cible est de type 0x1F
    if (g_gameState.entityTypeTable[currentEntity] != 0x1F)
        return false;

    // Sauvegarde de la position originale de l'entité source
    uint16_t oldRow = g_gameState.entityRowTable[index];
    uint16_t oldCol = g_gameState.entityColTable[index];

    // Marque l'ancienne entité comme inactive
    g_gameState.entityTypeTable[currentEntity] = 0x20;
    g_gameState.entityTimerTable[currentEntity] = 0;

    // Remplace par la nouvelle entité et redessine
    moveAndRedrawEntity(currentEntity, newRow, newCol);
    drawRectangleFromGrid(oldRow, oldCol);

    // Marque l'ancienne entité comme inactive
    markEntryInactive(index);

    return true;
}


void markEntryInactive(int index) {
    if (index >= 0 && index < MAX_CHANGES) {
        changeList[index].tileId = 0x00FF;
    }
}

void finalizeLevelVisuals() {
    int i = 0;
    while (i < currentLevelStateVersion) {
        if (changeList[i].tileId == 0x00FF) {
            if (currentLevelStateVersion < 1) break;
            --currentLevelStateVersion;

            // Décale tous les éléments suivants
            for (int j = i; j < currentLevelStateVersion; ++j) {
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
    if (levelTransitionFlag != 0) {
        runEffect(2); // Effet visuel/sonore pour fermeture de dialogue
        drawRectangleFromGrid(row, col);

        row = previousRow;
        col = previousCol;

        // Positionne le joueur à sa précédente position
        g_state.bottomEntityMap[row][col] = 0xFFFE;

        runEffect(1); // Peut-être un effet de confirmation
        levelTransitionFlag = 0;
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
    for (int y = 0; y < ROWS; ++y) {
        for (int x = 0; x < COLS; ++x) {
            if (gameGrid[y][x] == 0xFFFE) {
                return true;
            }
        }
    }
    return false;
}

bool canPlaceEntityAtPosition(uint16_t xPos, uint16_t& typeOut, uint16_t objectId, uint8_t mode) {
    uint16_t type = typeOut;

    if (type == 0) {
        uint16_t si = objectId;
        if (objectId >= 0x3B) {
            uint16_t tmp = xPos + 0x46;
            if ((tmp & 0x03) == 0)
                --si;
        }
        type = 0;
        while (si >= memoryTable_10C8[type]) {
            ++type;
        }
        typeOut = type;
    } else if (type >= 3) {
        uint16_t tmp = xPos + 0x46;
        if ((tmp & 0x03) != 0)
            --objectId;
        typeOut = type - 1;
        objectId += memoryTable_10C8[type - 1];
    }

    if (typeOut >= 4 && typeOut <= 10) {
        uint16_t cx;
        if (xPos > 0x10 && typeOut == 4) {
            cx = memoryTable_10C6[typeOut] + 7;
        } else {
            cx = memoryTable_10C8[typeOut];
        }

        uint16_t bx = (xPos + 0x7B2);
        if ((bx & 3) != 0)
            --cx;

        bx = (xPos + 1) / 4;
        bx += cx;

        uint16_t ax = 0x16D * xPos + bx + 4;
        uint16_t remainder = ax % 7;
        cx -= remainder;

        if (typeOut == 4) {
            if (objectId > cx)
                return false;
            if (objectId == cx && mode < 2)
                return false;
        } else {
            if (objectId < cx)
                return false;
            if (objectId == cx && mode > 1)
                return false;
        }

        return true;
    }

    return false;
}

int initOrHandleEvent(int arg) {
    return handleEvent(arg, 0, 0);
}

int handleEvent(int arg0, int arg2, int arg4) {
    if (arg4 == 0) {
        while (eventCounter > 0) {
            --eventCounter;
            uint16_t index = eventCounter;
            auto func = reinterpret_cast<void(*)()>(*reinterpret_cast<uint16_t*>(0x2E92 + 2 * index));
            func();
        }
        loc_B7();               // fonction de post-traitement
        validateBuffer();     // vérifie ou flush un buffer
    }

    if (arg2 == 0) {
        if (arg4 == 0) {
            setupFileMode();     // setup mode fichier
            reinterpret_cast<void(*)()>(externalCallback)();  // call d'une fonction externe
        }
        loc_CB(arg0);           // fonction d'arrêt propre ou libération
    }

    return 0;
}

void processCallbackQueue(uint16_t* start, uint16_t* end) {
    while (true) {
        uint8_t highestValue = 0;
        uint16_t* candidate = end;

        // Recherche du bloc avec valeur la plus haute en [bx+1]
        for (uint16_t* entry = start; entry < end; entry += 3) {
            if (entry[0] != 0xFF && entry[1] >= highestValue) {
                highestValue = entry[1];
                candidate = entry;
            }
        }

        // Si aucun bloc valide trouvé → fin
        if (candidate == end) return;

        // Marquer l’entrée comme utilisée
        bool wasZero = (candidate[0] == 0);
        candidate[0] = 0xFF;

        // Appel du callback pointé par [candidate + 2]
        void (*callback)() = *(void (**)(void))((uint8_t*)candidate + 2);
        if (wasZero) {
            callback();
        } else {
            callback();
        }

        // Recommencer
    }
}


uint32_t computeAdjustedTime(Entity* entity, const EntityParams* params) {
    loadSpeedSettingFromEnvVar();
    uint64_t time = ((uint64_t)speedMultiplierHigh << 16 | speedMultiplierLow) + 0x12CEA600;

    uint16_t base = entity->position + 0xF844;
    int16_t shifted = base >> 2;
    time += (uint64_t)shifted * 0x1F80786;

    uint16_t lowBits = base & 0x3;
    time += (uint64_t)lowBits * 0x33801E1;
    if (lowBits != 0)
        time += 0x00015180;

    uint16_t total = 0;
    for (int i = 0; i < entity->height - 1; ++i)
        total += entityTable[i + 0x10BC];

    total += entity->width - 1;
    if (entity->height > 2 && (entity->position & 3) == 0)
        ++total;

    uint16_t si = total * 0x18 + params->param1;
    if (speedFallbackUsed && canPlaceEntityAtPosition(entity->position + 0xF84E, 0, total, params->param1))
        --si;

    time += (uint64_t)si * 0xE10;
    time += (uint64_t)(params->param0) * 0x3C + params->param3;

    return (uint32_t)time;
}

void loadSpeedSettingFromEnvVar() {
    const char* env = findEnvVarValue(0x10EC); // admettons: "KYE_SPEED"

    if (!env || strlen(env) < 4) {
        fallback:
        speedFallbackUsed = true;
        speedMultiplierHigh = 0;
        speedMultiplierLow = 0x4650;
        copyString(dst, 0x10EF); // "slow"
        copyString(dest, 0x10F3); // "slow"
        return;
    }

    // Validation des 4 premiers caractères (doivent avoir flag 0x0C dans une table à 0xD6F)
    for (int i = 0; i < 3; ++i) {
        uint8_t ch = env[i];
        if ((lookupTable[ch] & 0x0C) == 0) goto fallback;
    }

    // Le 4e doit être '+' ou '-' ou un chiffre avec flag 0x02
    if (env[3] != '+' && env[3] != '-') {
        if ((lookupTable[(uint8_t)env[3]] & 0x02) == 0) goto fallback;
    }

    // Copie sécurisée dans une chaîne temporaire
    char padded[4] = {};
    strncpy(padded, env, 3); // pas +4 ! Le 4e est géré différemment
    padded[3] = '\0';

    // Parse le reste de la chaîne
    int32_t speed = parseSignedDecimalString(env + 3);

    // Multiplie par 0xE10
    uint64_t result = static_cast<uint64_t>(speed) * 0xE10;
    speedMultiplierLow = static_cast<uint16_t>(result);
    speedMultiplierHigh = static_cast<uint16_t>(result >> 16);
    speedFallbackUsed = false;
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

int processMainLoop(
    void (*callback)(char** bufferPtrRef, const char* src, int length),
    char** bufferPtrRef,
    int value,
    uint8_t* typePtr
) {
    // Format string en entrée selon la valeur du type
    const char* format = getFormatStringFromType(*typePtr);
    const char* cursor = format;

    while (*cursor) {
        if (*cursor != '%') {
            // Scan jusqu’au prochain '%'
            const char* start = cursor;
            while (*cursor && *cursor != '%') {
                ++cursor;
            }

            int literalLen = static_cast<int>(cursor - start);
            callback(bufferPtrRef, start, literalLen);
            continue;
        }

        // On est sur un '%'
        ++cursor;
        uint8_t opcode = *cursor++;
        
        switch (opcode) {
            case 0x00: { // '%0' => écrire un '%'
                char percent = '%';
                callback(bufferPtrRef, &percent, 1);
                break;
            }
            case 0x01: { // '%1' => afficher `value` en tant que caractère
                char ch = static_cast<char>(value & 0xFF);
                callback(bufferPtrRef, &ch, 1);
                break;
            }
            case 0x02: { // '%2' => afficher `value` en hex (2 chiffres)
                char hex[3];
                std::snprintf(hex, sizeof(hex), "%02X", value & 0xFF);
                callback(bufferPtrRef, hex, 2);
                break;
            }
            case 0x03: { // '%3' => afficher `value` en décimal signé
                char temp[16];
                int8_t signedByte = static_cast<int8_t>(value & 0xFF);
                int len = std::snprintf(temp, sizeof(temp), "%d", signedByte);
                callback(bufferPtrRef, temp, len);
                break;
            }
            case 0x04: { // '%4' => afficher `value` comme mot signé
                char temp[16];
                int16_t wordVal = static_cast<int16_t>(value);
                int len = std::snprintf(temp, sizeof(temp), "%d", wordVal);
                callback(bufferPtrRef, temp, len);
                break;
            }
            case 0x05: { // '%5' => afficher `value` comme double-mot signé
                char temp[16];
                int32_t dwordVal = static_cast<int32_t>(value);
                int len = std::snprintf(temp, sizeof(temp), "%d", dwordVal);
                callback(bufferPtrRef, temp, len);
                break;
            }
            case 0x06: { // '%6' => string pointée par `typePtr + 1`
                const char* str = reinterpret_cast<const char*>(typePtr + 1);
                int len = static_cast<int>(std::strlen(str));
                callback(bufferPtrRef, str, len);
                break;
            }
            case 0x07: { // '%7' => Entity symbol
                const char* symbol = getEntitySymbol(*typePtr);
                callback(bufferPtrRef, symbol, static_cast<int>(std::strlen(symbol)));
                break;
            }
            case 0x08: { // '%8' => Entity name
                const char* name = getEntityName(*typePtr);
                callback(bufferPtrRef, name, static_cast<int>(std::strlen(name)));
                break;
            }
            case 0x09: { // '%9' => Entity type char
                char c = getEntityTypeChar(*typePtr);
                callback(bufferPtrRef, &c, 1);
                break;
            }
            case 0x0A: { // '%A' => Entity params
                const char* params = getEntityParams(*typePtr);
                callback(bufferPtrRef, params, static_cast<int>(std::strlen(params)));
                break;
            }
            case 0x0B: { // '%B' => Table symbol
                const char* sym = getTableSymbol(*typePtr);
                callback(bufferPtrRef, sym, static_cast<int>(std::strlen(sym)));
                break;
            }
            case 0x0C: { // '%C' => Table name
                const char* name = getTableName(*typePtr);
                callback(bufferPtrRef, name, static_cast<int>(std::strlen(name)));
                break;
            }
            case 0x0D: { // '%D' => Table type char
                char c = getTableTypeChar(*typePtr);
                callback(bufferPtrRef, &c, 1);
                break;
            }
            case 0x0E: { // '%E' => Table params
                const char* params = getTableParams(*typePtr);
                callback(bufferPtrRef, params, static_cast<int>(std::strlen(params)));
                break;
            }
            default: {
                // Ignorer ou loguer erreur ?
                break;
            }
        }
    }

    return static_cast<int>(*bufferPtrRef - format); // longueur totale écrite
}

void renderLivesAndLevelInfo() {
    if (someGlobalCondition != 0) {
        if (someGlobalCondition == 1) {
            // Fallback rendering mode, used in `loc_299B`
            int count = 0;
            uint16_t* gridPtr = reinterpret_cast<uint16_t*>(0x127E);
            for (int y = 0; y < 30; ++y) {
                for (int x = 0; x < 20; ++x) {
                    if (*gridPtr == 0xFFF3) ++count;
                    ++gridPtr;
                }
            }

            char buffer[108]{};
            prepareAndCallProcessMainLoop(bufferCopyCallback, &buffer, count, reinterpret_cast<uint8_t*>(0x4DC));
            int length = std::min<int>(strlen(buffer), 25);
            drawText(baseX + 5, baseY, buffer, length);
            prepareAndCallProcessMainLoop(bufferCopyCallback, &buffer, 0x2D5E, reinterpret_cast<uint8_t*>(0x4ED));
            length = std::min<int>(strlen(buffer), 25);
            drawText(baseX + 0x6E, baseY, buffer, length);
            return;
        }
        return;
    }

    drawRectangle(baseX, baseY, baseX + 0x46, baseY + 0x11, pen_gray);

    HDC tempDC = CreateCompatibleDC(g_windowHandle);
    SelectObject(tempDC, bitmap_kye);

    int xOffset = 0;
    for (int i = 0; i < remainingLives; ++i) {
        int x = baseX + xOffset + 1;
        int y = baseY + 1;
        BitBlt(g_windowHandle, x, y, 16, 16, tempDC, 0, 0, 0xCC0020);
        xOffset += 0x14;
    }

    DeleteDC(tempDC);

    // Draw level index
    char buffer[108]{};
    prepareAndCallProcessMainLoop(bufferCopyCallback, &buffer, currentLevelIndex, reinterpret_cast<uint8_t*>(0x4B8));
    int length = strlen(buffer);
    drawText(baseX + 0x50, baseY, buffer, length);

    // Count special tiles in grid
    int count = 0;
    uint16_t* gridPtr = reinterpret_cast<uint16_t*>(0x127E);
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (*gridPtr == 0xFFF3) ++count;
            ++gridPtr;
        }
    }

    // Display special tile count
    prepareAndCallProcessMainLoop(bufferCopyCallback, &buffer, count, reinterpret_cast<uint8_t*>(0x4C6));
    length = strlen(buffer);
    drawText(baseX + 0xA0, baseY, buffer, length);

    // Final static score
    prepareAndCallProcessMainLoop(bufferCopyCallback, &buffer, 0x2D5E, reinterpret_cast<uint8_t*>(0x4ED));
    length = std::min<int>(strlen(buffer), 25);
    drawText(baseX + 0x6E, baseY, buffer, length);
}

void gameMainLoop() {
    for (int entityIndex = 0; entityIndex < currentLevelStateVersion; ++entityIndex) {
        uint16_t entityType = g_gameState.entities[entityIndex].type;

        int newRow = g_gameState.entities[entityIndex].row;
        int newCol = g_gameState.entities[entityIndex].col;

        // Calcul ou récupération de di nécessaire pour certains handlers
        // Exemple : di pourrait être un index voisin utilisé dans certains cas
        int di = calculateDiForEntity(entityIndex); // À définir selon contexte

        switch (entityType) {
            case 0x00:
                handleSmartEntityCommon(entityIndex);
                break;
            case 0x01:
                handleSmartEntityType1(entityIndex, newRow, newCol);
                break;
            case 0x02:
                handleSmartEntityType2(entityIndex, newRow, newCol);
                break;
            case 0x03:
                handleSmartEntityType3(entityIndex, newRow, newCol, di);
                break;
            case 0x04:
                handleSmartEntityType4Main(entityIndex, newRow, newCol);
                break;
            case 0x0B: // 11 en décimal
                handleSmartEntityType11(entityIndex, newRow, newCol, di);
                break;
            default:
                handleUnknownEntityType(entityIndex);
                break;
        }
    }

    finalizeLevelVisuals();
}


// Pour les entités intelligentes standards (type 0x00)
void handleSmartEntityCommon(int entityIndex) {
    int newRow = g_gameState.entities[entityIndex].row;
    int newCol = g_gameState.entities[entityIndex].col;

    uint16_t type = g_gameState.entities[entityIndex].type;

    if (!isSmartEntityAltType(type)) {
        if (canEntityMove(entityIndex)) {
            moveAndRedrawEntity(entityIndex, newRow, newCol);
        }
    } else {
        handleSmartEntityAlt(entityIndex);
    }
}

void handleSmartEntityAlt(int entityIndex) {
    int newRow = g_gameState.entities[entityIndex].row;
    int newCol = g_gameState.entities[entityIndex].col;

    if (canEntityMove(entityIndex)) {
        moveAndRedrawEntity(entityIndex, newRow, newCol);
    }
}

bool handleSmartEntityType1(int entityIndex, int newRow, int newCol) {
    if (!tryMoveSmartEntity(entityIndex)) {
        if (!canEntityMove(entityIndex)) {
            // Essaye de déplacer à gauche si case libre
            if (isTileEmpty(newRow, newCol - 1)) {
                moveAndRedrawEntity(entityIndex, newRow, newCol - 1);
                return true;
            }
            // Sinon, tente un remplacement d'entité
            if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol - 1)) {
                return true;
            }

            // Vérifie l'entité à droite
            int rightEntityIndex = getEntityAt(newRow, newCol);
            if (rightEntityIndex != -1) {
                uint16_t rightType = g_gameState.entities[rightEntityIndex].type;
                if (rightType == 0x14) {
                    g_gameState.entities[entityIndex].type = 0x04;
                    return true;
                } else if (rightType == 0x15) {
                    g_gameState.entities[entityIndex].type = 0x03;
                    return true;
                }
            }
        }
    }
    return false;
}

bool handleSmartEntityType2(int entityIndex, int newRow, int newCol) {
    if (!tryMoveSmartEntity(entityIndex)) {
        if (!canEntityMove(entityIndex)) {
            // Essaye de déplacer à droite si case libre
            if (isTileEmpty(newRow, newCol + 1)) {
                moveAndRedrawEntity(entityIndex, newRow, newCol + 1);
                return true;
            }
            // Sinon, tente un remplacement d'entité
            if (replaceEntityIfTargetMatches(entityIndex, newRow, newCol + 1)) {
                return true;
            }

            // Vérifie le type actuel à la position
            uint16_t currentType = getEntityTypeAt(newRow, newCol);
            if (currentType == 0x14) {
                g_gameState.entities[entityIndex].type = 0x04;
                return true;
            } else if (currentType == 0x15) {
                g_gameState.entities[entityIndex].type = 0x03;
                return true;
            }
        }
    }
    return false;
}

bool handleSmartEntityType3(int entityIndex, int newRow, int newCol, int di) {
    if (!tryMoveSmartEntity(entityIndex)) {
        if (!canEntityMove(entityIndex)) {
            if (di < 0 || di >= GameState::NUM_ENTITIES) {
                return false;
            }

            uint16_t typeAtDi = g_gameState.entities[di].type;

            if (typeAtDi == 0x15) {
                g_gameState.entities[entityIndex].type = 0x04;
                moveAndRedrawEntity(entityIndex, newRow, newCol);
                return true;
            }
        }
    }
    return false;
}


bool handleSmartEntityType3_continue(int entityIndex, int newRow, int newCol) {
    // Partie suivante du code

    // Appelle tryMoveSmartEntity et test si succès
    if (!tryMoveSmartEntity(entityIndex)) {
        // Teste si peut bouger
        if (!canEntityMove(entityIndex)) {
            // Calcul d'adresse dans topEntityMap (offset 0x1256)
            int idx = newRow * GameState::GRID_COLS + newCol;

            // Récupère l'entité au-dessus (dans topEntityMap)
            int entityAbove = g_gameState.topEntityMap[newRow][newCol];

            // Si case au-dessus est vide (-1)
            if (entityAbove == -1) {
                // Déplace entité vers case au-dessus
                moveAndRedrawEntity(entityIndex, newRow - 1, newCol);
                return true;
            }

            // Sinon essaie de remplacer entité à la case au-dessus
            if (replaceEntityIfTargetMatches(entityIndex, newRow - 1, newCol)) {
                return true;
            }
        }
    }

    return false; // fallback handler
}

bool handleSmartEntityType4Main(int entityIndex, int newRow, int newCol) {
    if (tryMoveSmartEntity(entityIndex)) {
        return true;
    }

    if (!canEntityMove(entityIndex)) {
        return false;
    }

    int entityBelow = g_gameState.bottomEntityMap[newRow][newCol];
    if (entityBelow == -1) {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol);
        return true;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow + 1, newCol)) {
        return true;
    }

    int belowEntityIndex = getEntityAt(newRow + 1, newCol);
    if (belowEntityIndex >= 0) {
        uint16_t belowType = g_gameState.entities[belowEntityIndex].type;

        if (belowType == 0x14) {
            g_gameState.entities[entityIndex].type = 0x01;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return true;
        } else if (belowType == 0x15) {
            g_gameState.entities[entityIndex].type = 0x02;
            moveAndRedrawEntity(entityIndex, newRow, newCol);
            return true;
        }
    }

    return false;
}


// Fallback par défaut pour les types inconnus ou la fin de traitement
void handleUnknownEntityType(int& entityIndex) {}

uint16_t getEntityTypeAt(int row, int col) {
    if (row < 0 || row >= GameState::GRID_ROWS || col < 0 || col >= GameState::GRID_COLS)
        return 0xFFFF;

    int entityIndex = g_gameState.entityMap[row][col];
    if (entityIndex < 0 || entityIndex >= GameState::NUM_ENTITIES)
        return 0xFFFF;

    return g_gameState.entities[entityIndex].type;
}

bool isTileEmpty(int row, int col) {
    if (row < 0 || row >= GameState::GRID_ROWS || col < 0 || col >= GameState::GRID_COLS)
        return false;

    return g_gameState.entityMap[row][col] == -1;
}

inline bool isSmartEntityAltType(uint16_t type) {
    return type >= 0x32 && type <= 0x3B;
}

bool handleSmartEntity(int entityIndex) {
    if (!tryMoveSmartEntity(entityIndex)) {
        return true; // succès
    }
    handleUnknownEntityType(entityIndex);
    return false;
}

bool isSmartEntityType(uint16_t type) {
    return type == 0x00 ||
           (type >= 0x14 && type <= 0x1A) ||
           (type >= 0x32 && type <= 0x3B);
}

int getEntityAt(int row, int col) {
    if (row < 0 || row >= GameState::GRID_ROWS || col < 0 || col >= GameState::GRID_COLS)
        return -1;
    return g_gameState.entityMap[row][col];
}

bool handleSmartEntityType11(int entityIndex, int newRow, int newCol, int di) {
    int entityBelow = g_gameState.bottomEntityMap[newRow][newCol];

    if (entityBelow == -1) {
        moveAndRedrawEntity(entityIndex, newRow + 1, newCol);
        return true;
    }

    if (replaceEntityIfTargetMatches(entityIndex, newRow + 1, newCol)) {
        return true;
    }

    uint16_t var8 = g_gameState.someMap1[newRow * GameState::GRID_COLS + newCol];
    uint16_t varA = g_gameState.someMap2[newRow * GameState::GRID_COLS + newCol];
    uint16_t varC = g_gameState.someMap3[newRow * GameState::GRID_COLS + newCol];

    if (di >= 0 &&
        g_gameState.entities[di].type >= 0x0B &&
        g_gameState.entities[di].type <= 0x0E) {
        return true;
    }

    if (di >= 0 &&
        g_gameState.entities[di].type == 0x16) {
        return true;
    }

    bool conditionMet = (var8 == 0xFFFF && varA == 0xFFFF &&
                         (di == 0xFFFB || di == 0xFFFC || di == 0xFFF8) &&
                         varC == 0);

    if (conditionMet) {
        int dx = 1;
        int newX = newCol - 1;
        int newY = newRow + 1 - (dx << 1);
        moveAndRedrawEntity(entityIndex, newY, newX);
        return true;
    }

    return false;
}
