FILES = src/main.c src/parse.c src/mach.c src/hashtable.c src/events.c src/windows.c src/border.c src/auto_color.c src/animation.c
LIBS = -framework AppKit -framework CoreVideo -F/System/Library/PrivateFrameworks/ -framework SkyLight
MENUBAR_FILES = src/menubar/menubar.m src/menubar/options.c src/mach.c

all: | bin
	clang -std=c99 -O3 -g $(FILES) -o bin/borders $(LIBS)

menubar: | bin
	clang -std=c99 -O3 -g -fobjc-arc $(MENUBAR_FILES) -o bin/bordersbar -framework AppKit

debug: | bin
	clang -std=c99 -O0 -g -DDEBUG $(FILES) -o bin/debug $(LIBS)

asan: | bin
	clang -std=c99 -Wall -g -fsanitize=address -fsanitize=undefined -fno-omit-frame-pointer -g $(FILES) -o bin/debug $(LIBS)
	./bin/debug

test: | bin
	clang -std=c99 -O0 -g tests/auto_color_test.c src/auto_color.c -o bin/auto_color_test -framework CoreGraphics -framework CoreFoundation
	./bin/auto_color_test
	clang -std=c99 -O0 -g tests/menubar_options_test.c src/menubar/options.c -o bin/menubar_options_test
	./bin/menubar_options_test

bin:
	mkdir bin

clean:
	rm -rf bin
