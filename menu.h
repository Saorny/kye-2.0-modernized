#ifndef MENU_H
#define MENU_H

#include <SDL3/SDL.h>
#include "game.h"

enum class ActiveMenu : std::int16_t
{
    None  = 0,
    Game  = 1,
    Level = 2,
    Help  = 3,
};

extern ActiveMenu g_activeMenu;
extern int g_hoveredMenuItem;

void drawMenuBar();
void drawActiveMenuPopup();
void handleMenuMouseMove(int x, int y);
bool handleMenuMouseDown(int x, int y);
void closeMenu();

bool handleMainMenuCommand(MenuCommand command);

#endif