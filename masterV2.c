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
    int grid[]; // grilla almacenada en memoria compartida (width*height)
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

GameState * createSharedMemoryState(unsigned short width, unsigned short height, unsigned int numPlayers);
Semaphores * createSharedMemorySemaphores(unsigned int numPlayers);

int main(int argc, char *argv[]) {
    unsigned int width = 10, height = 10, delay = 200, timeout = 10, seed = time(NULL), numPlayers;
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
            numPlayers = argc - i - 1;
            if(numPlayers < 1){
                fprintf(stderr, "Debe haber al menos un jugador\n");
                exit(1);
            }
            if(numPlayers > MAX_PLAYERS){
                fprintf(stderr, "Máximo %d jugadores permitidos\n", MAX_PLAYERS);
                exit(1);
            }
            for(unsigned int j=0; j<numPlayers; j++){
                players[j] = argv[i + j + 1];
            }
            break; 
        }
    }


    //Creación de las memorias compartidas
    GameState *gameState = createSharedMemoryState(width, height, numPlayers);
    Semaphores * semaphores = createSharedMemorySemaphores(numPlayers);




    //Creación proceso vista y canal de comunicación vista->master
    int pipeVistaToMaster[2];
    if (pipe(pipeVistaToMaster) == -1) {
        perror("pipe Vista->master");
        exit(1);
    }

    char wbuf[16], hbuf[16];
    snprintf(wbuf, sizeof wbuf, "%u", width);
    snprintf(hbuf, sizeof hbuf, "%u", height);

    pid_t vista_pid = fork();
    if (vista_pid == -1) {
        perror("Error en el fork de la vista\n");
        exit(1);
    }

    if (vista_pid == 0) {
        // Proceso hijo (vista)
        close(pipeVistaToMaster[0]); // Cierro el extremo de lectura
        if (dup2(pipeVistaToMaster[1], STDOUT_FILENO) == -1) {
            perror("dup2 stdout vista");
            exit(1);
        }
        close(pipeVistaToMaster[1]); // Cierro el extremo de escritura duplicado
        char *vista_argv[] = {"./vista", wbuf, hbuf, NULL};
        char *envp[] = { NULL };
        execve("./vista", vista_argv, envp);
        perror("execve vista");
        exit(1);
    }
    close(pipeVistaToMaster[1]);



    //Creación de los procesos de los jugadores y canales de comunicación player->master
	int pipePlayerToMaster[numPlayers][2];


    for (unsigned int i=0; i<numPlayers; i++){
        if (pipe(pipePlayerToMaster[i]) == -1) {
            perror("pipe player->master");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork jugador");
            exit(1);
        }

        if(pid==0){
            // Proceso jugador
            close(pipePlayerToMaster[i][0]); 
            if (dup2(pipePlayerToMaster[i][1], STDOUT_FILENO) == -1) { // jugador escribe al fd 1
                perror("dup2 stdout jugador");
                exit(1);
            }
            close(pipePlayerToMaster[i][1]); // Cerramos descriptor original tras dup2
            char wbuf[16], hbuf[16];
            snprintf(wbuf, sizeof wbuf, "%u", width);
            snprintf(hbuf, sizeof hbuf, "%u", height);
            char *player_argv[] = {"./playerV2", wbuf, hbuf, NULL};
         	char *envp[] = { NULL };
			execve("./playerV2", player_argv, envp);
            perror("execve playerV2");
            exit(1);
        }

        // Proceso máster
        gameState->players[i].pid = pid; // Registrar PID para que el jugador encuentre su índice
        close(pipePlayerToMaster[i][1]); // fd de escritura del master no se usa (master solo lee)
    }





    // Empieza el juego: habilitar a cada jugador a enviar exactamente 1 movimiento por ronda

    const int max_rounds = 3; // límite de rondas para esta implementación mínima
 
    for (int round = 0; round < max_rounds && !gameState->gameOver; round++) {
        
  
        // El master toma prioridad para escribir 
        sem_wait(&semaphores->mutexMasterAccess);  // Toma prioridad como "writer"
        sem_wait(&semaphores->mutexGameState);     // Toma control del mutex de los lectores
        sem_post(&semaphores->mutexMasterAccess); 
        
   
        // Procesar movimientos de todos los jugadores
        for (unsigned int i = 0; i < numPlayers; i++) {
            // Habilitar a un jugador para que envíe un movimiento
            sem_post(&semaphores->playerCanMove[i]);

            // Leer 1 byte (unsigned char) con el movimiento del pipe correspondiente
            unsigned char move = 255;
            ssize_t n = read(pipePlayerToMaster[i][0], &move, sizeof(move));
            if (n == 0) {
                // EOF: marcar bloqueado
                gameState->players[i].blocked = true;
            } else if (n < 0) {
                perror("master: read movimiento");
            } else {
                // Movimiento recibido
                // printf("Recibido movimiento de jugador %d: %u\n", i, (unsigned)move);
            }
        }
        
        // Liberar el mutex para que los jugadores puedan leer
        sem_post(&semaphores->mutexGameState);
        
    }

    gameState->gameOver = true;
    return 0;
}

GameState * createSharedMemoryState(unsigned short width, unsigned short height, unsigned int numPlayers) {
    int gameStateSmFd = shm_open("/game_state", O_CREAT | O_RDWR, 0666);
    if (gameStateSmFd == -1) {
        perror("Error al crear la memoria compartida para el estado del juego");
        exit(1);
    }

    // Configurar el tamaño de la memoria compartida
    size_t grid_size = (size_t)width * (size_t)height * sizeof(int);
    if (ftruncate(gameStateSmFd, sizeof(GameState) + grid_size) == -1) {
        perror("Error al configurar el tamaño de la memoria compartida");
        exit(1);
    }

    GameState *gameState = mmap(NULL, sizeof(GameState) + grid_size, PROT_READ | PROT_WRITE, MAP_SHARED, gameStateSmFd, 0);
    if (gameState == MAP_FAILED) {
        perror("Error al mapear la memoria compartida");
        exit(1);
    }

    // Inicializar el estado del juego
    gameState->width = width;
    gameState->height = height;
    gameState->playersNumber = numPlayers;
    gameState->gameOver = false;


    //Crear los jugadores
    for (unsigned int i = 0; i < numPlayers; i++) {
        snprintf(gameState->players[i].playerName, sizeof(gameState->players[i].playerName), "P%d", i + 1);
        gameState->players[i].x = rand() % width;
        gameState->players[i].y = rand() % height;
        gameState->players[i].score = 0;
    }

    // Inicialización grilla en memoria compartida
    int *cells = gameState->grid;
    for (unsigned int row = 0; row < height; row++) {
        for (unsigned int col = 0; col < width; col++) {
            cells[row * width + col] = 0;
        }
    }

    return gameState;
}

Semaphores * createSharedMemorySemaphores(unsigned int numPlayers) {
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
    for (unsigned int i = 0; i < numPlayers; i++) {
        sem_init(&semaphores->playerCanMove[i], 1, 0);
    }

    return semaphores;
}