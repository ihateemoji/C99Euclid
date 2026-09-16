# Euclid RPE — C99 CLAP Euclidean MIDI sequencer
#   make          build the .clap (with X11 GUI)
#   make test     run the Euclidean engine tests
#   make install  copy into ~/.clap

CC      ?= gcc
CFLAGS  ?= -O3 -fPIC -Wall -Wextra -std=c99
CFLAGS  += -Ithird_party/clap/include -Isrc
LDFLAGS ?= -shared -Wl,--version-script=export.map -lm -lX11

SRC = src/euclid.c src/plugin.c src/gui_x11.c
OUT = EuclidRPE.clap

.PHONY: all clean test install

all: $(OUT)

$(OUT): $(SRC) src/euclid.h
	$(CC) $(CFLAGS) -o $(OUT) $(SRC) $(LDFLAGS)

test: src/euclid.c src/euclid_test.c src/euclid.h
	$(CC) -O0 -g -Wall -Wextra -std=c99 -Isrc -o /tmp/euclid_test src/euclid.c src/euclid_test.c
	/tmp/euclid_test

install: $(OUT)
	mkdir -p "$(HOME)/.clap"
	cp -f $(OUT) "$(HOME)/.clap/EuclidRPE.clap"

clean:
	rm -f $(OUT) /tmp/euclid_test
