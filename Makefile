CC = gcc
CFLAGS = -Wall 
LIBS_VISTA = -lncurses

TARGETS = master player vista

all: check-ncurses $(TARGETS)

check-ncurses:
	@if ! pkg-config --exists ncurses 2>/dev/null; then \
		apt install libncurses5-dev libncursesw5-dev; \
	fi

master: master.c estructuras.h
	$(CC) $(CFLAGS) -o master master.c 

player: player.c estructuras.h
	$(CC) $(CFLAGS) -o player player.c 

vista: vista.c estructuras.h
	$(CC) $(CFLAGS)  -o vista vista.c $(LIBS_VISTA)

clean:
	rm -f $(TARGETS) *.o

