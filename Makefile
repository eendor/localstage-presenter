CC ?= cc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wshadow -Wformat=2 -fstack-protector-strong -D_FORTIFY_SOURCE=2
LDLIBS = -lm
SRC = src/main.c src/http.c src/ws.c src/signaling.c src/decks.c src/net.c src/json.c src/qr.c src/util.c
OBJ = $(SRC:.c=.o) build/web.o

all: localstage

localstage: $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LDLIBS)

src/%.o: src/%.c src/server.h src/util.h src/json.h src/qr.h
	$(CC) $(CFLAGS) -c -o $@ $<

build/embed: tools/embed.c
	@mkdir -p build
	$(CC) -O2 -o $@ $<

build/web.c: web/index.html build/embed
	build/embed web/index.html $@

build/web.o: build/web.c
	$(CC) $(CFLAGS) -c -o $@ $<

build/test: tests/test.c src/util.c src/json.c src/qr.c
	@mkdir -p build
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test: localstage build/test
	build/test ./localstage

clean:
	rm -rf build localstage src/*.o

.PHONY: all test clean run

run: localstage
	OPEN_BROWSER=1 ./localstage
