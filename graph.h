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

struct TimeStamp {
    uint16_t adjustedLow;
    uint16_t adjustedHigh;
};


// Rectangle principal global utilisé dans le jeu (ex: zone d'affichage)
extern Rect g_mainRect;

int isPendingDraw = 0;
int pendingRow = 0;
int pendingCol = 0;
int colOffsetBytes = 0;
int cellHeight = 16;
int cellWidth = 16;
int gridOriginX = 0;
int gridOriginY = 0;

extern RECT invalidatedRect;
extern char stringBuffer[512];
HDC g_deviceContext = nullptr;
HGDIOBJ gridPen = nullptr;
extern const char str_0444[];  // 2 octets à copier
extern const char str_0448[];  // chaîne 1
extern const char str_044D[];  // chaîne 2
extern const char str_044F[];  // chaîne facultative (9 octets)

void updateDisplayString(const char* str);
void showMessage(const char* caption, const char* message);
int isPointInRect(int x, int y);
void maybeDrawPendingRectangle();
void drawPendingBlock();
void drawRectangleFromGrid(int row, int col);
void renderEntityToWindow(int entityIndex);

HBRUSH brush_black;       // word_AE74
HBRUSH brush_white;       // word_AE72
HBRUSH brush_blue;        // word_AE70
HBRUSH brush_green;       // word_AE6E
HBRUSH brush_red;         // word_AE6C

HPEN pen_black;           // word_AE68
HPEN pen_gray;            // gridPen
HPEN pen_blue;            // word_AE64

HBITMAP bitmap_kye;       // memoryDC
HBITMAP bitmap_block;     // word_AE60
HBITMAP bitmap_wall;      // word_AE5E

extern HINSTANCE hInstanceTmp; // Instance de l'application
extern const char kye[];       // Nom de la ressource bitmap "kye"
extern const char aBlock[];    // "block"
extern const char aWall[];     // "wall"

#endif // GRAPH_H
