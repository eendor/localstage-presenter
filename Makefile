CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wshadow -Wformat=2 -fstack-protector-strong -D_FORTIFY_SOURCE=2
LDLIBS = -lm
SRC = src/main.c src/http.c src/ws.c src/signaling.c src/decks.c src/net.c src/json.c src/qr.c src/util.c src/plat.c
GEN = build/web.c build/pdfjs.c build/pdfworker.c build/pptx.c build/h2c.c
OBJ = $(SRC:.c=.o) $(GEN:.c=.o)

all: localstage

localstage: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/server.h src/util.h src/json.h src/qr.h src/plat.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/embed: tools/embed.c
	@mkdir -p build
	$(CC) -O2 -o $@ $<

build/web.c: web/index.html build/embed
	build/embed web/index.html $@

build/pdfjs.c: web/vendor/pdf.min.js build/embed
	build/embed $< $@ web_pdfjs

build/pdfworker.c: web/vendor/pdf.worker.min.js build/embed
	build/embed $< $@ web_pdfworker

build/pptx.c: web/vendor/pptx-renderer.js build/embed
	build/embed $< $@ web_pptx

build/h2c.c: web/vendor/html2canvas.min.js build/embed
	build/embed $< $@ web_h2c

build/%.o: build/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

build/test: tests/test.c src/util.c src/json.c src/qr.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: localstage build/test
	build/test ./localstage

# Windows build (cross-compile from Linux/macOS with MinGW or zig, or build natively in MSYS2):
#   make windows                                  # uses x86_64-w64-mingw32-gcc
#   make windows WINCC="zig cc -target x86_64-windows-gnu"
WINCC ?= x86_64-w64-mingw32-gcc
windows: localstage.exe
localstage.exe: $(SRC) $(GEN) src/*.h
	$(WINCC) -O2 -std=c11 -Wall -Wextra -D_WIN32_WINNT=0x0601 -D__USE_MINGW_ANSI_STDIO=1 -o $@ $(SRC) $(GEN) -lws2_32 -liphlpapi -lbcrypt -lshell32

clean:
	rm -rf build localstage localstage.exe src/*.o

.PHONY: all test clean run windows

run: localstage
	OPEN_BROWSER=1 ./localstage
