#ifndef GRAPH_H
#define GRAPH_H

#include <cstdint>
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

using VoidCallback = void (*)();

extern SDL_Window*   g_window;
extern SDL_Renderer* g_renderer;

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
    std::int16_t x0, y0, x1, y1; // pixels
};

extern Rect g_mainRect;


// extern RECT invalidatedRect;
extern char stringBuffer[512];
extern const char str_0444[];  // 2 octets à copier
extern const char str_0448[];  // chaîne 1
extern const char str_044D[];  // chaîne 2
extern const char str_044F[];  // chaîne facultative (9 octets)

extern bool g_isRendering;

void showMessage(const char* caption, const char* message);
int isPointInRect(int x, int y);
void maybeDrawPendingRectangle();
void drawPendingBlock();
void drawRectangleFromGrid(int row, int col);
void initializeWindowSize();

// extern HBRUSH brush_black;       // word_AE74
// extern HBRUSH brush_white;       // word_AE72
// extern HBRUSH brush_blue;        // word_AE70
// extern HBRUSH brush_green;       // word_AE6E
// extern HBRUSH brush_red;         // word_AE6C

// extern HPEN pen_black;           // word_AE68
// extern HPEN pen_gray;            // gridPen
// extern HPEN pen_blue;            // word_AE64

// extern BitmapHandle bitmap_kye;       // memoryDC
// extern BitmapHandle bitmap_block;     // word_AE60
// extern BitmapHandle bitmap_wall;      // word_AE5E

extern const char kye[];       // Nom de la ressource bitmap "kye"
extern const char aBlock[];    // "block"
extern const char aWall[];     // "wall"

extern uint16_t stringBufferCapacity;
extern bool g_needsRedraw;

extern const char* borderTitle;
extern const char* defaultKyeTitle;

int showNameInputDialog();
void renderEntityToSdl(int16_t entityIndex);
GridCellRect computeHudCellRectFromIndex(std::int16_t cellIndex);
void drawFrame(SDL_Renderer* renderer, const SDL_FRect& r, bool selected);
void cleanupAndExit(int exitCode);
void initTickCounter();
bool loadSpriteSheets();
int initializeGameWindow(
    int cmdShow,
    uint16_t titleSegment,
    uint16_t titleOffset,
    int skipRegisterClassFlag,
    void* hInstance
);
void drawWhatDialogUI();
bool initializeRendererIfNeeded();

struct SpriteSheet {
    SDL_Texture* tex = nullptr;
    int w = 0;
    int h = 0;
    int tileW = 16;
    int tileH = 16;
};

extern TTF_Font *g_sheetFont;
// extern SpriteSheet g_sheetFont;
extern SpriteSheet g_sheetKye;   // graph_kye.bmp
extern SpriteSheet g_sheetMobiles;  // graph_mobiles.bmp
extern SpriteSheet g_sheetStatics;   // graph_statiscs.bmp

extern int16_t baselineY;
extern int16_t leftEdge3;
extern int16_t rightEdge3;

extern bool isPendingDraw;

struct RectI16 {
  std::int16_t left;
  std::int16_t top;
  std::int16_t right;
  std::int16_t bottom;
};

extern std::int16_t gridOriginX;
extern std::int16_t gridOriginY;

extern SDL_Rect g_windowRectPx;
extern SDL_Rect g_clientRectPx;
extern SDL_Rect g_gridRectPx;
extern SDL_Rect g_baseRectPx;
extern SDL_Rect g_invalidateRectPx;

extern int uiTopX;
extern int uiTopY;
extern int uiBottomX;
extern int uiBottomY;
extern int uiBottomLineX;
extern int uiBottomLineY;

extern int uiLeftX;
extern int uiRightY;
extern int uiRightX;
extern char hudMessageText[256];
extern TTF_Font* g_font;

int showFileMessage(const char* message);
void runTileSparkleEffect(int16_t effectId);
void renderHudAndFrame();
void initializeLayoutRects();
void drawText(int x, int y, const char* text, int len);
void showWhatDialog();

extern SDL_Window* g_secondaryWindow;
extern SDL_Texture* bitmap_wall;
extern SDL_Texture* g_kyeTexture;
void releaseDialogResources(SDL_Texture* dialogTexture = nullptr);
SpriteSheet loadBmpSheet(SDL_Renderer* r, const char* path, int tileW, int tileH);
const char* getPointerFromSegmentOffset(uint16_t seg, uint16_t off);
void showDialog(const char* dialogId, VoidCallback onOk);
void DLG_OK_FUNC();
void drawTextAt(int16_t x, int16_t y, const char* text, int length);
void showHelp();
void showAboutDialog();

#endif // GRAPH_H
