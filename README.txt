# Kye 2.0 Modern

**Kye 2.0 Modern** is an unofficial, free, non-commercial SDL3 preservation port of **Kye**, the classic puzzle game originally created by **Colin Garbutt** in 1992.

This project aims to make the original Kye experience playable on modern Windows systems while preserving the spirit, rules, timing, and behavior of the original game as closely as possible.

---

## Status

This project is currently a work-in-progress modern port.

Implemented / partially implemented:

- Modern C++17 codebase
- SDL3 rendering
- SDL3_ttf text rendering
- Original-style 20x30 grid gameplay
- Kye movement
- Static tiles and walls
- Diamonds
- Enemies
- Pushable blocks
- Magnets
- Arrow dispensers
- Lava animation
- Countdown entities
- One-way tiles
- Basic menu system
- Windows release packaging

Some behavior is still being refined against the original game.

---

## Download

A prebuilt Windows release is provided as a `.zip` package.

To play:

1. Download the release archive.
2. Extract it anywhere.
3. Run:

```text
kye.exe

Do not remove the graph/ folder or the .kye level files from the game directory.

Building from source
Requirements

This project is currently built with:

MSYS2 / MinGW64
GCC / G++
SDL3
SDL3_ttf
Make

Install dependencies in MSYS2 MinGW64:

pacman -S mingw-w64-x86_64-gcc
pacman -S mingw-w64-x86_64-SDL3
pacman -S mingw-w64-x86_64-SDL3_ttf
pacman -S make pkgconf
Debug build
make

or:

make debug
Release build
make release
Full Windows package
make package

This creates:

release/
kye2-modern-win64.zip

The package includes the executable, required DLLs, assets, and level files.

Project structure
.
├── main.cpp
├── game.cpp
├── game.h
├── graph.cpp
├── graph.h
├── menu.cpp
├── menu.h
├── file.cpp
├── file.h
├── system.cpp
├── util.cpp
├── error.cpp
├── graph/
│   ├── graph_kye.bmp
│   ├── graph_mobiles.bmp
│   ├── graph_statics.bmp
│   └── font files
├── default.kye
└── Makefile
Controls

The original Kye control style is preserved.

Typical controls:

Arrow keys: move Kye
Home / Page Up / End / Page Down: diagonal movement
Menu bar: Game / Level / Help

Exact behavior may still be refined during development.

Preservation notes

This project was created as a preservation and compatibility effort.

The goal is not to replace the original game, but to keep it playable and understandable on modern systems.

The implementation attempts to respect the original behavior, including timing, entity logic, animations, and level format behavior.

Legal / copyright notice

Kye was originally created by Colin Garbutt.

This project is an unofficial fan-made preservation port. It is not affiliated with, endorsed by, sponsored by, or approved by Colin Garbutt or any rightsholder.

The modern C++/SDL3 source code written for this port is released under the GNU General Public License v3.0, unless otherwise stated.

Original Kye name, concept, graphics, levels, data files, and any other original assets remain the property of Colin Garbutt or their respective rightsholders. These materials, when included, are provided only for preservation, compatibility, and historical purposes. They are not covered by the GPL license applied to the modern port source code.

If you are the original author or a rightsholder and would like any material removed, credited differently, or handled in another way, please open an issue or contact the maintainer. I will respond respectfully and promptly.

License
Source code

The source code of this modern port is licensed under:

GNU General Public License v3.0

See LICENSE for details.

Original assets and levels

Original Kye assets and level files are not licensed by this project.

They remain under the ownership of their original author or rightsholders and are included only for preservation and compatibility purposes.

Non-commercial statement

This project is distributed for free.

It is not sold, monetized, or bundled with advertising.

In the spirit of the original charity/shareware release, players are encouraged to support a children’s charity or another meaningful cause if they enjoy the game.

Credits

Original game:

Kye
Created by Colin Garbutt
1992

Modern port:

Kye 2.0 Modern
C++17 / SDL3 preservation port

Special thanks to everyone who keeps old games, old software, and digital history alive.

Disclaimer

This project is provided as-is, without warranty of any kind.

It may contain bugs, incomplete behavior, or inaccuracies compared with the original game.

Use at your own risk.

Contributing

Contributions are welcome, especially for:

Behavior comparison with the original game
Bug fixes
SDL3 rendering improvements
Timing and animation accuracy
Level compatibility
Documentation
Packaging improvements

Please keep the project respectful toward the original author and the original work.

Philosophy

Old games are part of our shared digital memory.

This project exists because small, strange, clever games like Kye deserve to remain playable, studyable, and loved.
