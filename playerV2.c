/*PROBLEMA: como se que numero de jugador soy desde aca? Eso me está impidiendo 
trabajar con las memorias compartidas.*/


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

#define MAX_PLAYERS 9

typedef struct {
    char playerName[16];
    unsigned int score;
    unsigned int invalid;
    unsigned int valid;
    unsigned short x, y;
    pid_t pid;
    bool blocked;
} Player;

typedef struct {
    unsigned short width;
    unsigned short height;
    unsigned int playersNumber;
    Player players[9];
    bool gameOver;
    int **rows;
} GameState;

typedef struct{
    sem_t pendingView;
    sem_t viewEndedPrinting;
    sem_t mutexMasterAccess;
    sem_t mutexGameState;
    sem_t mutexPlayerAccess;
    unsigned int playersReadingState;
    sem_t playerCanMove[9];
} Semaphores;

int main(int argc, char *argv[]) {
    unsigned int width = atoi(argv[1]);
    unsigned int height = atoi(argv[2]);

    //Conexión a las memorias compartidas
    GameState *gameState = connectToSharedMemoryState();
    Semaphores *semaphores = connectToSharedMemorySemaphores();

    //Lectura del GameState y escribo el movimiento que quiero hacer en el fd=1
    //Luego de haber solicitado el movimiento se bloquea hasta que el master lo habilite de nuevo
    //@TODO
    
    
    
}

GameState * connectToSharedMemoryState() {
    int gameStateSmFd = shm_open("/game_state", O_RDWR, 0666);
    if (gameStateSmFd == -1) {
        perror("Error al abrir la memoria compartida para el estado del juego");
        exit(1);
    }

    GameState *gameState = mmap(NULL, sizeof(GameState), PROT_READ | PROT_WRITE, MAP_SHARED, gameStateSmFd, 0);
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
    if (semaphores == MAP_FAILED) {
        perror("Error al mapear la memoria compartida");
        exit(1);
    }

    return semaphores;
}