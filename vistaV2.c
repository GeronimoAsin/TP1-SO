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

typedef struct
{
    char playerName[16];
    unsigned int score;
    unsigned int invalid;
    unsigned int valid;
    unsigned short x, y;
    pid_t pid;
    bool blocked;
} Player;

typedef struct
{
    unsigned short width;
    unsigned short height;
    unsigned int playersNumber;
    Player players[9];
    bool gameOver;
    int grid[];
} GameState;

typedef struct
{
    sem_t pendingView;
    sem_t viewEndedPrinting;
    sem_t mutexMasterAccess;
    sem_t mutexGameState;
    sem_t mutexPlayerAccess;
    unsigned int playersReadingState;
    sem_t playerCanMove[9];
} Semaphores;

GameState * connectToSharedMemoryState(unsigned int width, unsigned int height);
Semaphores * connectToSharedMemorySemaphores();
void print_state(GameState *gameState);


int main(int argc, char *argv[])
{
    unsigned int width = atoi(argv[1]);
    unsigned int height = atoi(argv[2]);
    GameState *gameState = connectToSharedMemoryState(width, height);
    Semaphores *semaphores = connectToSharedMemorySemaphores();

    // Esperar cambios en el estado del juego
    while (1)
    {
        // Espera notificación del máster
        if (sem_wait(&semaphores->pendingView) == -1)
        {
            perror("vista: sem_wait pendingView");
            break;
        }

        // Imprimir estado actual del juego
        print_state(gameState);

        // Notifica al máster que el estado fue impreso
        sem_post(&semaphores->viewEndedPrinting);

        // Si el juego terminó, termina
        if (gameState->gameOver)
        {
            break;
        }
    }

    // aca falta desmapear la memoria compartida cuando termina el juego
}

// Imprime el estado del juego leyendo la grilla contigua y superponiendo jugadores
void print_state(GameState *gameState)
{
    if (gameState == NULL)
        return;

    unsigned short W = gameState->width;
    unsigned short H = gameState->height;

    printf("\n=== ESTADO DEL JUEGO ===\n");
    printf("Tablero: %ux%u | Jugadores: %u\n", W, H, gameState->playersNumber);

    for (unsigned int y = 0; y < H; y++)
    {
        for (unsigned int x = 0; x < W; x++)
        {
            int mostrado = 0;
            for (unsigned int p = 0; p < gameState->playersNumber; p++)
            {
                Player *pl = &gameState->players[p];
                if (pl->x == x && pl->y == y)
                {
                    printf("P%u ", p + 1);
                    mostrado = 1;
                    break;
                }
            }
            if (!mostrado)
            {
                int v = gameState->grid[y * W + x];
                printf("%2d ", v);
            }
        }
        printf("\n");
    }

    printf("\nJugadores:\n");
    for (unsigned int i = 0; i < gameState->playersNumber; i++)
    {
        Player *pl = &gameState->players[i];
        printf("  %u: %s - Puntaje: %u, Pos: (%u,%u)%s\n",
               i + 1, pl->playerName, pl->score, pl->x, pl->y, pl->blocked ? " [BLOQ]" : "");
    }

    if (gameState->gameOver)
    {
        printf("\n*** JUEGO TERMINADO ***\n");
    }
    printf("=======================\n\n");
}

GameState * connectToSharedMemoryState(unsigned int width, unsigned int height) {
    int gameStateSmFd = shm_open("/game_state", O_RDONLY, 0666);
    if (gameStateSmFd == -1) {
        perror("Error al abrir la memoria compartida para el estado del juego");
        exit(1);
    }

    int map_size=sizeof(GameState) + (size_t)width * height * sizeof(int);

    GameState *gameState = mmap(NULL, map_size, PROT_READ , MAP_SHARED, gameStateSmFd, 0);
    if (gameState == MAP_FAILED) {
        perror("Error al mapear la memoria compartida");
        exit(1);
    }

    return gameState;
}

Semaphores * connectToSharedMemorySemaphores() {
    int semaphoresSmFd = shm_open("/game_sync", O_RDWR, 0666);
    if (semaphoresSmFd == -1) {
        perror("Error al abrir la memoria compartida para los semáforos");
        exit(1);
    }

    Semaphores *semaphores = mmap(NULL, sizeof(Semaphores), PROT_READ | PROT_WRITE, MAP_SHARED, semaphoresSmFd, 0);
    close(semaphoresSmFd);
    if (semaphores == MAP_FAILED) {
        perror("Error al mapear la memoria compartida");
        exit(1);
    }

    return semaphores;
}

