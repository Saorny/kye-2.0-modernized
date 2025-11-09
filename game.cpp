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


uint32_t computeAdjustedTime(Entity* entity, const EntityActionStruct* params) {
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

    for (int entityIndex = 0; entityIndex < currentLevelStateVersion; ++entityIndex) {
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