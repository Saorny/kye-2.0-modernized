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

namespace fs = std::filesystem;

struct FileEntry {
    std::string name;
    fs::path fullPath;
    bool isDir = false;
};

HWND      g_hwnd = nullptr;

int isPendingDraw = 0;
int colOffsetBytes = 0;
int cellHeight = 16;
int cellWidth = 16;
int gridOriginX = 0;
int gridOriginY = 0;
uint16_t g_biosTickCountLo = 0;
uint16_t g_biosTickCountHi = 0;

HDC g_hwnd = nullptr;
HGDIOBJ gridPen = nullptr;

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
    HDC windowDC = GetDC(g_hdc2);
    if (!windowDC) return;

    // Créer un DC mémoire compatible avec celui de la fenêtre
    HDC memoryDC = CreateCompatibleDC(windowDC);
    if (!memoryDC) {
        ReleaseDC(g_hdc2, windowDC);
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
    ReleaseDC(g_hdc2, windowDC);
}

int isPointInRect(int x, int y) {
    return PTINRECT(&g_mainRect, x, y);
}

void showMessage(const char* caption, const char* message) {
    MessageBoxA(
        g_hdc2,
        message,
        caption,
        MB_ICONWARNING
    );
}


void drawRectangleFromGrid(int row, int col) {
    // Marque la cellule dans la grille comme sélectionnée
    g_state.bottomEntityMap[row][col] = 0xFFFF;

    // Sélectionne l'objet graphique (stylo/pinceau ?) à utiliser pour dessiner
    HGDIOBJ oldPen = SelectObject(g_hwnd, gridPen);

    // Calcule les coordonnées en pixels de la cellule à dessiner
    int top    = gridOriginY + row * cellHeight;
    int left   = gridOriginX + col * cellWidth;
    int bottom = top + cellHeight;
    int right  = left + cellWidth;

    // Dessine le rectangle à la position donnée
    Rectangle(g_hwnd, left, top, right, bottom);

    // (Optionnel) On pourrait re-sélectionner l'ancien objet si besoin
    // SelectObject(g_hdc2, oldPen);
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

    const char* base = getLegacyString(g_selectedFilePath);
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
    bitmap_kye   = LoadBitmap(g_hInstance, kye);
    bitmap_block = LoadBitmap(g_hInstance, aBlock);
    bitmap_wall  = LoadBitmap(g_hInstance, aWall);
}

void stopGameTimer() {
    if (g_timerId != 0) {
        KILLTIMER(g_hwnd, 0);  // Stoppe le timer ID 0 pour ce contexte
    }
    g_timerActive = 0;
}

const char* getPointerFromSegmentOffset(uint16_t seg, uint16_t off)
{
    (void)seg;
    (void)off;
    return "Kye (Modern)";
}

static int initializeGameWindow(
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

    {
        const char* title = getPointerFromSegmentOffset(titleSegment, titleOffset);
        int i = 0;
        while (title && title[i] != '\0' && i < (int)sizeof(windowTitle) - 1) {
            windowTitle[i] = title[i];
            ++i;
        }
        windowTitle[i] = '\0';
    }

    initializeWindowSize();
    loadGraphicsResources();
    initializeWindowHandleIfNeeded();
    initializeLayoutRects();
    advanceToNextLevelOrBlock();

    const int seed = computeTimestampNow(nullptr);
    seedGameRNG(seed);

    startPollingTimer();
    frameCounter = 0;
    resetLevelStateMemory();

    if (!isDiggerKeyword(windowTitle)) {
        copyMemory(g_selectedFilePath, borderTitle, 10);
        loadLevelByIndex(g_levelIndex);
        clearStatusLine(g_statusLineBuffer);
        updateNextLevelMenuItem();
        matchedEntryCount = 0;
        showWhatDialog();
    }

    copyMemory(g_selectedFilePath, defaultKyeTitle, 12);
    loadLevelByIndex(g_levelIndex);
    clearStatusLine(g_statusLineBuffer);
    updateNextLevelMenuItem();
    matchedEntryCount = 0;

    return 1;
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

void renderEntityToSdl(int16_t entityIndex)
{
    if (entityIndex < 0 || entityIndex >= GameState::MAX_NUM_ENTITIES) return;

    const EntityActionStruct& e = g_gameState.entities[entityIndex];

    const int type    = e.actionCode;
    const int row     = e.row;
    const int col     = e.col;
    const int subtype = e.timer;

    int srcX = 0;
    int srcY = 0;

    if (type >= 0x0F && type <= 0x13) {
        srcX = type << 4;
        srcY = subtype << 4;         // 0..3 -> 0,16,32,48
    } else if (type < 0x17) {
        srcX = type << 4;
        srcY = 0;
    } else if (type >= 0x32 && type <= 0x3B) {
        srcX = type << 4;
        srcY = 0x0F00;               // tu as dit "on garde"
    } else {
        srcX = type << 4;
        srcY = 0;
    }

    const SDL_Rect src{ srcX, srcY, 16, 16 };
    const SDL_Rect dst{ col * cellWidth, row * cellHeight, 16, 16 };

    SDL_RenderCopy(gRenderer, spriteSheet, &src, &dst);
}

void drawRectangle(i16 left, i16 top, i16 right, i16 bottom)
{
    HDC hdc = GetDC(g_hdc2);
    if (!hdc) {
        return;
    }

    Rectangle(hdc, left, top, right, bottom);
    ReleaseDC(g_hdc2, hdc);
}


void drawTextAt(i16 x, i16 y, const char* text, int length)
{
    HDC hdc = GetDC(g_hdc2);
    if (!hdc) {
        return;
    }

    TextOutA(hdc, x, y, text, length);
    ReleaseDC(g_hdc2, hdc);
}

int showNameInputDialog()
{
    INT_PTR result = DialogBoxW(
        g_hInstance, 
        L"DLG_NAM1", 
        g_mainWindow,
        DLG_INPNAM_FUNC
    );

    return static_cast<int>(result);
}

static GridCellRect computeHudCellRectFromIndex(i16 cellIndex)
{
    const i16 gridRow = static_cast<i16>(cellIndex / 0x10);
    const i16 gridCol = static_cast<i16>(cellIndex % 0x10);

    const i16 cellBaseX = static_cast<i16>(gridRow * 0x14);
    const i16 cellBaseY = static_cast<i16>(gridCol * 0x14);

    return GridCellRect{
        static_cast<i16>(cellBaseX + 2),
        static_cast<i16>(cellBaseY + 2),
        static_cast<i16>(cellBaseX + 0x12),
        static_cast<i16>(cellBaseY + 0x12),
    };
}

void drawHudCellOutlineFromIndex_SDL(i16 cellIndex)
{
    const auto r = computeHudCellRectFromIndex(cellIndex);

    SDL_Rect rect;
    rect.x = r.x0;
    rect.y = r.y0;
    rect.w = r.x1 - r.x0;
    rect.h = r.y1 - r.y0;

    SDL_SetRenderDrawColor(gRenderer, 255, 255, 255, 255); // brush_white fill? (si tu veux fill)
    // SDL_RenderFillRect(gRenderer, &rect);

    SDL_SetRenderDrawColor(gRenderer, 128, 128, 128, 255); // pen_gray
    SDL_RenderDrawRect(gRenderer, &rect);
}

void renderFrame()
{
    if (g_isRendering) {
        advanceToNextLevelOrBlock();
        return;
    }

    g_isRendering = true;

    initializeLayoutRects();
    renderHudAndFrame();

    SDL_RenderPresent(gRenderer);

    g_isRendering = false;
}


static inline void setDrawColorPenBlack() { SDL_SetRenderDrawColor(gRenderer, 0, 0, 0, 255); }
static inline void setDrawColorPenGray()  { SDL_SetRenderDrawColor(gRenderer, 128, 128, 128, 255); }

void renderHudAndFrame()
{
    setDrawColorPenBlack();
    SDL_RenderDrawLine(gRenderer, baseX, uiTopY - 1, uiRightX, uiTopY - 1);
    SDL_RenderDrawLine(gRenderer, rightEdge3 + 1, baseY, rightEdge3 + 1, uiBottomLineY + 2);
    renderLivesAndLevelInfo();
    setDrawColorPenGray();

    const SDL_Rect statusBox{
        statusBoxLeftX,
        uiTopY,
        uiRightX - statusBoxLeftX,
        uiBottomY - uiTopY
    };
    SDL_RenderDrawRect(gRenderer, &statusBox);

    setDrawColorPenBlack();

    const int len = cStringLen(statusLineBuffer); // asm: scasb -> longueur
    drawTextSdl(statusBoxLeftX + 4, uiTopY, statusLineBuffer, len);

    renderFrameByInteractionMode();
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
    // "*.kye" -> ".kye"
    // si glob bizarre, fallback: ""
    if (!glob) return "";
    std::string g = glob;
    // retire espaces
    while (!g.empty() && std::isspace(static_cast<unsigned char>(g.back()))) g.pop_back();
    while (!g.empty() && std::isspace(static_cast<unsigned char>(g.front()))) g.erase(g.begin());
    if (g.size() >= 3 && g[0] == '*' && g[1] == '.') {
        return g.substr(1); // ".kye"
    }
    return "";
}

static std::string safeJoinStrings(const char* a, const char* b) {
    std::string out;
    if (a) out += a;
    if (b) out += b;
    return out;
}

static std::vector<fs::path> listDrivesWindows() {
    std::vector<fs::path> drives;
#ifdef _WIN32
    DWORD mask = GetLogicalDrives();
    for (char letter = 'A'; letter <= 'Z'; ++letter) {
        if (mask & 1u) {
            std::string root;
            root += letter;
            root += ":\\";
            drives.emplace_back(root);
        }
        mask >>= 1u;
    }
#endif
    return drives;
}

static void refreshListing(
    const fs::path& dir,
    const std::string& extFilter,
    std::vector<FileEntry>& out
) {
    out.clear();

    // sur Windows: montrer drives si on est “au root conceptuel” vide (optionnel)
    // Ici, on les montre toujours en “raccourcis” en haut.
#ifdef _WIN32
    for (const auto& d : listDrivesWindows()) {
        out.push_back(FileEntry{ d.string(), d, true });
    }
#endif

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

// Rendu minimal sans texte (si tu n’as pas SDL_ttf).
// -> On dessine juste des lignes + highlight.
// Si tu as SDL_ttf, tu peux afficher st.items[i].name.
static void renderSimple(FileDialogState& st, SDL_Renderer* r) {
    // fond
    SDL_Rect panel{ st.x, st.y, st.w, st.h };
    SDL_SetRenderDrawColor(r, 20, 20, 22, 255);
    SDL_RenderFillRect(r, &panel);

    // bordure
    SDL_SetRenderDrawColor(r, 120, 120, 130, 255);
    SDL_RenderDrawRect(r, &panel);

    // zone liste
    SDL_Rect list{ st.x + 10, st.y + 50, st.w - 20, st.h - 100 };
    SDL_SetRenderDrawColor(r, 30, 30, 34, 255);
    SDL_RenderFillRect(r, &list);
    SDL_SetRenderDrawColor(r, 80, 80, 90, 255);
    SDL_RenderDrawRect(r, &list);

    const int visibleRows = std::max(1, list.h / st.rowH);
    const int end = std::min(static_cast<int>(st.items.size()), st.scroll + visibleRows);

    for (int i = st.scroll; i < end; ++i) {
        const int row = i - st.scroll;
        SDL_Rect rowRect{ list.x, list.y + row * st.rowH, list.w, st.rowH };

        if (i == st.selectedIndex) {
            SDL_SetRenderDrawColor(r, 70, 70, 90, 255);
            SDL_RenderFillRect(r, &rowRect);
        }

        // petite icône à gauche: dossier / fichier
        SDL_Rect icon{ rowRect.x + 6, rowRect.y + 4, 14, 14 };
        if (st.items[i].isDir) SDL_SetRenderDrawColor(r, 180, 160, 60, 255);
        else                   SDL_SetRenderDrawColor(r, 140, 140, 150, 255);
        SDL_RenderFillRect(r, &icon);

        // ligne séparatrice
        SDL_SetRenderDrawColor(r, 50, 50, 60, 255);
        SDL_RenderDrawLine(r, rowRect.x, rowRect.y + st.rowH - 1, rowRect.x + rowRect.w, rowRect.y + st.rowH - 1);
    }

    // boutons (juste rectangles)
    SDL_Rect btnOk{ st.x + st.w - 180, st.y + st.h - 40, 80, 26 };
    SDL_Rect btnCancel{ st.x + st.w - 90, st.y + st.h - 40, 80, 26 };
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
    if (e.type == SDL_MOUSEWHEEL) {
        st.scroll -= e.wheel.y; // wheel up => y=+1
        clampScroll(st);
    } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
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
    } else if (e.type == SDL_KEYDOWN) {
        switch (e.key.keysym.sym) {
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

    // 3) render
    renderSimple(st, renderer);
    return false; // pas encore validé
}

static void drawFrame(SDL_Renderer* renderer, const SDL_Rect& r, bool selected) {
    // brush_black vs brush_white -> on fait simple: noir si selected sinon blanc
    if (selected) SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    else          SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);

    SDL_RenderRect(renderer, &r);

    // INFLATERECT(-1,-1) => shrink de 1
    SDL_Rect inner = r;
    inner.x += 1; inner.y += 1;
    inner.w -= 2; inner.h -= 2;

    SDL_RenderRect(renderer, &inner);
}

static void cleanupAndExit(int exitCode)
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
    SDL_Rect box { 200, 150, 400, 200 };
    SDL_SetRenderDrawColor(g_renderer, 200, 200, 200, 255);
    SDL_RenderFillRect(g_renderer, &box);

    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderRect(g_renderer, &box);

    // Texte à ajouter plus tard
}
