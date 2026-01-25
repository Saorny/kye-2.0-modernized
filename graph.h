#ifndef GRAPH_H
#define GRAPH_H

#include <cstdint>
#include <windows.h>
#include <SDL3/SDL.h>

extern HINSTANCE g_hInstance;
extern HWND      g_mainWindow;

static SDL_Window*   g_window   = nullptr;
static SDL_Renderer* g_renderer = nullptr;

int pendingRow = 0;
int pendingCol = 0;

INT_PTR CALLBACK DLG_INPNAM_FUNC(
    HWND   hDlg,
    UINT   msg,
    WPARAM wParam,
    LPARAM lParam
);


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

struct GridCellRect {
    i16 x0, y0, x1, y1; // pixels
};

// Rectangle principal global utilisé dans le jeu (ex: zone d'affichage)
extern Rect g_mainRect;


extern RECT invalidatedRect;
extern char stringBuffer[512];
extern const char str_0444[];  // 2 octets à copier
extern const char str_0448[];  // chaîne 1
extern const char str_044D[];  // chaîne 2
extern const char str_044F[];  // chaîne facultative (9 octets)

static bool g_isRendering = false;

void showMessage(const char* caption, const char* message);
int isPointInRect(int x, int y);
void maybeDrawPendingRectangle();
void drawPendingBlock();
void drawRectangleFromGrid(int row, int col);
void renderEntityToWindow(int entityIndex);
void initializeWindowSize();

extern SDL_Renderer* gRenderer;

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

extern HINSTANCE g_hInstance; // Instance de l'application
extern const char kye[];       // Nom de la ressource bitmap "kye"
extern const char aBlock[];    // "block"
extern const char aWall[];     // "wall"

extern char stringBuffer[];
extern uint16_t stringBufferCapacity;
extern bool g_needsRedraw;

const char borderTitle[255];
const char defaultKyeTitle[255];

void drawRectangle(i16 left, i16 top, i16 right, i16 bottom);
void drawTextAt(i16 x, i16 y, const char* text, int length);
int showNameInputDialog();
void renderEntityToSdl(int16_t entityIndex);
static GridCellRect computeHudCellRectFromIndex(i16 cellIndex);
static void drawFrame(SDL_Renderer* renderer, const SDL_Rect& r, bool selected);
static void cleanupAndExit(int exitCode);
static void initTickCounter();
static int initializeGameWindow(
    int cmdShow,
    uint16_t titleSegment,
    uint16_t titleOffset,
    int skipRegisterClassFlag,
    void* hInstance
);
void drawWhatDialogUI();

#endif // GRAPH_H
