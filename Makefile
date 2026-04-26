# ======================================
# Compiler
# ======================================

CXX = g++

# ======================================
# Target
# ======================================

TARGET = editor

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
# Compiler flags
# ======================================

CXXFLAGS = -std=gnu++17 -Wall -Wextra -pedantic

# Debug build
DEBUGFLAGS = -g -O0

# Release build
RELEASEFLAGS = -O2

# SDL flags via pkg-config
SDL_CFLAGS = $(shell pkg-config --cflags sdl3 sdl3-ttf)
SDL_LIBS   = $(filter-out -mwindows,$(shell pkg-config --libs sdl3 sdl3-ttf)) -lcomdlg32

CXXFLAGS += $(SDL_CFLAGS)

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
release: $(TARGET)

# ======================================
# Linking
# ======================================

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(SDL_LIBS) -lole32

# ======================================
# Compilation
# ======================================

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

%.o: %.c
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# ======================================
# Include dependencies
# ======================================

-include $(DEPS)

# ======================================
# Clean
# ======================================

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)

rebuild: clean all

# ======================================
# macOS app bundle
# ======================================

bundle: release

	mkdir -p release/Contents/MacOS
	mkdir -p release/Contents/Resources

	cp $(TARGET) release/Contents/MacOS/
	chmod +x release/Contents/MacOS/$(TARGET)

	echo '<?xml version="1.0" encoding="UTF-8"?>' > release/Contents/Info.plist
	echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' >> release/Contents/Info.plist
	echo '<plist version="1.0">' >> release/Contents/Info.plist
	echo '<dict>' >> release/Contents/Info.plist
	echo '  <key>CFBundleExecutable</key>' >> release/Contents/Info.plist
	echo '  <string>$(TARGET)</string>' >> release/Contents/Info.plist
	echo '</dict>' >> release/Contents/Info.plist
	echo '</plist>' >> release/Contents/Info.plist

	xattr -cr release

	echo "App bundle created in ./release"