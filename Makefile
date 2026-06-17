# Makefile multiplateforme : Debian (raylib via pkg-config) et Windows/MinGW.
#
#   make            -> compile le binaire
#   make run        -> compile puis lance avec la carte d'exemple
#   make clean      -> supprime le binaire
#
# Sur Windows, utilise plutot build_win.bat (qui ajoute w64devkit au PATH puis
# appelle ce Makefile). RAYLIB_DIR pointe alors la lib MinGW pre-compilee.

CC      ?= cc
CFLAGS  ?= -O3 -std=c99 -Wall -Wextra -Wno-format-truncation
CFLAGS  += -Isrc

SRC = src/main.c src/sspm.c src/miniz.c
HDR = src/sspm.h src/miniz.h

# Carte par defaut pour `make run`.
MAP ?= ../java-sspm-reader-main/java-sspm-reader-main/map1.sspm

ifeq ($(OS),Windows_NT)
# -------- Windows (MinGW + raylib pre-compile) --------
  RAYLIB_DIR ?= ../tools/raylib
  CFLAGS  += -I$(RAYLIB_DIR)/include
  LDLIBS   = -L$(RAYLIB_DIR)/lib -lraylib -lopengl32 -lgdi32 -lwinmm -lole32 -mwindows
  BIN      = sspm-player.exe
  RM       = del /q
else
# -------- Linux / Debian (raylib via pkg-config) --------
  RAYLIB_CFLAGS := $(shell pkg-config --cflags raylib 2>/dev/null)
  RAYLIB_LIBS   := $(shell pkg-config --libs   raylib 2>/dev/null)
  ifeq ($(strip $(RAYLIB_LIBS)),)
    RAYLIB_LIBS := -lraylib
  endif
  CFLAGS  += $(RAYLIB_CFLAGS)
  LDLIBS   = $(RAYLIB_LIBS) -lGL -lm -lpthread -ldl -lrt -lX11
  BIN      = sspm-player
  RM       = rm -f
endif

$(BIN): src/main.c src/globals.c src/settings.c src/settings_ui.c src/play.c src/menu.c src/calib.c src/profile_ui.c src/sspm.c src/filepicker.c src/bg.c src/postfx.c src/miniz.o $(HDR)
	$(CC) $(CFLAGS) -o $@ src/main.c src/globals.c src/settings.c src/settings_ui.c src/play.c src/menu.c src/calib.c src/profile_ui.c src/sspm.c src/filepicker.c src/bg.c src/postfx.c src/miniz.o $(LDLIBS)

src/miniz.o: src/miniz.c src/miniz.h
	$(CC) $(CFLAGS) -Wno-type-limits -c src/miniz.c -o src/miniz.o

run: $(BIN)
	./$(BIN) "$(MAP)"

clean:
	$(RM) $(BIN) src/miniz.o

.PHONY: run clean
