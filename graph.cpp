#include <iostream>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <dos.h>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <string>
#include <system_error>
#include <cstddef>
#include <windows.h>
#include <tuple>
#include <cstring>
#include "graph.h"
#include "dialog.h"
#include "game.h"
#include "util.h"
#include "system.h"

HBITMAP g_blockBitmap;


void maybeDrawPendingRectangle() {
    if (isPendingDraw == 0) {
        return;
    }

    uint16_t row = pendingRow;
    uint16_t col = pendingCol;

    // Calcule l'adresse du flag dans une table (tableau de short)
    uint16_t offset = (row * 0x28) + (col * 2);
    int16_t* cellFlags = reinterpret_cast<int16_t*>(0x127E); // Base de la table (hypothétique)
    
    if (cellFlags[offset / 2] == -1) {
        drawRectangleFromGrid(row, col);
    }

    isPendingDraw = 0;
}

void drawPendingBlock()
{
    // Obtenir le DC de la fenêtre
    HDC windowDC = GetDC(g_windowHandle);
    if (!windowDC) return;

    // Créer un DC mémoire compatible avec celui de la fenêtre
    HDC memoryDC = CreateCompatibleDC(windowDC);
    if (!memoryDC) {
        ReleaseDC(g_windowHandle, windowDC);
        return;
    }

    // Sélectionner le bitmap du bloc dans le DC mémoire
    HGDIOBJ oldObj = SelectObject(memoryDC, g_blockBitmap);

    // Calculer les coordonnées en pixels
    int pixelY = pendingRow * cellHeight; // en général 16
    int pixelX = pendingCol * cellWidth;  // idem

    // Blitter le bloc depuis le DC mémoire vers le DC de la fenêtre
    BitBlt(
        windowDC,       // DC de destination
        pixelX, pixelY, // position dans la fenêtre
        16, 16,         // taille du bloc
        memoryDC,       // DC source
        16, 0,          // position dans le bitmap
        SRCCOPY         // mode de copie
    );

    // Nettoyage
    SelectObject(memoryDC, oldObj);
    DeleteDC(memoryDC);
    ReleaseDC(g_windowHandle, windowDC);
}

int isPointInRect(int x, int y) {
    return PTINRECT(&g_mainRect, x, y);
}

void updateDisplayString(const char* str) {
    InvalidateRect(g_windowHandle, &invalidatedRect, FALSE);

    std::strncpy(stringBuffer, str, sizeof(stringBuffer) - 1);
    stringBuffer[sizeof(stringBuffer) - 1] = '\0';
}

void showMessage(const char* caption, const char* message) {
    MessageBoxA(
        g_windowHandle,  // ✅ HWND, pas HDC
        message,
        caption,
        MB_ICONWARNING   // 0x30 = MB_ICONWARNING, mais écris-le en clair
    );
}


void drawRectangleFromGrid(int row, int col) {
    // Marque la cellule dans la grille comme sélectionnée
    g_state.bottomEntityMap[row][col] = 0xFFFF;

    // Sélectionne l'objet graphique (stylo/pinceau ?) à utiliser pour dessiner
    HGDIOBJ oldPen = SelectObject(g_deviceContext, gridPen);

    // Calcule les coordonnées en pixels de la cellule à dessiner
    int top    = gridOriginY + row * cellHeight;
    int left   = gridOriginX + col * cellWidth;
    int bottom = top + cellHeight;
    int right  = left + cellWidth;

    // Dessine le rectangle à la position donnée
    Rectangle(g_deviceContext, left, top, right, bottom);

    // (Optionnel) On pourrait re-sélectionner l'ancien objet si besoin
    // SelectObject(g_windowHandle, oldPen);
}

void cancelPendingInteraction() {
    if (mouseCaptureFlag != 0) {
        RELEASECAPTURE();
        mouseCaptureFlag = 0;
    }

    if (someGlobalCondition == 0) {
        hasPendingDialogBox = 0;
        matchingEntryCount = 0;
        maybeDrawPendingRectangle();
    }
}

void updateNextLevelMenuItem() {
    HMENU menu = GetMenu(g_deviceContext);
    if (totalLevelCount == 1) {
        EnableMenuItem(menu, MENU_ITEM_ID_NEXT_LEVEL, MF_GRAYED);
    } else {
        EnableMenuItem(menu, MENU_ITEM_ID_NEXT_LEVEL, MF_ENABLED);
    }
}

void initializeWindowSize() {
    char tempBuffer[130] = {};  // var_82 (0x82 bytes)

    // Étape 1 : copier les 2 octets de str_0444 dans le buffer
    std::memcpy(tempBuffer, str_0444, 2);

    // Étape 2 : vérifier si la chaîne à l’adresse 0x1A0h est non vide
    const char* baseStr = reinterpret_cast<const char*>(0x1A0);
    if (std::strlen(baseStr) > 0) {
        // Étape 3 : concaténer les 5 premiers caractères de str_0448 à la suite de tempBuffer
        std::strncat(tempBuffer, str_0448, 5);

        // Étape 4 : copier baseStr dans tempBuffer (à la suite de ce qui a déjà été écrit)
        std::strcat(tempBuffer, baseStr);

        // Étape 5 : ajouter 2 caractères supplémentaires depuis str_044D
        std::strncat(tempBuffer, str_044D, 2);
    }

    // Étape 6 : condition facultative (ajout de 9 octets)
    if (someGlobalCondition == 1) {
        std::strncat(tempBuffer, str_044F, 9);
    }

    // Étape finale : appeler SetWindowText avec le buffer final
    SetWindowText(WindowFromDC(g_deviceContext), tempBuffer);
}

void loadGraphicsResources() {
    // Création des brosses de couleur unie
    brush_black = CreateSolidBrush(RGB(0x00, 0x00, 0x00));
    brush_white = CreateSolidBrush(RGB(0xFF, 0xFF, 0xFF));
    brush_blue  = CreateSolidBrush(RGB(0x00, 0x00, 0xFF));
    brush_green = CreateSolidBrush(RGB(0x00, 0xFF, 0x00));
    brush_red   = CreateSolidBrush(RGB(0xFF, 0x00, 0x00));

    // Création de stylos (ligne 1 pixel)
    pen_black = CreatePen(PS_SOLID, 1, RGB(0x00, 0x00, 0x00));
    pen_gray  = CreatePen(PS_SOLID, 1, RGB(0xFF, 0xFF, 0xFF));  // probablement utilisé pour la grille
    pen_blue  = CreatePen(PS_SOLID, 1, RGB(0x00, 0x00, 0xFF));

    // Chargement des bitmaps : "kye", "block", "wall"
    bitmap_kye   = LoadBitmap(hInstanceTmp, kye);
    bitmap_block = LoadBitmap(hInstanceTmp, aBlock);
    bitmap_wall  = LoadBitmap(hInstanceTmp, aWall);
}

void stopGameTimer() {
    if (g_timerId != 0) {
        KILLTIMER(g_deviceContext, 0);  // Stoppe le timer ID 0 pour ce contexte
    }
    g_timerActive = 0;
}

int initializeGameWindow(int cmdShow, uint16_t titleSegment, uint16_t titleOffset, bool skipRegisterClassFlag, HINSTANCE hInstance) {
    char windowTitleBuffer[96] = {0};

    // 1. Copier le titre de la fenêtre depuis segment:offset (émulation mémoire DOS)
    const char* title = getPointerFromSegmentOffset(titleSegment, titleOffset);
    size_t i = 0;
    while (title[i] != '\0' && i < sizeof(windowTitleBuffer) - 1) {
        windowTitleBuffer[i] = title[i];
        ++i;
    }
    windowTitleBuffer[i] = '\0';

    if (!skipRegisterClassFlag) {
        WNDCLASSA wc = {};
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = WNDPROC;
        wc.hInstance = hInstance;
        wc.hIcon = LoadIconA(hInstance, "IDI_KYE");
        wc.hCursor = LoadCursor(nullptr, (LPCSTR)IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(WHITE_BRUSH);
        wc.lpszClassName = "KyeWindowClass";

        if (!RegisterClassA(&wc)) {
            return 0;
        }
    }

    HWND hWnd = CreateWindowA(
        "KyeWindowClass",
        "Kye",
        WS_OVERLAPPEDWINDOW,
        50, 50, 450, 480,
        nullptr,
        nullptr,
        hInstance,
        nullptr
    );

    if (!hWnd) return 0;

    // Mémorisation globale
    g_hInstance = hInstance;
    g_hWnd = hWnd;
    isWindowsResourceAllocated = false;
    someGlobalCondition = false;
    mouseCaptureFlag = false;

    word_B044 = LoadCursor(nullptr, (LPCSTR)IDC_ARROW);
    word_B042 = LoadCursor(nullptr, (LPCSTR)IDC_CROSS);

    initializeWindowSize();
    ShowWindow(hWnd, cmdShow);
    loadGraphicsResources();
    initializeWindowHandleIfNeeded();
    initializeLayoutRects();
    releaseDialogResources();

    int initStatus = sub_639C(0);
    seedGameRNG(initStatus);
    startPollingTimer();
    OPENSOUND();
    word_85CE = 0;
    resetLevelStateMemory();

    if (!isDiggerKeyword(windowTitleBuffer)) {
        memcpy((void*)0x01A0, (void*)0x0186, 10);  // simulate rep movsw
        loadLevelByIndex(currentLevelIndex);
        updateDisplayString(0x29FA);
        updateNextLevelMenuItem();
        matchingEntryCount = 0;
        InvalidateRect(hWnd, nullptr, TRUE);
        UpdateWindow(hWnd);
        showWhatDialog();
    }

    memcpy((void*)0x01A0, (void*)0x0191, 12);  // simulate rep movsw
    loadLevelByIndex(currentLevelIndex);
    updateDisplayString(0x29FA);
    updateNextLevelMenuItem();
    matchingEntryCount = 0;
    InvalidateRect(hWnd, nullptr, TRUE);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    stopGameTimer();
    CLOSESOUND();

    return 0; // TODO: return actual result code if needed
}

void initializeLayoutRects(HWND hWnd) {
    RECT windowRect = {};
    RECT clientRect = {};
    RECT rect1 = {};
    RECT rect2 = {};
    RECT rect3 = {};

    // 1. Récupérer les dimensions de la fenêtre et client
    GetWindowRect(hWnd, &windowRect);
    GetClientRect(hWnd, &clientRect);

    // 2. Calcul du point d’origine Y de la grille
    gridOriginY = clientRect.top;

    // 3. Premier rectangle : grid zone ?
    SetRect(
        &rect1,
        clientRect.left,
        clientRect.top,
        clientRect.left + 0x1E0, // +480
        clientRect.top + 0x140   // +320
    );

    // 4. Deuxième rectangle : baseX ?
    SetRect(
        &rect2,
        clientRect.left,
        baselineY + 1,
        clientRect.left + 0x12C, // +300
        baselineY + 0x10 + 1     // +16 + 1
    );
    baseX = rect2.left;

    // 5. Troisième rectangle : bouton / barre ?
    SetRect(
        &rect3,
        leftEdge3,
        baselineY + 1,
        rightEdge3 + 2,
        baselineY + 0x10 + 1
    );
}

TimeStamp computeTimestampFromNow(TimeStamp* outPtr = nullptr) {
    uint16_t rawDate = 0;
    uint16_t rawTime = 0;

    // 1. Obtenir date et heure
    getCurrentDate(&rawDate);
    getCurrentTime(&rawTime);

    // 2. Calculer l'heure ajustée
    uint16_t adjustedLow = 0;
    uint16_t adjustedHigh = 0;
    computeAdjustedTime(rawDate, rawTime, &adjustedLow, &adjustedHigh);

    // 3. Si outPtr fourni, stocker dedans
    if (outPtr != nullptr) {
        outPtr->adjustedLow = adjustedLow;
        outPtr->adjustedHigh = adjustedHigh;
    }

    // 4. Retourner les valeurs sous forme de struct
    return TimeStamp{ adjustedLow, adjustedHigh };
}

void renderEntityToWindow(int entityIndex) {
    const auto& e = g_gameState.entities[entityIndex];

    int spriteX = 0;
    int spriteY = 0;

    if (e.actionCode < 0x17) {
        spriteX = e.actionCode * 16;
        spriteY = 0;
    } else if (e.actionCode >= 0x32 && e.actionCode <= 0x3B) {
        spriteX = e.actionCode * 16;
        spriteY = 0x0F00; // = 0xFCE0 − 0xE0
    } else if (e.actionCode >= 0x0F && e.actionCode <= 0x13) {
        spriteX = e.actionCode * 16;
        spriteY = e.subtype * 16;
    } else {
        spriteX = e.actionCode * 16;
        spriteY = 0;
    }

    SDL_Rect srcRect = { spriteX, spriteY, 16, 16 };
    SDL_Rect dstRect = { e.col * cellWidth, e.row * cellHeight, 16, 16 };

    SDL_RenderCopy(gRenderer, spriteSheet, &srcRect, &dstRect);
}
