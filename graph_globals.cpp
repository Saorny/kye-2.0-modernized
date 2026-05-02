#include "graph.h"

const char *borderTitle = "border.kye";
const char *defaultKyeTitle = "default.kye";

int16_t baselineY = 0;
int16_t leftEdge3 = 300;
int16_t rightEdge3 = 480;

SDL_Window*   g_window   = nullptr;
SDL_Renderer* g_renderer = nullptr;

int16_t gridOriginX = 0;
int16_t gridOriginY = 0;

bool g_isRendering = false;

SDL_Rect g_windowRectPx{};
SDL_Rect g_clientRectPx{};
SDL_Rect g_gridRectPx{};
SDL_Rect g_baseRectPx{};
SDL_Rect g_invalidateRectPx{};

int uiTopX = 0;
int uiTopY = 0;
int uiBottomLineX = 0;
int uiBottomLineY = 0;
int uiLeftX = 0;
int uiLeftY = 0;
int uiBottomX = 0;
int uiBottomY = 0;

SpriteSheet g_sheetKye{};
SpriteSheet g_sheetMobiles{};
SpriteSheet g_sheetStatics{};

TTF_Font* g_font = nullptr;
SDL_Window* g_secondaryWindow = nullptr;
SDL_Texture* bitmap_wall = nullptr;
SDL_Texture* g_kyeTexture = nullptr;

bool g_needsRedraw = false;
char stringBuffer[512]{};

int MENU_BAR_HEIGHT = 24;
int g_oneWayAnimPhase = 1;
