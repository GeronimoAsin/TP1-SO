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

GameState * createSharedMemoryState(unsigned short width, unsigned short height, unsigned int num_players);

int main(int argc, char *argv[]) {
    unsigned int width = 10, height = 10, delay = 200, timeout = 10, seed = time(NULL), num_players;
    char * view = NULL;
    char * players[MAX_PLAYERS] = {0};


    //Lectura de los datos pasados como params
    for(int i = 1; i < argc; i++){
        if(!strcmp(argv[i],"-w") && i + 1 < argc){
            if(isdigit((unsigned char)argv[i + 1][0])){
                width = atoi(argv[i + 1]);
                i++; // Saltar el valor del flag ancho
            }
        }
        else if(!strcmp(argv[i],"-h") && i + 1 < argc){
            if(isdigit((unsigned char)argv[i + 1][0])){
                height = atoi(argv[i + 1]);
                i++; // Saltar el valor del flag alto
            }
        }
        else if(!strcmp(argv[i],"-d") && i + 1 < argc){
            if(isdigit((unsigned char)argv[i + 1][0])){
                delay = atoi(argv[i + 1]);
                i++; // Saltar el valor del flag delay
            }
        }
        else if(!strcmp(argv[i],"-t") && i + 1 < argc){
            if(isdigit((unsigned char)argv[i + 1][0])){
                timeout = atoi(argv[i + 1]);
                i++; // Saltar el valor del flag timeout
            }
        }
        else if(!strcmp(argv[i],"-s") && i + 1 < argc){
            if(isdigit((unsigned char)argv[i + 1][0])){
                seed = atoi(argv[i + 1]);
                i++; // Saltar el valor del flag seed
            }
        }
        else if(!strcmp(argv[i],"-v") && i + 1 < argc){
            view = argv[i + 1];
            i++; // Saltar el valor del flag vista
        }
        else if(!strcmp(argv[i],"-p")){
            num_players = argc - i - 1;
            if(num_players < 1){
                fprintf(stderr, "Debe haber al menos un jugador\n");
                exit(1);
            }
            if(num_players > MAX_PLAYERS){
                fprintf(stderr, "Máximo %d jugadores permitidos\n", MAX_PLAYERS);
                exit(1);
            }
            for(int j=0; j<num_players; j++){
                players[j] = argv[i + j + 1];
            }
            break; 
        }
    }


    //Creación de las memorias compartidas
    GameState *gameState = createSharedMemoryState(width, height, num_players);
    Semaphores * semaphores = createSharedMemorySemaphores(num_players);

    //Creación de los procesos de los jugadores y los correspondientes pipes player->master

	int pipe_player_to_master[num_players][2];


    for (int i=0; i<num_players; i++)
      {

      	pipe(pipe_player_to_master[i]); //para cada jugador genero un pipe

      	if(fork()==0)
          {

          	close(pipe_player_to_master[i][0]); //el fd de lectura del jugador no se usa (player solo escribe)
            dup2(pipe_player_to_master[i][1], 1); //el extremo de escritura del master esta asociado al fd 1 (segun la consigna)

			char *player_argv[]={"./playerV2", width,height};
         	char *envp[] = { NULL };
			execve("./playerV2",player_argv,envp);
          }

          close(pipe_player_to_master[i][1]); //fd de escritura del master no se usa (master solo lee)
      }


    //Empieza el juego: Se habilita de a un jugador a enviar un movimiento. Termina cuando estan
    //todos bloqueados o se alcanza el tiempo de espera
    while (!gameState->gameOver) {

      sem_wait(&semaphores->mutexMasterAccess); //Entra primero el master y toma el control
      sem_wait(&semaphores->mutexGameState);     //bloquea el acceso a gameState de los jugadores
      sem_post(&semaphores->mutexMasterAccess);


      //EJECUTAR MOVIMIENTOS

        for (int i = 0; i < num_players; i++) {
            // Habilitar al jugador para que envíe un movimiento
            sem_post(&semaphores->playerCanMove[i]);            
        }

        //Una vez que se ejecutan los movimientos, se desbloquean los lectores
        sem_post(&semaphores->mutexGameState);

        // Esperar a que todos los jugadores hayan enviado su movimiento
        sem_wait(&semaphores->viewEndedPrinting);
    }
}

GameState * createSharedMemoryState(unsigned short width, unsigned short height, unsigned int num_players) {
    int gameStateSmFd = shm_open("/game_state", O_CREAT | O_RDWR, 0666);
    if (gameStateSmFd == -1) {
        perror("Error al crear la memoria compartida para el estado del juego");
        exit(1);
    }

    // Configurar el tamaño de la memoria compartida
    if (ftruncate(gameStateSmFd, sizeof(GameState) + (sizeof(int *) * height * width * sizeof(int))) == -1) {
        perror("Error al configurar el tamaño de la memoria compartida");
        exit(1);
    }

    GameState *gameState = mmap(NULL, sizeof(GameState) + (sizeof(int *) * height * width * sizeof(int)), PROT_READ | PROT_WRITE, MAP_SHARED, gameStateSmFd, 0);
    if (gameState == MAP_FAILED) {
        perror("Error al mapear la memoria compartida");
        exit(1);
    }

    // Inicializar el estado del juego
    gameState->width = width;
    gameState->height = height;
    gameState->playersNumber = num_players;
    gameState->gameOver = false;


    //Crear los jugadores
    for (int i = 0; i < num_players; i++) {
        sprintf(gameState->players[i].playerName, 16, "Player_%d", i + 1);
        gameState->players[i].x = rand() % width;
        gameState->players[i].y = rand() % height;
        gameState->players[i].score = 0;
    }

    // Crear las filas de la cuadrícula
    gameState->rows = malloc(height * sizeof(int));
    for (int i = 0; i < height; i++) {
        gameState->rows[i] = malloc(width * sizeof(int));
        memset(gameState->rows[i], 0, width * sizeof(int));
    }

    return gameState;
}

Semaphores * createSharedMemorySemaphores(unsigned int num_players) {
    int semaphoresSmFd = shm_open("/game_sync", O_CREAT | O_RDWR, 0666);
    if (semaphoresSmFd == -1) {
        perror("Error al crear la memoria compartida para los semáforos");
        exit(1);
    }

    // Configurar el tamaño de la memoria compartida
    if (ftruncate(semaphoresSmFd, sizeof(Semaphores)) == -1) {
        perror("Error al configurar el tamaño de la memoria compartida");
        exit(1);
    }

    Semaphores *semaphores = mmap(NULL, sizeof(Semaphores), PROT_READ | PROT_WRITE, MAP_SHARED, semaphoresSmFd, 0);
    if (semaphores == MAP_FAILED) {
        perror("Error al mapear la memoria compartida");
        exit(1);
    }

    // Inicializar los semáforos
    sem_init(&semaphores->pendingView, 1, 0);
    sem_init(&semaphores->viewEndedPrinting, 1, 0);
    sem_init(&semaphores->mutexMasterAccess, 1, 1);
    sem_init(&semaphores->mutexGameState, 1, 1);
    sem_init(&semaphores->mutexPlayerAccess, 1, 1);
    semaphores->playersReadingState = 0;
    for (int i = 0; i < num_players; i++) {
        sem_init(&semaphores->playerCanMove[i], 1, 0);
    }

    return semaphores;
}