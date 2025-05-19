#ifndef GRAPH_H
#define GRAPH_H

#include <cstdint>
#include <windows.h>

// Représente un rectangle (comme dans les vieilles API graphiques)
struct Rect {
    int16_t left;
    int16_t top;
    int16_t right;
    int16_t bottom;
};

// Rectangle principal global utilisé dans le jeu (ex: zone d'affichage)
extern Rect g_mainRect;

int isPendingDraw = 0;
int pendingRow = 0;
int pendingCol = 0;
int colOffsetBytes = 0;
int cellHeight = 16;
int cellWidth = 16;

#endif // GRAPH_H
