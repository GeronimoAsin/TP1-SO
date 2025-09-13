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

#define MAX_PLAYERS 9


int main(int argc, char *argv[]) {

  if (argc < 3) {
        fprintf(stderr, "Uso: %s <width> <height>\n", argv[0]);
        return 1;
    }

    unsigned int width = (unsigned int)atoi(argv[1]);
    unsigned int height = (unsigned int)atoi(argv[2]);

    GameState *gameState = connectToSharedMemoryState(width, height);
    Semaphores *semaphores = connectToSharedMemorySemaphores();

    //Averiguamos a que indice del arreglo de semaforos corresponde este proceso
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

    //Lectura del GameState y escribo el movimiento que quiero hacer en el fd=1
    //Luego de haber solicitado el movimiento se bloquea hasta que el master lo habilite de nuevo
    while(!isOver){

        // Esperar a que el máster habilite este jugador 
        while (sem_wait(&semaphores->playerCanMove[playerIndex]) == -1 && errno == EINTR) {}

        // Mecanismo Readers-Writers
        sem_wait(&semaphores->mutexMasterAccess);  // Toma prioridad como "writer"
        sem_post(&semaphores->mutexMasterAccess);  // Libera el control del master inmediatamente

        sem_wait(&semaphores->mutexPlayerAccess);
        if(semaphores->playersReadingState++ == 0){
            sem_wait(&semaphores->mutexGameState); // el primer lector toma el mutex
        }
        sem_post(&semaphores->mutexPlayerAccess);

        // Posición del jugador
        int currentX = (int)gameState->players[playerIndex].x; // columnas
        int currentY = (int)gameState->players[playerIndex].y; // filas

        unsigned int W = gameState->width;
        unsigned int H = gameState->height;
        unsigned char movement = 9; 
        int bestVal = -1;
        
        for(int dy=-1; dy<=1; dy++){
            for(int dx=-1; dx<=1; dx++){
                
                if(dx == 0 && dy == 0) 
                continue; // ignoro la celda actual
                
                int neighborX = currentX + dx;
                int neighborY = currentY + dy;
                
                if (neighborX >= 0 && neighborX < (int)W && neighborY >= 0 && neighborY < (int)H) 
                {
                    int val = gameState->grid[neighborY * W + neighborX];
                    
                    if(val > bestVal){
                        bestVal = val;
                        // Mapeo (dx,dy) -> movimiento correspondiente
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
        
        // LIBERO mutex del GameState
        sem_wait(&semaphores->mutexPlayerAccess);
        if(--semaphores->playersReadingState == 0){
            sem_post(&semaphores->mutexGameState); // el último lector en salir libera el mutex
        }
        sem_post(&semaphores->mutexPlayerAccess);
        
        // Valida fin de juego
        if (gameState->gameOver){
            isOver = true;
        }

        // Envío del movimiento al master
        write(1, &movement, sizeof(movement));

    }
    return 0;
}
