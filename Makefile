CPP = g++
CFLAGS = -g -O -std=gnu++11 -pedantic -Wall -Wno-uninitialized -datapath=./LEVEL -Wc++11-extensions
CXXFLAGS = -std=gnu++11 -g -Og

CFLAGS += -I/usr/local/include/SDL2

LDFLAGS += -L/usr/local/Cellar/sdl2/2.30.9/lib

SDL_LIBS = -lSDL2 -lSDL2_ttf

SRCS = main.cpp graph-lib.cpp sqx_encoder.cpp sqx_converter.cpp sqx_decoder.cpp game-file.cpp map.cpp editor.cpp
OBJS = $(SRCS:.cpp=.o)
DEPS = $(SRCS:.cpp=.d)

OBJDIR = obj/sdl

editor: $(OBJS)
	$(CPP) $(OBJS) -o $@ $(SDL_LIBS) $(CFLAGS) $(LDFLAGS)

clean:
	rm -f $(OBJS) $(DEPS) editor

reboot: clean editor

%.o: %.cpp
	$(CPP) $(CXXFLAGS) -c $< -o $@

%i.d: %.cpp
	$(CPP) $(CXXFLAGS) -MMD -MP $< -MF $@

release: $(OBJS)
	# Create the app bundle structure
	mkdir -p release/Contents/MacOS
	mkdir -p release/Contents/Resources

	# Copy the executable to the app bundle
	cp editor release/Contents/MacOS/

	# Optionally, add icons or resources to the Resources folder
	# cp my_icon.icns release/Contents/Resources/

	# Now make the app executable
	chmod +x release/Contents/MacOS/editor

	# Create a basic Info.plist (you can customize this further)
	echo '<?xml version="1.0" encoding="UTF-8"?>' > release/Contents/Info.plist
	echo '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' >> release/Contents/Info.plist
	echo '<plist version="1.0">' >> release/Contents/Info.plist
	echo '<dict>' >> release/Contents/Info.plist
	echo '  <key>CFBundleExecutable</key>' >> release/Contents/Info.plist
	echo '  <string>editor</string>' >> release/Contents/Info.plist
	echo '</dict>' >> release/Contents/Info.plist
	echo '</plist>' >> release/Contents/Info.plist

	# Make the app bundle discoverable by Finder
	xattr -cr release

	# Finished!
	echo "App bundle created: release"