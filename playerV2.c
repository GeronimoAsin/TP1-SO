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
    GameState *gameState = connectToSharedMemoryState(width, height);
    Semaphores *semaphores = connectToSharedMemorySemaphores();

    //Averiguamos a que indice del arreglo de semaforos corresponde este proceso
    int playerIndex = -1;
    for (int i = 0; i < MAX_PLAYERS && playerIndex == -1; i++) {
        if (gameState->players[i].pid == getpid()) {
            playerIndex = i;
        }
    }

    //Lectura del GameState y escribo el movimiento que quiero hacer en el fd=1
    //Luego de haber solicitado el movimiento se bloquea hasta que el master lo habilite de nuevo
    //@TODO
    while(!gameState->gameOver){
        // Esperar a que el máster habilite este jugador 
        while (sem_wait(&semaphores->playerCanMove[playerIndex]) == -1 && errno == EINTR) {}

        //Valida que durante el bloqueo no se haya terminado el juego
        if (gameState->gameOver) break;

        // Mecanismo Readers-Writers clasico 
        sem_wait(&semaphores->mutexMasterAccess);  // Verifica si el master quiere escribir
        sem_post(&semaphores->mutexMasterAccess);  // Libera el control del master inmediatamente
        
        sem_wait(&semaphores->mutexPlayerAccess);
        if(semaphores->playersReadingState++ == 0){
            sem_wait(&semaphores->mutexGameState); // el primer lector toma el mutex
        }
        sem_post(&semaphores->mutexPlayerAccess);


        unsigned int currentX = gameState->players[playerIndex].x;
        unsigned int currentY = gameState->players[playerIndex].y;
        unsigned char movement = 9;
        unsigned int max = 0;

        // el "<" no incluia la posicion +1 solo permitia moverse en -1 y 0
        for(int i=-1; i<=1; i++){
            for(int j=-1; j<=1; j++)
            {
                if((i != 0 || j != 0) && currentX + i < gameState->height && currentY + j < gameState->width && (int)(currentX + i) >= 0 && (int)(currentY + j) >= 0 && gameState->rows[currentX + i][currentY + j])
                {
                    if(gameState->rows[currentX + i][currentY + j] > max)
                    {
                        max = gameState->rows[currentX + i][currentY + j];
                        if(i == -1 && j == 0){
                            movement = 6;
                        } else if(i == 1 && j == 0){
                            movement = 2;
                        } else if(i == 0 && j == -1){
                            movement = 0;
                        } else if(i == 0 && j == 1){
                            movement = 4;
                        }else if(i == -1 && j == -1){
                            movement = 7;
                        } else if(i == -1 && j == 1 ){
                            movement = 5;
                        } else if(i == 1 && j == -1){
                            movement = 1;
                        } else if(i == 1 && j == 1){
                            movement = 3;
                        }
                    }
                }
            }
        }
        
        // LIBERO mutex del GameState
        sem_wait(&semaphores->mutexPlayerAccess);
        if(--semaphores->playersReadingState == 0){
            sem_post(&semaphores->mutexGameState); // el último lector libera el mutex
        }
        sem_post(&semaphores->mutexPlayerAccess);
        
        // Envio del movimiento al master
        write(1, &movement, sizeof(movement));
    }
    return 0;
}

GameState * connectToSharedMemoryState(unsigned int width, unsigned int height) {
    int gameStateSmFd = shm_open("/game_state", O_RDWR, 0666);
    if (gameStateSmFd == -1) {
        perror("Error al abrir la memoria compartida para el estado del juego");
        exit(1);
    }

    GameState *gameState = mmap(NULL, sizeof(GameState) + (sizeof(int *) * height * width * sizeof(int)), PROT_READ , MAP_SHARED, gameStateSmFd, 0);
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