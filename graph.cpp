#include <iostream>
#include <cstdint>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <vector>
#include <filesystem>
#include <string>
#include <system_error>
#include <cstddef>
#include <tuple>
#include <cstring>
#include "graph.h"
#include "game.h"
#include "util.h"
#include "system.h"

namespace fs = std::filesystem;

struct FileEntry {
    std::string name;
    fs::path fullPath;
    bool isDir = false;
};

bool isPendingDraw = false;
uint16_t g_biosTickCountLo = 0;
uint16_t g_biosTickCountHi = 0;

void maybeDrawPendingRectangle()
{
    if (isPendingDraw)
    {
        if (g_gameState.tileMap[pendingRow][pendingCol] == EntityType::EMPTY_CELL)
        {
            drawRectangleFromGrid(pendingRow, pendingCol);
        }
    }

    isPendingDraw = false;
}

bool loadSpriteSheets()
{
    if (!g_renderer)
        return false;

    g_sheetKye      = loadBmpSheet(g_renderer, "graph/graph_kye.bmp",      16, 16);
    g_sheetMobiles  = loadBmpSheet(g_renderer, "graph/graph_mobiles.bmp",  16, 16);
    g_sheetStatics  = loadBmpSheet(g_renderer, "graph/graph_statics.bmp",  16, 16);
    // g_sheetFont     = loadBmpSheet(g_renderer, "graph/font.bmp",           8, 16);
    // TTF_Font* font = TTF_OpenFont("graph/font.otf", 16);
    g_font = TTF_OpenFont("graph/font3.ttf", 16);
    // g_font = TTF_OpenFont("C:/Windows/Fonts/arial.ttf", 16);
    cout << "g_font => " << g_font << endl;
    std::cout << "Statics sheet size = "
          << g_sheetStatics.w << " x "
          << g_sheetStatics.h << std::endl;
    SDL_SetTextureScaleMode(g_sheetStatics.tex, SDL_SCALEMODE_NEAREST);
    SDL_SetTextureBlendMode(g_sheetStatics.tex, SDL_BLENDMODE_BLEND);
    return true;
}

void drawPendingBlock()
{
    const int16_t destY = pendingRow * cellHeight;
    const int16_t destX = pendingCol * cellWidth;

    constexpr int16_t tileSize = 16;

    // Source = (16, 0)
    SDL_FRect srcRect{
        16.0f,
        0.0f,
        static_cast<float>(tileSize),
        static_cast<float>(tileSize)
    };

    SDL_FRect dstRect{
        static_cast<float>(destX),
        static_cast<float>(destY),
        static_cast<float>(tileSize),
        static_cast<float>(tileSize)
    };

    SDL_RenderTexture(
        g_renderer,
        g_sheetKye.tex,
        &srcRect,
        &dstRect
    );
}

int isPointInRect(int x, int y) {
    return (x >= g_mainRect.left && x < g_mainRect.right &&
            y >= g_mainRect.top  && y < g_mainRect.bottom);
}

void showMessage(const char* caption, const char* message)
{
    if (!g_window)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "showMessage called without window: %s - %s",
                    caption, message);
        return;
    }

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_WARNING,
        caption,
        message,
        g_window
    );
}

void drawRectangleFromGrid(int row, int col)
{
    g_gameState.tileMap[row][col] = EntityType::EMPTY_CELL;

    const int x0 = gridOriginX + col * cellWidth;
    const int y0 = gridOriginY + row * cellHeight;

    SDL_FRect r{
        (float)x0,
        (float)y0,
        (float)cellWidth,
        (float)cellHeight
    };

    SDL_SetRenderDrawColor(g_renderer, 255,255,255,255); // blanc
    SDL_RenderFillRect(g_renderer, &r);
}

static const char* getLegacyString(uint16_t offset) {
    return getPointerFromSegmentOffset(0, offset);
}

static void appendBytes(char* dst, size_t dstCap, const void* src, size_t n) {
    if (!dst || dstCap == 0 || !src || n == 0) return;
    size_t len = std::strlen(dst);
    size_t room = (len < dstCap) ? (dstCap - 1 - len) : 0;
    size_t take = (n < room) ? n : room;
    if (take == 0) return;
    std::memcpy(dst + len, src, take);
    dst[len + take] = '\0';
}

static void appendCString(char* dst, size_t dstCap, const char* src) {
    if (!dst || dstCap == 0 || !src) return;
    size_t len = std::strlen(dst);
    size_t room = (len < dstCap) ? (dstCap - 1 - len) : 0;
    if (room == 0) return;
    size_t take = std::strlen(src);
    if (take > room) take = room;
    std::memcpy(dst + len, src, take);
    dst[len + take] = '\0';
}

void initializeWindowSize() {
    char windowTitle[130] = {0};

    const char* titlePrefix = getLegacyString(0x0444);
    appendBytes(windowTitle, sizeof(windowTitle), titlePrefix, 4);
    const char* base = "Kye (Modern)";
    // const char* base = getLegacyString(g_selectedFilePath);
    if (base && base[0] != '\0') {
        const char* titleMid5 = getLegacyString(0x0448);
        appendBytes(windowTitle, sizeof(windowTitle), titleMid5, 5);

        appendCString(windowTitle, sizeof(windowTitle), base);

        const char* titleSuffix = getLegacyString(0x044D);
        appendBytes(windowTitle, sizeof(windowTitle), titleSuffix, 4);
    }

    if (g_interactionMode == GameInteractionMode::PendingBlock) {
        const char* modeSuffix = getLegacyString(0x044F);
        appendBytes(windowTitle, sizeof(windowTitle), modeSuffix, 18);
    }

    if (g_window) {
        SDL_SetWindowTitle(g_window, windowTitle);
    }
}

const char* getPointerFromSegmentOffset(uint16_t seg, uint16_t off)
{
    (void)seg;
    (void)off;
    return "Kye (Modern)";
}

bool initializeRendererIfNeeded()
{
    if (g_renderer != nullptr)
        return true;

    g_renderer = SDL_CreateRenderer(
        g_window,
        nullptr
    );

    if (!g_renderer)
        return false;
    
    if (TTF_Init() == -1)
    {
        std::cout << "TTF_Init error: " << SDL_GetError() << std::endl;
    }
    return true;
}

int initializeGameWindow(
    int cmdShow,
    uint16_t titleSegment,
    uint16_t titleOffset,
    int skipRegisterClassFlag,
    void* hInstance
) {
    (void)cmdShow;
    (void)skipRegisterClassFlag;
    (void)hInstance;

    char windowTitle[96] = {0};
    const char* title = getPointerFromSegmentOffset(titleSegment, titleOffset);

    if (title) {
        std::strncpy(windowTitle, title, sizeof(windowTitle) - 1);
    }

    if (!initializeRendererIfNeeded())
        return 0;
    if (!loadSpriteSheets())
        return 0;

    initializeWindowSize();
    initializeLayoutRects();

    advanceToNextLevelOrBlock();

    const int seed = computeTimestampNow(nullptr);
    seedGameRNG(seed);

    startPollingTimer();
    frameCounter = 0;
    resetLevelStateMemory();

    copyMemory(g_selectedFilePath, defaultKyeTitle, 12);
    loadLevelByIndex(g_levelIndex);
    clearStatusLine(g_statusLineBuffer);
    updateNextLevelMenuItem();
    matchedEntryCount = 0;

    if (!isDiggerKeyword(windowTitle)) {
        showWhatDialog();
    }
    std::cout << "Statics texture = " << g_sheetStatics.tex << std::endl;
    return 1;
}

void showWhatDialog()
{
    showDialog("DLG_WHAT", DLG_OK_FUNC);
}

void DLG_OK_FUNC()
{
    handleDialogClose(NewLevelDialogResult::Accepted);
}

void showDialog(const char* dialogId, VoidCallback onOk)
{
    g_newLevelDialogOpen = true;

    // Layout simple centré
    g_panel = { 200.f, 150.f, 240.f, 140.f };

    g_okBtn = {
        g_panel.x + 30.f,
        g_panel.y + g_panel.h - 40.f,
        70.f,
        30.f
    };

    g_cancelBtn = {
        g_panel.x + g_panel.w - 100.f,
        g_panel.y + g_panel.h - 40.f,
        70.f,
        30.f
    };

    reinterpret_cast<uintptr_t>(onOk);
}

static SDL_Rect makeRectLTRB(int l, int t, int r, int b) {
    SDL_Rect out{};
    out.x = l;
    out.y = t;
    out.w = r - l;
    out.h = b - t;
    return out;
}

void initializeLayoutRects() {
    if (!g_window) return;

    // SDL3: préfère "InPixels" si tu veux coller au rendu réel (DPI)
    int winW = 0, winH = 0;
    SDL_GetWindowSizeInPixels(g_window, &winW, &winH);

    // En Win32, clientRect.left/top = 0 en coords client.
    // Donc l’équivalent simple en SDL : client = (0,0, winW, winH)
    g_clientRectPx = SDL_Rect{ 0, 0, winW, winH };

    // (Optionnel) windowRect : SDL ne te donne pas un "screen rect" LTRB comme Win32,
    // mais tu peux stocker la même chose que client pour tes calculs.
    g_windowRectPx = g_clientRectPx;

    // si = 0x10 dans l'asm
    constexpr int kBandHeight = 0x10; // 16

    // A) gridOriginY : 480x320 à partir du client origin
    g_gridRectPx = makeRectLTRB(
        g_clientRectPx.x,
        g_clientRectPx.y,
        g_clientRectPx.x + 0x1E0, // 480
        g_clientRectPx.y + 0x140  // 320
    );

    // B) baseX : bande UI gauche
    g_baseRectPx = makeRectLTRB(
        g_clientRectPx.x,
        baselineY + 1,
        g_clientRectPx.x + 0x12C, // 300
        baselineY + kBandHeight + 1
    );

    // C) invalidatedRect : bande UI droite
    g_invalidateRectPx = makeRectLTRB(
        leftEdge3,
        baselineY + 1,
        rightEdge3 + 2,
        baselineY + kBandHeight + 1
    );
}

void renderEntity(int entityIndex)
{
    if (entityIndex < 0 ||
        entityIndex >= static_cast<int16_t>(g_gameState.entities.size()) ||
        g_sheetMobiles.tex == nullptr)
    {
        return;
    }

    const EntityInfo& entity =
        g_gameState.entities[entityIndex];

    const int type = static_cast<int>(entity.entityType);

    SDL_FRect dstRect{
        static_cast<float>(gridOriginX + entity.col * cellWidth),
        static_cast<float>(gridOriginY + entity.row * cellHeight),
        static_cast<float>(cellWidth),
        static_cast<float>(cellHeight)
    };

    float srcX = 0;
    float srcY = 0;

    if (type >= 0x0F && type <= 0x13)
    {
        srcX = static_cast<float>(type * 0x10);
        // srcY = static_cast<float>(type * 16);
         srcY = 16.0f;
    }
    else if (type >= 0x32 && type <= 0x3B)
    {
        srcX = static_cast<float>(type * 0x10);
        srcY = 16.0f;
    }
    else
    {
        srcX = static_cast<float>(type * 0x10);
        srcY = 0.0f;
    }

    SDL_FRect srcRect{
        srcX,
        srcY,
        16.0f,
        16.0f
    };

    SDL_RenderTexture(g_renderer, g_sheetMobiles.tex, &srcRect, &dstRect);
}

int showNameInputDialog()
{
    if (!g_window || !g_renderer)
        return 0;

    g_newLevelDialogOpen = true;
    g_newLevelDialogResult = NewLevelDialogResult::None;
    g_levelInput.clear();

    SDL_StartTextInput(g_window);

    auto pointInFRect = [](float x, float y, const SDL_FRect& r) -> bool {
        return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
    };

    while (g_newLevelDialogResult == NewLevelDialogResult::None) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                g_newLevelDialogResult = NewLevelDialogResult::Cancelled;
                break;
            }

            if (e.type == SDL_EVENT_KEY_DOWN) {
                const SDL_Keycode key = e.key.key;

                if (key == SDLK_ESCAPE) {
                    g_newLevelDialogResult = NewLevelDialogResult::Cancelled;
                } else if (key == SDLK_RETURN || key == SDLK_KP_ENTER) {
                    g_newLevelDialogResult = NewLevelDialogResult::Accepted;
                } else if (key == SDLK_BACKSPACE) {
                    if (!g_levelInput.empty())
                        g_levelInput.pop_back();
                }
            }

            if (e.type == SDL_EVENT_TEXT_INPUT) {
                if (g_levelInput.size() < 120) {
                    g_levelInput += e.text.text; // UTF-8
                }
            }

            if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
                const float mx = static_cast<float>(e.button.x);
                const float my = static_cast<float>(e.button.y);

                if (pointInFRect(mx, my, g_okBtn)) {
                    g_newLevelDialogResult = NewLevelDialogResult::Accepted;
                } else if (pointInFRect(mx, my, g_cancelBtn)) {
                    g_newLevelDialogResult = NewLevelDialogResult::Cancelled;
                }
            }
        }
        drawWhatDialogUI();
        SDL_RenderPresent(g_renderer);
        SDL_Delay(1);
    }

    SDL_StopTextInput(g_window);
    g_newLevelDialogOpen = false;

    return (g_newLevelDialogResult == NewLevelDialogResult::Accepted) ? 1 : 0;
}

GridCellRect computeHudCellRectFromIndex(std::int16_t cellIndex)
{
    const std::int16_t gridRow = static_cast<std::int16_t>(cellIndex / 0x10);
    const std::int16_t gridCol = static_cast<std::int16_t>(cellIndex % 0x10);

    const std::int16_t cellBaseX = static_cast<std::int16_t>(gridRow * 0x14);
    const std::int16_t cellBaseY = static_cast<std::int16_t>(gridCol * 0x14);

    return GridCellRect{
        static_cast<std::int16_t>(cellBaseX + 2),
        static_cast<std::int16_t>(cellBaseY + 2),
        static_cast<std::int16_t>(cellBaseX + 0x12),
        static_cast<std::int16_t>(cellBaseY + 0x12),
    };
}

void renderFullWallLayer()
{
    if (!g_renderer || !g_sheetStatics.tex)
        return;

    for (int row = 0; row < GRID_ROWS; ++row)
    {
        for (int col = 0; col < GRID_COLS; ++col)
        {
            const EntityType tileValue = g_gameState.tileMap[row][col];

            if (tileValue == EntityType::EMPTY_CELL)
            {
                SDL_FRect dst{
                    float(gridOriginX + col * cellWidth),
                    float(gridOriginY + row * cellHeight),
                    float(cellWidth),
                    float(cellHeight)
                };

                SDL_SetRenderDrawColor(g_renderer,255,255,255,255);
                SDL_RenderFillRect(g_renderer,&dst);
                continue;
            }

            if (tileValue >= EntityType::ONE_WAY_TOP_TO_BOTTOM)
            {
                renderStaticObjects(row, col, tileValue);
            }
        }
    }
}

void renderAllEntities()
{
    for (int i = 0; i < g_activeSpawnerCount ; ++i)
    {
        renderEntity(i);
    }
}

void renderAllObjects()
{
    if (g_interactionMode == GameInteractionMode::NormalPlay)
    {
        renderFullWallLayer();
        renderAllEntities();

        if (g_levelJustLoadedFlag)
        {
            g_levelJustLoadedFlag = 0;
            runTileSparkleEffect(1);
        }
        else
        {
            runTileSparkleEffect(0);
        }
        return;
    }

    if (g_interactionMode == GameInteractionMode::PendingBlock)
    {
        renderFullWallLayer();
        renderAllEntities();
        runTileSparkleEffect(0);
        return;
    }

    return;
}

int renderHudAndFrame()
{
    renderAllObjects();

    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);

    SDL_RenderLine(
        g_renderer,
        (float)baseX,
        (float)(uiTopY - 1),
        (float)uiRightX,
        (float)(uiTopY - 1)
    );

    SDL_RenderLine(
        g_renderer,
        (float)(rightEdge3 + 1),
        (float)baseY,
        (float)(rightEdge3 + 1),
        (float)(uiBottomLineY + 2)
    );

    renderLivesAndLevelInfo();
    // drawTextAt(
    //     (int16_t)(g_invalidateRectPx.x + 4),
    //     (int16_t)uiTopY,
    //     hudMessageText,
    //     (int)strlen(hudMessageText)
    // );
    return 1;
}

static inline void setDrawColorPenBlack() { SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255); }
static inline void setDrawColorPenGray()  { SDL_SetRenderDrawColor(g_renderer, 128, 128, 128, 255); }

// void renderHudAndFrame()
// {
//     Uint8 oldR, oldG, oldB, oldA;
//     SDL_GetRenderDrawColor(g_renderer, &oldR, &oldG, &oldB, &oldA);
//     SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
//     SDL_RenderLine(
//         g_renderer,
//         (float)baseX, (float)(uiTopY - 1),
//         (float)uiRightX, (float)(uiTopY - 1)
//     );
//     SDL_RenderLine(
//         g_renderer,
//         (float)(rightEdge3 + 1), (float)baseY,
//         (float)(rightEdge3 + 1), (float)(uiBottomLineY + 2)
//     );
//     renderLivesAndLevelInfo();
//     SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
//     SDL_FRect hudRect{
//         (float)uiLeftX,
//         (float)uiTopY,
//         (float)(uiRightX - uiLeftX),
//         (float)(uiBottomY - uiTopY)
//     };
//     SDL_RenderRect(g_renderer, &hudRect);
//     SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
//     const char* hudText = hudMessageText;
//     const int hudLen = (int)SDL_strlen(hudText);
//     drawTextAt((int16_t)(uiLeftX + 4), (int16_t)uiTopY, hudText, hudLen);
//     SDL_SetRenderDrawColor(g_renderer, oldR, oldG, oldB, oldA);
//     renderAllObjects();
// }

// void drawTextAt(int16_t x, int16_t y, const char* text, int length)
// {
//     if (!g_renderer || !g_font || !text || length <= 0)
//         return;

//     std::string str(text, length);

//     SDL_Color color{0,0,0,255};

//     SDL_Surface* surface = TTF_RenderText_Blended(
//         g_font,
//         str.c_str(),
//         str.length(),
//         color
//     );
//     if (!surface)
//         return;

//     SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surface);
//     SDL_DestroySurface(surface);

//     if (!tex)
//         return;

//     SDL_FRect dst{
//         static_cast<float>(x),
//         static_cast<float>(y),
//         static_cast<float>(surface->w),
//         static_cast<float>(surface->h)
//     };

//     SDL_RenderTexture(g_renderer, tex, nullptr, &dst);

//     SDL_DestroyTexture(tex);
// }

void drawTextAt(int16_t x, int16_t y, const char* text, int length)
{
    if (!g_font) {
        return;
    }
    if (!g_renderer || !text || length <= 0) {
        return;
    }

    std::string str(text, length);

    SDL_Color color{0,0,0,255};

    SDL_Surface* surface = TTF_RenderText_Solid(
        g_font,
        str.c_str(),
        str.length(),
        color
    );
    if (!surface)
        return;

    int w = surface->w;
    int h = surface->h;

    SDL_Texture* tex = SDL_CreateTextureFromSurface(g_renderer, surface);
    SDL_DestroySurface(surface);

    if (!tex)
        return;
    float scaleX = 0.9f;
    SDL_FRect dst{
        (float)x,
        (float)y,
        (float)(w * scaleX),
        (float)h
    };

    SDL_RenderTexture(g_renderer, tex, nullptr, &dst);

    SDL_DestroyTexture(tex);
}

static bool endsWithCaseInsensitive(const std::string& s, const std::string& suffix) {
    if (suffix.size() > s.size()) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])));
        const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
        if (a != b) return false;
    }
    return true;
}

static std::string extensionFromGlob(const char* glob) {
    if (!glob) return "";
    std::string g = glob;
    while (!g.empty() && std::isspace(static_cast<unsigned char>(g.back()))) g.pop_back();
    while (!g.empty() && std::isspace(static_cast<unsigned char>(g.front()))) g.erase(g.begin());
    if (g.size() >= 3 && g[0] == '*' && g[1] == '.') {
        return g.substr(1);
    }
    return "";
}

static std::string safeJoinStrings(const char* a, const char* b) {
    std::string out;
    if (a) out += a;
    if (b) out += b;
    return out;
}

static void refreshListing(
    const fs::path& dir,
    const std::string& extFilter,
    std::vector<FileEntry>& out
) {
    out.clear();

    // parent dir shortcut
    if (dir.has_parent_path()) {
        out.push_back(FileEntry{ "..", dir.parent_path(), true });
    }

    std::error_code ec;
    for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator();
         it.increment(ec))
    {
        const fs::directory_entry& de = *it;
        FileEntry fe;
        fe.fullPath = de.path();
        fe.name = fe.fullPath.filename().string();
        fe.isDir = de.is_directory(ec);

        if (fe.isDir) {
            out.push_back(std::move(fe));
        } else {
            if (extFilter.empty()) {
                out.push_back(std::move(fe));
            } else {
                const std::string fname = fe.name;
                if (endsWithCaseInsensitive(fname, extFilter)) {
                    out.push_back(std::move(fe));
                }
            }
        }
    }

    // tri: dirs d’abord, puis alpha
    std::stable_sort(out.begin(), out.end(), [](const FileEntry& a, const FileEntry& b) {
        if (a.isDir != b.isDir) return a.isDir > b.isDir;
        return a.name < b.name;
    });
}

struct FileDialogState {
    bool initialized = false;

    fs::path currentDir;
    std::string extFilter;       // ".kye"
    std::vector<FileEntry> items;

    int selectedIndex = -1;

    uint32_t lastClickTicks = 0;
    int lastClickIndex = -1;

    // layout très simple
    int x = 40, y = 40, w = 720, h = 520;
    int rowH = 22;
    int scroll = 0;
};

// Petit helper de hit-test
static int hitTestIndex(const FileDialogState& st, int mx, int my) {
    const int listX = st.x + 10;
    const int listY = st.y + 50;
    const int listW = st.w - 20;
    const int listH = st.h - 100;

    if (mx < listX || mx >= listX + listW) return -1;
    if (my < listY || my >= listY + listH) return -1;

    const int relY = my - listY;
    const int row = relY / st.rowH;
    const int idx = st.scroll + row;
    if (idx < 0 || idx >= static_cast<int>(st.items.size())) return -1;
    return idx;
}

static void clampScroll(FileDialogState& st) {
    const int visibleRows = std::max(1, (st.h - 100) / st.rowH);
    const int maxScroll = std::max(0, static_cast<int>(st.items.size()) - visibleRows);
    st.scroll = std::max(0, std::min(st.scroll, maxScroll));
}

static void renderSimple(FileDialogState& st, SDL_Renderer* r)
{
    SDL_FRect panel{
        (float)st.x, (float)st.y,
        (float)st.w, (float)st.h
    };
    SDL_SetRenderDrawColor(r, 20, 20, 22, 255);
    SDL_RenderFillRect(r, &panel);
    SDL_SetRenderDrawColor(r, 120, 120, 130, 255);
    SDL_RenderRect(r, &panel);
    SDL_FRect list{
        (float)(st.x + 10),
        (float)(st.y + 50),
        (float)(st.w - 20),
        (float)(st.h - 100)
    };

    SDL_SetRenderDrawColor(r, 30, 30, 34, 255);
    SDL_RenderFillRect(r, &list);

    SDL_SetRenderDrawColor(r, 80, 80, 90, 255);
    SDL_RenderRect(r, &list);

    const int visibleRows = std::max(1, (int)(list.h / st.rowH));
    const int end = std::min((int)st.items.size(), st.scroll + visibleRows);

    for (int i = st.scroll; i < end; ++i)
    {
        const int row = i - st.scroll;

        SDL_FRect rowRect{
            list.x,
            list.y + row * st.rowH,
            list.w,
            (float)st.rowH
        };

        if (i == st.selectedIndex) {
            SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
            SDL_RenderFillRect(r, &rowRect);
        }

        // icône
        SDL_FRect icon{
            rowRect.x + 6.0f,
            rowRect.y + 4.0f,
            14.0f,
            14.0f
        };

        if (st.items[i].isDir)
            SDL_SetRenderDrawColor(r, 180, 160, 60, 255);
        else
            SDL_SetRenderDrawColor(r, 140, 140, 150, 255);
        SDL_RenderFillRect(r, &icon);
        SDL_SetRenderDrawColor(r, 50, 50, 60, 255);
        SDL_RenderLine(
            r,
            rowRect.x,
            rowRect.y + st.rowH - 1,
            rowRect.x + rowRect.w,
            rowRect.y + st.rowH - 1
        );
    }

    SDL_FRect btnOk{
        (float)(st.x + st.w - 180),
        (float)(st.y + st.h - 40),
        80.0f, 26.0f
    };

    SDL_FRect btnCancel{
        (float)(st.x + st.w - 90),
        (float)(st.y + st.h - 40),
        80.0f, 26.0f
    };

    SDL_SetRenderDrawColor(r, 60, 90, 60, 255);
    SDL_RenderFillRect(r, &btnOk);

    SDL_SetRenderDrawColor(r, 120, 60, 60, 255);
    SDL_RenderFillRect(r, &btnCancel);
}

bool fileOpenDialog_SDL(
    SDL_Renderer* renderer,
    const SDL_Event& e,
    const char* partA,
    const char* partB,
    const char* defaultFilter,
    std::string& outSelectedPath
) {
    static FileDialogState st;

    // 1) init = équivalent logique de sub_2A48
    if (!st.initialized) {
        st.initialized = true;
        st.extFilter = extensionFromGlob(defaultFilter); // "*.kye" -> ".kye"

        // équivalent "buffer = str_060C + str_068C" puis DlgDirList:
        // ici on interprète ça comme un "répertoire initial"
        std::string initial = safeJoinStrings(partA, partB);
        fs::path p = initial.empty() ? fs::current_path() : fs::path(initial);

        // si c’est un fichier, on prend son parent
        std::error_code ec;
        if (fs::is_regular_file(p, ec)) p = p.parent_path();
        if (p.empty() || !fs::exists(p, ec) || !fs::is_directory(p, ec)) {
            p = fs::current_path();
        }

        st.currentDir = p;
        refreshListing(st.currentDir, st.extFilter, st.items);
        st.selectedIndex = -1;
        st.scroll = 0;
    }

    // 2) input handling
    if (e.type == SDL_EVENT_MOUSE_WHEEL) {
        st.scroll -= e.wheel.y; // wheel up => y=+1
        clampScroll(st);
    } else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) {
        const int idx = hitTestIndex(st, e.button.x, e.button.y);
        if (idx >= 0) {
            const uint32_t now = SDL_GetTicks();
            const bool dbl = (idx == st.lastClickIndex) && (now - st.lastClickTicks < 350);
            st.lastClickIndex = idx;
            st.lastClickTicks = now;

            st.selectedIndex = idx;

            if (dbl) {
                const auto& it = st.items[idx];
                if (it.isDir) {
                    st.currentDir = it.fullPath;
                    refreshListing(st.currentDir, st.extFilter, st.items);
                    st.selectedIndex = -1;
                    st.scroll = 0;
                } else {
                    outSelectedPath = it.fullPath.string();
                    st.initialized = false; // fermer
                    return true;
                }
            }
        } else {
            // clic boutons? (simple)
            SDL_Rect btnOk{ st.x + st.w - 180, st.y + st.h - 40, 80, 26 };
            SDL_Rect btnCancel{ st.x + st.w - 90, st.y + st.h - 40, 80, 26 };
            auto inRect = [](int x, int y, const SDL_Rect& r) {
                return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
            };

            if (inRect(e.button.x, e.button.y, btnCancel)) {
                st.initialized = false;
                return false;
            }
            if (inRect(e.button.x, e.button.y, btnOk)) {
                if (st.selectedIndex >= 0 && st.selectedIndex < (int)st.items.size()) {
                    const auto& it = st.items[st.selectedIndex];
                    if (!it.isDir) {
                        outSelectedPath = it.fullPath.string();
                        st.initialized = false;
                        return true;
                    }
                }
            }
        }
    } else if (e.type == SDL_EVENT_KEY_DOWN) {
        switch (e.key.key) {
            case SDLK_ESCAPE:
                st.initialized = false;
                return false;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (st.selectedIndex >= 0 && st.selectedIndex < (int)st.items.size()) {
                    const auto& it = st.items[st.selectedIndex];
                    if (it.isDir) {
                        st.currentDir = it.fullPath;
                        refreshListing(st.currentDir, st.extFilter, st.items);
                        st.selectedIndex = -1;
                        st.scroll = 0;
                    } else {
                        outSelectedPath = it.fullPath.string();
                        st.initialized = false;
                        return true;
                    }
                }
                break;

            case SDLK_BACKSPACE:
                if (st.currentDir.has_parent_path()) {
                    st.currentDir = st.currentDir.parent_path();
                    refreshListing(st.currentDir, st.extFilter, st.items);
                    st.selectedIndex = -1;
                    st.scroll = 0;
                }
                break;

            case SDLK_DOWN:
                if (!st.items.empty()) {
                    st.selectedIndex = std::min(st.selectedIndex + 1, (int)st.items.size() - 1);
                    clampScroll(st);
                }
                break;

            case SDLK_UP:
                if (!st.items.empty()) {
                    st.selectedIndex = std::max(st.selectedIndex - 1, 0);
                    clampScroll(st);
                }
                break;
        }
    }

    renderSimple(st, renderer);
    return false;
}

void drawFrame(SDL_Renderer* renderer, const SDL_FRect& r, bool selected)
{
    if (selected)
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    else
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderRect(renderer, &r);

    SDL_FRect inner = r;
    inner.x += 1.0f;
    inner.y += 1.0f;
    inner.w -= 2.0f;
    inner.h -= 2.0f;

    SDL_RenderRect(renderer, &inner);
}

void releaseDialogResources(SDL_Texture* dialogTexture)
{
    if (dialogTexture) {
        SDL_DestroyTexture(dialogTexture);
    }
}

void cleanupAndExit(int exitCode)
{
    if (g_renderer) { SDL_DestroyRenderer(g_renderer); g_renderer = nullptr; }
    if (g_window)   { SDL_DestroyWindow(g_window);     g_window = nullptr; }
    SDL_Quit();
    std::exit(exitCode);
}

void initTickCounter()
{
    const uint64_t ticksMs = static_cast<uint64_t>(SDL_GetTicks());
    const uint32_t t = static_cast<uint32_t>(ticksMs & 0xFFFFFFFFu);

    g_biosTickCountLo = static_cast<uint16_t>(t & 0xFFFFu);
    g_biosTickCountHi = static_cast<uint16_t>((t >> 16) & 0xFFFFu);
}

void drawWhatDialogUI()
{
    SDL_FRect box{
        200.0f,
        150.0f,
        400.0f,
        200.0f
    };

    SDL_SetRenderDrawColor(g_renderer, 200, 200, 200, 255);
    SDL_RenderFillRect(g_renderer, &box);

    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderRect(g_renderer, &box);
}

SpriteSheet loadBmpSheet(SDL_Renderer* r, const char* path, int tileW, int tileH)
{
    SpriteSheet s;
    s.tileW = tileW;
    s.tileH = tileH;

    SDL_Surface* surf = SDL_LoadBMP(path);
    if (!surf) throw std::runtime_error(std::string("SDL_LoadBMP failed: ") + SDL_GetError());

    s.w = surf->w;
    s.h = surf->h;

    std::cout << "Loaded BMP: " << path
              << " surface=" << surf
              << " w=" << s.w
              << " h=" << s.h << std::endl;

    SDL_Surface* converted = SDL_ConvertSurface(surf, SDL_PIXELFORMAT_RGBA32);

    s.tex = SDL_CreateTextureFromSurface(r, converted);

    SDL_DestroySurface(converted);
    SDL_DestroySurface(surf);

    if (!s.tex) throw std::runtime_error(std::string("SDL_CreateTextureFromSurface failed: ") + SDL_GetError());

    return s;
}

static void renderKyeTile()
{
    const int dstX = currentCol * cellWidth;
    const int dstY = currentRow * cellHeight;

    // Kye sprite position dans la sheet
    constexpr int srcX = 0;
    constexpr int srcY = 0;

    g_blockSheet.blit16(dstX, dstY, srcX, srcY);
}

void SpriteSheet16::blit16(int,int,int,int) {}

static void renderSparkle(float intensity)
{
    // intensity : 0.0 → 1.0
    SDL_SetRenderDrawBlendMode(g_renderer, SDL_BLENDMODE_BLEND);

    SDL_FRect rect;
    rect.x = static_cast<float>(currentCol * cellWidth);
    rect.y = static_cast<float>(currentRow * cellHeight);
    rect.w = static_cast<float>(cellWidth);
    rect.h = static_cast<float>(cellHeight);

    Uint8 alpha = static_cast<Uint8>(255.0f * intensity);

    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, alpha);
    SDL_RenderFillRect(g_renderer, &rect);
}

int showFileMessage(const char* message)
{
    const char* filename = std::strrchr(message, '\\');

    if (filename)
        filename++;
    else
        filename = message;

    SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR,
        filename,
        message,
        g_window
    );

    return 0;
}

void renderSparkleTileAndPresent(int sparkleCount)
{
    if (!g_renderer)
        return;
    SDL_FRect dst{
        float(gridOriginX + currentRow * cellWidth),
        float(gridOriginY + currentCol * cellHeight),
        float(cellWidth),
        float(cellHeight)
    };
    SDL_FRect src{
        0.0f,
        0.0f,
        16.0f,
        16.0f
    };

    // draw Kye
    SDL_RenderTexture(g_renderer, g_sheetKye.tex, &src, &dst);

    // sparkle pixels
    SDL_SetRenderDrawColor(g_renderer, 255,255,255,255);

    for (int i = 0; i < sparkleCount; i++)
    {
        int px = currentCol * cellWidth  + (rand() & 15);
        int py = currentRow * cellHeight + (rand() & 15);

        SDL_RenderPoint(g_renderer, px, py);
    }

    SDL_RenderPresent(g_renderer);
}

void runTileSparkleEffect(int effectId)
{
    if (!g_renderer)
        return;

    if (effectId == 1)
    {
        int sparkle = 0x100;

        while (sparkle >= 0)
        {
            renderSparkleTileAndPresent(sparkle);
            sparkle -= 16;
        }

        return;
    }

    if (effectId == 2)
    {
        for (int sparkle = 0; sparkle < 0x100; sparkle += 16)
        {
            renderSparkleTileAndPresent(sparkle);
        }

        return;
    }
    SDL_FRect src{
        0.0f,
        0.0f,
        16.0f,
        16.0f
    };
    SDL_FRect dst{
        float(currentCol * cellWidth),
        float(currentRow * cellHeight),
        float(cellWidth),
        float(cellHeight)
    };

    SDL_RenderTexture(g_renderer, g_sheetKye.tex, &src, &dst);
}

void drawText(int x, int y, const char* text, int len)
{
    if (!g_font || !text) {
        std::cout << "FONT NULL" << std::endl;
        return;
    }
    SDL_Color color = {255, 255, 255, 255};

    std::string str(text, len);

    SDL_Surface* surface = TTF_RenderText_Blended(g_font, str.c_str(), str.size(), color);

    if (!surface)
        return;

    SDL_Texture* texture = SDL_CreateTextureFromSurface(g_renderer, surface);

    SDL_FRect dst;
    dst.x = (float)x;
    dst.y = (float)y;
    dst.w = (float)surface->w;
    dst.h = (float)surface->h;

    SDL_RenderTexture(g_renderer, texture, nullptr, &dst);

    SDL_DestroyTexture(texture);
    SDL_DestroySurface(surface);
}

SDL_Texture* loadTexture(SDL_Renderer* renderer, const char* path)
{
    SDL_Surface* surf = SDL_LoadBMP(path);
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_DestroySurface(surf);
    return tex;
}
