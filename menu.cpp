#include "menu.h"

#include <vector>
#include <string>
#include <algorithm>
#include <cctype>

#include "game.h"
#include "graph.h"
#include "file.h"

ActiveMenu g_activeMenu = ActiveMenu::None;
int g_hoveredMenuItem = -1;

struct MenuItem
{
    const char* label;
    MenuCommand command;
    bool enabled;
};

struct MenuDefinition
{
    ActiveMenu menu;
    const char* title;
    SDL_Rect titleRect;
    SDL_Rect popupRect;
    std::vector<MenuItem> items;
};

static std::vector<MenuDefinition> buildMenus()
{
    return {
        {
            ActiveMenu::Game,
            "Game",
            SDL_Rect{ 24, 0, 72, 24 },
            SDL_Rect{ 24, 24, 148, 70 },
            {
                { "New Game", MenuCommand::NewGame, true },
                { "Exit",     MenuCommand::Quit,    true },
            }
        },
        {
            ActiveMenu::Level,
            "Level",
            SDL_Rect{ 104, 0, 82, 24 },
            SDL_Rect{ 104, 24, 218, 132 },
            {
                { "Restart level", MenuCommand::Restart,   true },
                { "Goto level...", MenuCommand::GotoLevel, true },
                { "File...",       MenuCommand::OpenFile,      true },
                { "Edit",          MenuCommand::EnterEditMode,   false },
            }
        },
        {
            ActiveMenu::Help,
            "Help",
            SDL_Rect{ 192, 0, 72, 24 },
            SDL_Rect{ 192, 24, 130, 96 },
            {
                { "Help",  MenuCommand::Help,  true },
                { "About", MenuCommand::About, true },
                { "What?", MenuCommand::What,  true },
            }
        },
    };
}

static bool pointInRect(int x, int y, const SDL_Rect& r)
{
    return x >= r.x && x < r.x + r.w &&
           y >= r.y && y < r.y + r.h;
}

static void drawFilledRect(const SDL_Rect& r, Uint8 red, Uint8 green, Uint8 blue)
{
    SDL_SetRenderDrawColor(g_renderer, red, green, blue, 255);

    SDL_FRect fr{
        static_cast<float>(r.x),
        static_cast<float>(r.y),
        static_cast<float>(r.w),
        static_cast<float>(r.h)
    };

    SDL_RenderFillRect(g_renderer, &fr);
}

static void drawRectBorder(const SDL_Rect& r, Uint8 red, Uint8 green, Uint8 blue)
{
    SDL_SetRenderDrawColor(g_renderer, red, green, blue, 255);

    SDL_FRect fr{
        static_cast<float>(r.x),
        static_cast<float>(r.y),
        static_cast<float>(r.w),
        static_cast<float>(r.h)
    };

    SDL_RenderRect(g_renderer, &fr);
}

static void drawMenuText(int x, int y, const char* text, bool enabled)
{
    if (!text)
        return;

    if (enabled)
        drawTextAt(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), text, static_cast<int>(std::strlen(text)));
    else
        drawTextAt(static_cast<std::int16_t>(x), static_cast<std::int16_t>(y), text, static_cast<int>(std::strlen(text)));
}

static MenuDefinition* findMenu(std::vector<MenuDefinition>& menus, ActiveMenu menu)
{
    for (auto& m : menus)
    {
        if (m.menu == menu)
            return &m;
    }

    return nullptr;
}

static const MenuItem* getHoveredItem(const MenuDefinition& menu)
{
    if (g_hoveredMenuItem < 0)
        return nullptr;

    if (g_hoveredMenuItem >= static_cast<int>(menu.items.size()))
        return nullptr;

    return &menu.items[g_hoveredMenuItem];
}

void drawMenuBar()
{
    if (!g_renderer)
        return;

    SDL_FRect bar{
        0.0f,
        0.0f,
        480.0f,
        static_cast<float>(MENU_BAR_HEIGHT)
    };

    SDL_SetRenderDrawColor(g_renderer, 255, 255, 255, 255);
    SDL_RenderFillRect(g_renderer, &bar);

    SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
    SDL_RenderRect(g_renderer, &bar);

    drawTextAt(8, 4, "Game", 4);
    drawTextAt(72, 4, "Level", 5);
    drawTextAt(144, 4, "Help", 4);

    drawActiveMenuPopup();
}

void drawActiveMenuPopup()
{
    if (g_activeMenu == ActiveMenu::None)
        return;

    auto menus = buildMenus();
    MenuDefinition* menu = findMenu(menus, g_activeMenu);

    if (!menu)
        return;

    drawFilledRect(menu->popupRect, 255, 255, 255);
    drawRectBorder(menu->popupRect, 0, 0, 0);

    constexpr int itemHeight = 26;

    for (int i = 0; i < static_cast<int>(menu->items.size()); ++i)
    {
        const MenuItem& item = menu->items[i];

        SDL_Rect itemRect{
            menu->popupRect.x + 1,
            menu->popupRect.y + 1 + i * itemHeight,
            menu->popupRect.w - 2,
            itemHeight
        };

        if (i == g_hoveredMenuItem && item.enabled)
            drawFilledRect(itemRect, 0, 0, 180);

        drawMenuText(
            itemRect.x + 24,
            itemRect.y + 4,
            item.label,
            item.enabled
        );
    }
}

void handleMenuMouseMove(int x, int y)
{
    auto menus = buildMenus();

    for (const auto& menu : menus)
    {
        if (pointInRect(x, y, menu.titleRect))
        {
            if (g_activeMenu != ActiveMenu::None)
            {
                g_activeMenu = menu.menu;
                g_hoveredMenuItem = -1;
            }

            return;
        }
    }

    MenuDefinition* active = findMenu(menus, g_activeMenu);

    if (!active)
    {
        g_hoveredMenuItem = -1;
        return;
    }

    if (!pointInRect(x, y, active->popupRect))
    {
        g_hoveredMenuItem = -1;
        return;
    }

    constexpr int itemHeight = 26;
    const int relativeY = y - active->popupRect.y - 1;
    const int index = relativeY / itemHeight;

    if (index >= 0 && index < static_cast<int>(active->items.size()))
        g_hoveredMenuItem = index;
    else
        g_hoveredMenuItem = -1;
}

bool handleMenuMouseDown(int x, int y)
{
    auto menus = buildMenus();

    for (const auto& menu : menus)
    {
        if (pointInRect(x, y, menu.titleRect))
        {
            if (g_activeMenu == menu.menu)
                g_activeMenu = ActiveMenu::None;
            else
                g_activeMenu = menu.menu;

            g_hoveredMenuItem = -1;
            return true;
        }
    }

    MenuDefinition* active = findMenu(menus, g_activeMenu);

    if (!active)
        return false;

    if (!pointInRect(x, y, active->popupRect))
    {
        closeMenu();
        return false;
    }

    constexpr int itemHeight = 26;
    const int relativeY = y - active->popupRect.y - 1;
    const int index = relativeY / itemHeight;

    if (index < 0 || index >= static_cast<int>(active->items.size()))
        return true;

    const MenuItem& item = active->items[index];

    if (!item.enabled)
        return true;

    closeMenu();
    return handleMainMenuCommand(item.command);
}

void closeMenu()
{
    g_activeMenu = ActiveMenu::None;
    g_hoveredMenuItem = -1;
}
