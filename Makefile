CC = i686-w64-mingw32-gcc
WINDRES = i686-w64-mingw32-windres
CFLAGS = -std=c99 -O2 -Wall -Wextra -Werror -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 -DUNICODE -D_UNICODE
LDLIBS = -lcomctl32 -lshell32 -lgdi32 -luser32

all: build/connect95.exe

build:
	mkdir -p build

build/resources.o: resources/connect95.rc resources/connect95.manifest resources/phone.ico resources/send.ico resources/ring.ico resources/clipboard.ico | build
	$(WINDRES) -I resources $< $@

build/connect95.exe: src/main.c build/resources.o
	$(CC) $(CFLAGS) -mwindows -static -Wl,--major-subsystem-version,5,--minor-subsystem-version,1 -o $@ $^ $(LDLIBS)

clean:
	rm -rf build

.PHONY: all clean
