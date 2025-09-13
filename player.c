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


static inline void acquireGameStatePlayerLock(Semaphores *semaphore);
static inline void releaseGameStatePlayerLock(Semaphores *semaphore);


int main(int argc, char *argv[]) {

  if (argc < 3) {
        fprintf(stderr, "Uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    // Dimensiones para calcular el tamaño del mapeo de memoria.
    unsigned int width = (unsigned int)atoi(argv[1]);
    unsigned int height = (unsigned int)atoi(argv[2]);

    GameState *gameState = connectToSharedMemoryState(width, height);
    Semaphores *semaphores = connectToSharedMemorySemaphores();

    //Determinación del indice del arreglo de semaforos correspondiente al jugador actual
    int playerIndex = -1;
    for (int i = 0; i < MAX_PLAYERS && playerIndex == -1; i++) {
        if (gameState->players[i].pid == getpid()) {
            playerIndex = i;
        }
    }
    if (playerIndex == -1) {
        fprintf(stderr, "No se encontró el índice del jugador para el PID actual\n");
        return 1;
    }

    bool isOver = false;



    while(!isOver){

        // Espera a que el máster habilite este jugador
        while (sem_wait(&semaphores->playerCanMove[playerIndex]) == -1 && errno == EINTR) {}

        // Adquirir mutexGameState
        acquireGameStatePlayerLock(semaphores);

        int currentX = (int)gameState->players[playerIndex].x; // columnas
        int currentY = (int)gameState->players[playerIndex].y; // filas


        unsigned int W = gameState->width;
        unsigned int H = gameState->height;
        unsigned char movement = 9; 
        int bestVal = -1;
        

        for(int dy=-1; dy<=1; dy++){
            for(int dx=-1; dx<=1; dx++){
                
                if(dx == 0 && dy == 0) 
                continue; // ignorar la celda actual
                
                int neighborX = currentX + dx;
                int neighborY = currentY + dy;
                
                if (neighborX >= 0 && neighborX < (int)W && neighborY >= 0 && neighborY < (int)H) 
                {
                    int val = gameState->grid[neighborY * W + neighborX];
                    
                    if(val > bestVal){
                        bestVal = val;
                        // Conversion de (dx,dy) al movimiento del jugador
                        if(dx == 0 && dy == -1){ movement = 0; }           // arriba
                        else if(dx == 1 && dy == -1){ movement = 1; }      // arriba-derecha
                        else if(dx == 1 && dy == 0){ movement = 2; }       // derecha
                        else if(dx == 1 && dy == 1){ movement = 3; }       // abajo-derecha
                        else if(dx == 0 && dy == 1){ movement = 4; }       // abajo
                        else if(dx == -1 && dy == 1){ movement = 5; }      // abajo-izquierda
                        else if(dx == -1 && dy == 0){ movement = 6; }      // izquierda
                        else if(dx == -1 && dy == -1){ movement = 7; }     // arriba-izquierda
                    }
                }
            }
        }

        // Liberar mutexGameState
        releaseGameStatePlayerLock(semaphores);

        // Validación fin de juego
        if (gameState->gameOver){
            isOver = true;
        }


        // Enviar movimiento al master
        write(1, &movement, sizeof(movement));

    }
    return 0;
}

static inline void acquireGameStatePlayerLock(Semaphores *semaphore)
{
    sem_wait(&semaphore->mutexMasterAccess);  // Espera si el master esta escribiendo
    sem_post(&semaphore->mutexMasterAccess);  // De lo contrario, libera el control del master inmediatamente

    sem_wait(&semaphore->mutexPlayerAccess);
    if (semaphore->playersReadingState++ == 0) {
        sem_wait(&semaphore->mutexGameState); // el primer lector toma el mutex
    }
    sem_post(&semaphore->mutexPlayerAccess);
}

static inline void releaseGameStatePlayerLock(Semaphores *semaphore)
{
    sem_wait(&semaphore->mutexPlayerAccess);
    if (--semaphore->playersReadingState == 0) {
        sem_post(&semaphore->mutexGameState); // el último lector en salir libera el mutex
    }
    sem_post(&semaphore->mutexPlayerAccess);
}
