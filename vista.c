#include "estructuras.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <semaphore.h>
#include <errno.h>
#include <ncurses.h>


void print_state(GameState *gameState);

static FILE *tty_in = NULL;
static FILE *tty_out = NULL;
static SCREEN *scr = NULL;

int myInitscr()
{
    // Conservamos los FILE* como globals para cerrarlos al final.
    // Intentamos abrir /dev/tty para garantizar acceso al terminal real.
    tty_in  = fopen("/dev/tty", "r");
    tty_out = fopen("/dev/tty", "w");
    if (!tty_in || !tty_out) {
        fprintf(stderr, "myInitscr: No se pudo abrir /dev/tty (errno=%d %s)\n", errno, strerror(errno));
        if (tty_in) fclose(tty_in);
        if (tty_out) fclose(tty_out);
        tty_in = tty_out = NULL;
        return 1;
    }

    const char *term = getenv("TERM");
    if (!term || !*term) {
        term = "xterm-256color";
        fprintf(stderr, "myInitscr: TERM no estaba seteado, usando %s\n", term);
    }//Para evitar este error, asegurarnos en el master de que el execve le pase a este proceso $TERM

    scr = newterm(term, tty_out, tty_in);
    if (!scr) {
        fprintf(stderr, "myInitscr: newterm() fallo (TERM=%s)\n", term);
        fclose(tty_in); tty_in = NULL;
        fclose(tty_out); tty_out = NULL;
        return 1;
    }

    // Establecemos la pantalla creada como la actual.
    set_term(scr);

    // Configuraciones habituales
    cbreak();
    noecho();
    keypad(stdscr, TRUE);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_RED,     COLOR_BLACK);
        init_pair(2, COLOR_GREEN,   COLOR_BLACK);
        init_pair(3, COLOR_YELLOW,  COLOR_BLACK);
        init_pair(4, COLOR_BLUE,    COLOR_BLACK);
        init_pair(5, COLOR_MAGENTA, COLOR_BLACK);
        init_pair(6, COLOR_CYAN,    COLOR_BLACK);
        init_pair(7, COLOR_GREEN,   COLOR_WHITE);
        init_pair(8, COLOR_BLACK,   COLOR_WHITE);
        init_pair(9, COLOR_RED,     COLOR_WHITE);
    }

    return 0;
}

void end_curses()
{
    if (scr) {
        // restaura modo normal y libera screen
        endwin();
        delscreen(scr);
        scr = NULL;
    }
    if (tty_in) { fclose(tty_in); tty_in = NULL; }
    if (tty_out) { fclose(tty_out); tty_out = NULL; }
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    unsigned int width = atoi(argv[1]);
    unsigned int height = atoi(argv[2]);

    GameState *gameState = connectToSharedMemoryState(width, height);
    Semaphores *semaphores = connectToSharedMemorySemaphores();

    if (myInitscr()) {
        fprintf(stderr, "No se pudo inicializar ncurses vía /dev/tty. Saliendo.\n");
        return 1;
    }

    // Esperar cambios en el estado del juego
    while (1)
    {
    
        if (sem_wait(&semaphores->pendingView) == -1)
        {
            fprintf(stderr, "vista: sem_wait pendingView fallo errno=%d (%s)\n", errno, strerror(errno));
            break;
        }

        // Imprimir estado actual del juego
        print_state(gameState);

        // Notifica al máster que el estado fue impreso
        if (sem_post(&semaphores->viewEndedPrinting) == -1) {
            fprintf(stderr, "vista: sem_post viewEndedPrinting fallo errno=%d (%s)\n", errno, strerror(errno));
            // no rompemos necesariamente; seguimos hasta el próximo ciclo
        }

        // Si el juego terminó, termina
        if (gameState->gameOver)
        {
            break;
        }
    }

    end_curses();

    // Desmapear la memoria compartida (buena práctica)
    // (nota: no cerramos / shm_unlink aquí)
    // suponer que connect... devolvió un puntero mapeado
    // no tenemos map_size aquí; si necesitás, guardalo y munmap:
    // munmap(gameState, map_size);

    return 0;
}

// Imprime el estado del juego leyendo la grilla contigua y superponiendo jugadores
void print_state(GameState *gameState)
{
    if (gameState == NULL)
        return;

    unsigned short W = gameState->width;
    unsigned short H = gameState->height;

    // Limpio pantalla para evitar superposiciones en actualizaciones
    clear();

    printw("=== ESTADO DEL JUEGO ===\n");
    printw("Tablero: %ux%u | Jugadores: %u\n", W, H, gameState->playersNumber);

    for (unsigned int y = 0; y < H; y++)
    {
        for (unsigned int x = 0; x < W; x++)
        {
            // Asegurar que no quede ningún atributo/color arrastrado de impresiones anteriores
            attrset(A_NORMAL);
            int mostrado = 0;
            for (unsigned int p = 0; p < gameState->playersNumber; p++)
            {
                Player *pl = &gameState->players[p];
                if (pl->x == x && pl->y == y)
                {
                    attron(COLOR_PAIR((p % 9) + 1));
                    printw("P%u ", p + 1);
                    attroff(COLOR_PAIR((p % 9) + 1));
                    mostrado = 1;
                    break;
                }
            }
            if (!mostrado)
            {
                int v = gameState->grid[y * W + x];
                if(v < 0){
                    int idx = -v;
                    if (idx > 9) idx = 9;
                    attron(COLOR_PAIR(idx));
                    printw("%2d ", -v);
                    attroff(COLOR_PAIR(idx));
                } else {
                    // v == 0 (si apareciera) o v>0
                    printw("%2d ", v);
                }
            }
        }
        printw("\n");
    }

    printw("\nJugadores:\n");
    for (unsigned int i = 0; i < gameState->playersNumber; i++)
    {
        Player *pl = &gameState->players[i];
        attron(COLOR_PAIR((i % 9) + 1));
        printw("  %u: %s - Puntaje: %u, Pos: (%u,%u)%s\n",
               i + 1, pl->playerName, pl->score, pl->x, pl->y, pl->blocked ? " [BLOQ]" : "");
        attroff(COLOR_PAIR((i % 9) + 1));
    }

    if (gameState->gameOver)
    {
        printw("\n*** JUEGO TERMINADO ***\n");
    }
    printw("=======================\n\n");
    refresh();
}
