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
HBITMAP g_blockBitmap;
#include "graph.h"
#include "dialog.h"

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