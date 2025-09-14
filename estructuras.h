#ifndef ESTRUCTURAS_H_
#define ESTRUCTURAS_H_
#include <semaphore.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#define MAX_PLAYERS 9

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
    int grid[]; // grilla almacenada en memoria compartida en formato arreglo width*height
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


static inline GameState * connectToSharedMemoryState(unsigned int width, unsigned int height) {
    int gameStateSmFd = shm_open("/game_state", O_RDONLY, 0666);
    if (gameStateSmFd == -1) {
        fprintf(stderr, "Error al abrir la memoria compartida para el estado del juego: errno=%d (%s)\n", errno, strerror(errno));
        exit(1);
    }

    size_t map_size = sizeof(GameState) + (size_t)width * height * sizeof(int);

    GameState *gameState = mmap(NULL, map_size, PROT_READ , MAP_SHARED, gameStateSmFd, 0);
    if (gameState == MAP_FAILED) {
        fprintf(stderr, "Error al mapear la memoria compartida: errno=%d (%s)\n", errno, strerror(errno));
        close(gameStateSmFd);
        exit(1);
    }

    if (gameStateSmFd > STDERR_FILENO) close(gameStateSmFd);

    return gameState;
}

static inline Semaphores * connectToSharedMemorySemaphores(void) {
    int semaphoresSmFd = shm_open("/game_sync", O_RDWR, 0666);
    if (semaphoresSmFd == -1) {
        fprintf(stderr, "Error al abrir la memoria compartida para los semáforos: errno=%d (%s)\n", errno, strerror(errno));
        exit(1);
    }

    Semaphores *semaphores = mmap(NULL, sizeof(Semaphores), PROT_READ | PROT_WRITE, MAP_SHARED, semaphoresSmFd, 0);
    if (semaphores == MAP_FAILED) {
        fprintf(stderr, "Error al mapear la memoria compartida de semáforos: errno=%d (%s)\n", errno, strerror(errno));
        if (semaphoresSmFd > STDERR_FILENO) close(semaphoresSmFd);
        exit(1);
    }

    if (semaphoresSmFd > STDERR_FILENO) close(semaphoresSmFd);

    return semaphores;
}



#endif
