# ======================================
# Compiler
# ======================================

CXX = g++

# ======================================
# Target
# ======================================

TARGET = kye.exe

# ======================================
# Source files
# ======================================

SRCS = \
main.cpp \
graph.cpp \
graph_globals.cpp \
file.cpp \
file_globals.cpp \
menu.cpp \
game.cpp \
game_globals.cpp \
system.cpp \
util.cpp \
error.cpp \
tinyfiledialogs.c

OBJS = $(SRCS:.cpp=.o)
OBJS := $(OBJS:.c=.o)

DEPS = $(SRCS:.cpp=.d)
DEPS := $(DEPS:.c=.d)

# ======================================
# Package files
# ======================================

DIST_DIR = release
ZIP_NAME = kye2-modern-win64.zip

ASSET_DIRS = graph
LEVEL_FILES = default.kye

# ======================================
# Compiler / linker flags
# ======================================

CXXFLAGS = -std=gnu++17 -Wall -Wextra -pedantic
DEBUGFLAGS = -g -O0
RELEASEFLAGS = -O2 -DNDEBUG -s

SDL_CFLAGS = $(shell pkg-config --cflags sdl3 sdl3-ttf)
SDL_LIBS   = $(filter-out -mwindows,$(shell pkg-config --libs sdl3 sdl3-ttf))

CXXFLAGS += $(SDL_CFLAGS)

LIBS = $(SDL_LIBS) -lcomdlg32 -lole32

# ======================================
# Default target
# ======================================

all: debug

# ======================================
# Debug build
# ======================================

debug: CXXFLAGS += $(DEBUGFLAGS)
debug: $(TARGET)

# ======================================
# Release build
# ======================================

release: CXXFLAGS += $(RELEASEFLAGS)
release: LDFLAGS += -mwindows
release: $(TARGET)

# ======================================
# Linking
# ======================================

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LIBS) $(LDFLAGS)

# ======================================
# Compilation
# ======================================

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

%.o: %.c
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# ======================================
# Dependencies
# ======================================

-include $(DEPS)

# ======================================
# Windows package
# ======================================

package:
	$(MAKE) clean
	$(MAKE) release
	rm -rf $(DIST_DIR)
	rm -f $(ZIP_NAME)
	mkdir -p $(DIST_DIR)

	cp $(TARGET) $(DIST_DIR)/
	cp -r $(ASSET_DIRS) $(DIST_DIR)/
	cp $(LEVEL_FILES) $(DIST_DIR)/

	cp /mingw64/bin/SDL3.dll $(DIST_DIR)/
	cp /mingw64/bin/SDL3_ttf.dll $(DIST_DIR)/

	cp /mingw64/bin/libstdc++-6.dll $(DIST_DIR)/
	cp /mingw64/bin/libgcc_s_seh-1.dll $(DIST_DIR)/
	cp /mingw64/bin/libwinpthread-1.dll $(DIST_DIR)/

	cp /mingw64/bin/libharfbuzz-0.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libfreetype-6.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libpng16-16.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/zlib1.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libbz2-1.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libbrotlidec.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libbrotlicommon.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libintl-8.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libiconv-2.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libglib-2.0-0.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libgraphite2.dll $(DIST_DIR)/ || true
	cp /mingw64/bin/libpcre2-8-0.dll $(DIST_DIR)/ || true

	echo "Kye 2.0 Modern SDL3 Port" > $(DIST_DIR)/README.txt
	echo "" >> $(DIST_DIR)/README.txt
	echo "How to run:" >> $(DIST_DIR)/README.txt
	echo "Double-click kye.exe." >> $(DIST_DIR)/README.txt
	echo "" >> $(DIST_DIR)/README.txt
	echo "Included files:" >> $(DIST_DIR)/README.txt
	echo "- kye.exe" >> $(DIST_DIR)/README.txt
	echo "- SDL3.dll" >> $(DIST_DIR)/README.txt
	echo "- SDL3_ttf.dll" >> $(DIST_DIR)/README.txt
	echo "- graph/ assets" >> $(DIST_DIR)/README.txt
	echo "- default.kye" >> $(DIST_DIR)/README.txt

	cd $(DIST_DIR) && powershell -Command "Compress-Archive -Force * ../$(ZIP_NAME)"

# ======================================
# Clean
# ======================================

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

rebuild:
	$(MAKE) clean
	$(MAKE) all

# ======================================
# Phony
# ======================================

.PHONY: all debug release clean rebuild package