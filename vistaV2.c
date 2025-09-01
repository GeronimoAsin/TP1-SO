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


void print_state (GameState gameState);

int main(int argc, char *argv[]) {
    unsigned int width = atoi(argv[1]);
    unsigned int height = atoi(argv[2]);
    GameState *gameState = connectToSharedMemoryState(width, height);
    Semaphores *semaphores = connectToSharedMemorySemaphores();

    // Esperar cambios en el estado del juego
    while (1) {
        // Espera notificación del máster
        if (sem_wait(&semaphores->pendingView) == -1) {
            perror("vista: sem_wait pendingView");
            break;
        }

        // Imprimir estado actual del juego
        print_state(gameState);

        // Notifica al máster que el estado fue impreso
        sem_post(&semaphores->viewEndedPrinting);

        // Si el juego terminó, termina
        if (gameState->gameOver) {
            break;
        }
    }

    //aca falta desmapear la memoria compartida cuando termina el juego

}