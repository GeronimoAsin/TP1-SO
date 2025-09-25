// This is a personal academic project. Dear PVS-Studio, please check it.
// PVS-Studio Static Code Analyzer for C, C++ and C#: http://www.viva64.com
#include "estructuras.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/select.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>
#include <semaphore.h>
#include <errno.h>
#include <sys/wait.h>
#include <signal.h>

GameState *createSharedMemoryState(unsigned short width, unsigned short height, unsigned int numPlayers);
Semaphores *createSharedMemorySemaphores(unsigned int numPlayers);
void cleanup_resources(unsigned int width, unsigned int height, unsigned int numPlayers, GameState *gameState, Semaphores *semaphores);
void signal_handler(int sig);
void masterEnters(Semaphores *semaphores);
void masterLeaves(Semaphores *semaphores);

// Variables globales para cleanup en señales
static GameState *g_gameState = NULL;
static Semaphores *g_semaphores = NULL;
static unsigned int g_width = 0, g_height = 0, g_numPlayers = 0;

void sleep_ms(int delay)
{
    struct timespec ts;
    ts.tv_sec = delay / 1000;               // parte entera en segundos
    ts.tv_nsec = (delay % 1000) * 1000000L; // resto en nanosegundos
    nanosleep(&ts, NULL);
}

int main(int argc, char *argv[])
{
    unsigned int width = 10, height = 10, delay = 200, timeout = 10, seed = time(NULL), numPlayers = 0;
    char *view = NULL;
    char *players[MAX_PLAYERS] = {0};

    // Validación parámetros mínimos
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s [-w width] [-h height] [-d delay] [-t timeout] [-s seed] [-v view] -p player1 [player2 ...]\n", argv[0]);
        exit(1);
    }

    // Lectura de los datos pasados como params
    for (int i = 1; i < argc; i++)
    {
        if (!strcmp(argv[i], "-w") && i + 1 < argc)
        {
            width = atoi(argv[i + 1]);
            if (width < 10)
                width = 10;
            i++;
        }
        else if (!strcmp(argv[i], "-h") && i + 1 < argc)
        {
            height = atoi(argv[i + 1]);
            if (height < 10)
                height = 10;
            i++;
        }
        else if (!strcmp(argv[i], "-d") && i + 1 < argc)
        {
            delay = atoi(argv[i + 1]);
            i++;
        }
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
        {
            timeout = atoi(argv[i + 1]);
            i++;
        }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
        {
            seed = atoi(argv[i + 1]);
            i++;
        }
        else if (!strcmp(argv[i], "-v") && i + 1 < argc)
        {
            view = argv[i + 1];
            i++;
        }
        else if (!strcmp(argv[i], "-p"))
        {
            numPlayers = argc - i - 1;
            if (numPlayers < 1)
            {
                fprintf(stderr, "Debe haber al menos un jugador\n");
                exit(1);
            }
            if (numPlayers > MAX_PLAYERS)
            {
                fprintf(stderr, "Máximo %d jugadores permitidos\n", MAX_PLAYERS);
                exit(1);
            }
            for (unsigned int j = 0; j < numPlayers; j++)
            {
                players[j] = argv[i + j + 1];
            }
            break;
        }
    }

    if (numPlayers == 0)
    {
        fprintf(stderr, "Error: Se requiere al menos un jugador con -p\n");
        exit(1);
    }

    // Establecimiento de la semilla para números aleatorios
    srand(seed);

    // Configuración de manejador de señales para limpieza
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Creación de las memorias compartidas
    GameState *gameState = createSharedMemoryState(width, height, numPlayers);
    Semaphores *semaphores = createSharedMemorySemaphores(numPlayers);

    // Configuración de variables globales para cleanup en señales
    g_gameState = gameState;
    g_semaphores = semaphores;
    g_width = width;
    g_height = height;
    g_numPlayers = numPlayers;

    // Variables para tracking de procesos hijos
    pid_t vista_pid = -1;
    pid_t player_pids[numPlayers];
    for (int i = 0; i < numPlayers; i++)
    {
        player_pids[i] = -1;
    }

    // Creación proceso vista
    if (view != NULL)
    {


        char wbuf[16], hbuf[16];
        snprintf(wbuf, sizeof(wbuf), "%u", width);
        snprintf(hbuf, sizeof(hbuf), "%u", height);

        vista_pid = fork();
        if (vista_pid == -1)
        {
            perror("Error en el fork de la vista\n");
            exit(1);
        }

        if (vista_pid == 0)
        {
            char *vista_argv[] = {view, wbuf, hbuf, NULL};
            char *envp[] = {getenv("TERM"), NULL};
            execve(view, vista_argv, envp);
            perror("execve vista");
            exit(1);
        }
    }

    // Creación de los procesos de los jugadores y canales de comunicación player->master
    int pipePlayerToMaster[numPlayers][2];

    for (unsigned int i = 0; i < numPlayers; i++)
    {
        if (pipe(pipePlayerToMaster[i]) == -1)
        {
            perror("pipe player->master");
            exit(1);
        }

        pid_t pid = fork();
        if (pid == -1)
        {
            perror("fork jugador");
            exit(1);
        }

        if (pid == 0)
        {
            // Proceso jugador
            for (unsigned int j = 0; j < i; j++)
            {
                close(pipePlayerToMaster[j][0]);
                close(pipePlayerToMaster[j][1]);
            }

            if (dup2(pipePlayerToMaster[i][1], STDOUT_FILENO) == -1)
            { // jugador escribe al fd 1
                perror("dup2 stdout jugador");
                exit(1);
            }
            close(pipePlayerToMaster[i][1]); // Cerramos descriptor original tras dup2
            char wbuf[16], hbuf[16];
            snprintf(wbuf, sizeof wbuf, "%u", width);
            snprintf(hbuf, sizeof hbuf, "%u", height);
            char *player_argv[] = {players[i], wbuf, hbuf, NULL};
            char *envp[] = {NULL};
            execve(players[i], player_argv, envp);
            fprintf(stderr, "execve player '%s': %s\n", players[i], strerror(errno));
            exit(1);
        }

        // Proceso máster
        gameState->players[i].pid = pid;
        player_pids[i] = pid;
        close(pipePlayerToMaster[i][1]);
    }

    // Impresión del estado inicial (en caso de tener vista)
    if (view != NULL)
    {
        sem_post(&semaphores->pendingView);
        sem_wait(&semaphores->viewEndedPrinting);
        sleep_ms(delay);
    }

    // Lógica principal del juego con select()
    time_t lastValidMove = time(NULL);

    while (1)
    {

        // Verificación timeout
        if ((unsigned int)(time(NULL) - lastValidMove) >= timeout)
        {
            break; // Si se supera el timeout, termina el juego
        }

        // Verificación de si todos los jugadores están bloqueados
        bool anyPlayerActive = false;
        for (unsigned int i = 0; i < numPlayers && !anyPlayerActive; i++)
        {
            if (!gameState->players[i].blocked)
            {
                anyPlayerActive = true;
            }
        }

        if (!anyPlayerActive)
        {
            break; // Si todos estan bloqueados, termina el juego
        }

        // Habilitación a todos los jugadores activos para que puedan moverse
        for (unsigned int i = 0; i < numPlayers; i++)
        {
            if (!gameState->players[i].blocked)
            {
                sem_post(&semaphores->playerCanMove[i]);
            }
        }

        // Configuración de fd_set para select()
        fd_set readfds;
        FD_ZERO(&readfds);
        int maxfd = -1;

        for (unsigned int i = 0; i < numPlayers; i++)
        {
            if (!gameState->players[i].blocked)
            {
                FD_SET(pipePlayerToMaster[i][0], &readfds);
                if (pipePlayerToMaster[i][0] > maxfd)
                {
                    maxfd = pipePlayerToMaster[i][0];
                }
            }
        }

        if (maxfd == -1)
        {
            // No hay jugadores activos
            continue;
        }

        // Uso de select() para esperar movimientos de cualquier jugador
        struct timeval selectTimeout;
        selectTimeout.tv_sec = 1; // 1 segundo de timeout para el select
        selectTimeout.tv_usec = 0;

        int selectResult = select(maxfd + 1, &readfds, NULL, NULL, &selectTimeout);

        if (selectResult == -1)
        {
            perror("select failed");
            break;
        }
        else if (selectResult == 0)
        {
            // Timeout del select, continua el bucle
            continue;
        }

        // Procesamiento de movimientos de todos los jugadores que tienen datos listos
        bool anyValidMove = false;

        for (unsigned int i = 0; i < numPlayers; i++)
        {
            if (!gameState->players[i].blocked && FD_ISSET(pipePlayerToMaster[i][0], &readfds))
            {
                unsigned char movement;
                int bytesRead = read(pipePlayerToMaster[i][0], &movement, 1);
                if (bytesRead == -1)
                {
                    perror("read jugador");
                    continue;
                }
                else if (bytesRead == 0)
                {
                    gameState->players[i].blocked = true;
                    continue;
                }

                // Procesamiento del movimiento
                masterEnters(semaphores);

                int currentX = gameState->players[i].x;
                int currentY = gameState->players[i].y;
                int newX = currentX, newY = currentY;

                switch (movement)
                {
                case 0:
                    newY--;
                    break; // arriba
                case 1:
                    newX++;
                    newY--;
                    break; // arriba-derecha
                case 2:
                    newX++;
                    break; // derecha
                case 3:
                    newX++;
                    newY++;
                    break; // abajo-derecha
                case 4:
                    newY++;
                    break; // abajo
                case 5:
                    newX--;
                    newY++;
                    break; // abajo-izquierda
                case 6:
                    newX--;
                    break; // izquierda
                case 7:
                    newX--;
                    newY--;
                    break; // arriba-izquierda
                default:   /* movimiento inválido */
                    break;
                }

                if (newX >= 0 && newY >= 0 &&
                    (unsigned int)newX < width && (unsigned int)newY < height &&
                    gameState->grid[(unsigned int)newY * width + (unsigned int)newX] > 0)
                {
                    // Movimiento válido
                    gameState->players[i].score +=
                        gameState->grid[(unsigned int)newY * width + (unsigned int)newX];
                    gameState->players[i].valid++;
                    // marca celda visitada por el jugador con -(index+1)
                    gameState->grid[(unsigned int)newY * width + (unsigned int)newX] = -(int)i;
                    gameState->players[i].x = (unsigned short)newX;
                    gameState->players[i].y = (unsigned short)newY;

                    anyValidMove = true;

                    // Verificación de si el jugador quedó bloqueado (sin movimientos válidos)
                    bool hasValidMoves = false;
                    for (int dy = -1; dy <= 1 && !hasValidMoves; dy++)
                    {
                        for (int dx = -1; dx <= 1 && !hasValidMoves; dx++)
                        {
                            if (dx == 0 && dy == 0)
                                continue;
                            int checkX = newX + dx;
                            int checkY = newY + dy;
                            if (checkX >= 0 && checkY >= 0 &&
                                (unsigned int)checkX < width && (unsigned int)checkY < height &&
                                gameState->grid[(unsigned int)checkY * width + (unsigned int)checkX] > 0)
                            {
                                hasValidMoves = true;
                            }
                        }
                    }
                    gameState->players[i].blocked = !hasValidMoves;
                }
                else
                {
                    // Movimiento inválido (puede ser porque otro jugador ya tomó esa celda)
                    gameState->players[i].invalid++;
                }
                masterLeaves(semaphores);
            }
        }

        // Actualización del tiempo desde el último movimiento válido
        if (anyValidMove)
        {
            lastValidMove = time(NULL);
        }

        // Verificación de bloqueo o no de todos los jugadores después de procesar movimientos
        masterEnters(semaphores);
        for (unsigned int i = 0; i < numPlayers; i++)
        {
            if (!gameState->players[i].blocked)
            {
                bool hasValidMoves = false;
                int currentX = gameState->players[i].x;
                int currentY = gameState->players[i].y;

                for (int dy = -1; dy <= 1 && !hasValidMoves; dy++)
                {
                    for (int dx = -1; dx <= 1 && !hasValidMoves; dx++)
                    {
                        if (dx == 0 && dy == 0)
                            continue;
                        int checkX = currentX + dx;
                        int checkY = currentY + dy;
                        if (checkX >= 0 && checkY >= 0 &&
                            (unsigned int)checkX < width && (unsigned int)checkY < height &&
                            gameState->grid[(unsigned int)checkY * width + (unsigned int)checkX] > 0)
                        {
                            hasValidMoves = true;
                        }
                    }
                }

                if (!hasValidMoves)
                {
                    gameState->players[i].blocked = true;
                }
            }
        }
        masterLeaves(semaphores);

        // Notificación a la vista (si hay una y hubo algún movimiento válido)
        if (view != NULL && anyValidMove)
        {
            sem_post(&semaphores->pendingView);
            sem_wait(&semaphores->viewEndedPrinting);
            sleep_ms(delay);
        }
    }

    // Marcado del fin del juego
    masterEnters(semaphores);
    gameState->gameOver = true;
    masterLeaves(semaphores);

    // Notificación a la vista del final (si existe)
    if (view != NULL)
    {
        sem_post(&semaphores->pendingView);
        sem_wait(&semaphores->viewEndedPrinting);
    }

    // Habilitación a todos los jugadores para que puedan terminar
    for (unsigned int i = 0; i < numPlayers; i++)
    {
        sem_post(&semaphores->playerCanMove[i]);
    }

    // Una vez que terminan los procesos hijos, se imprimen los resultados finales
    printf("\n=== RESULTADOS FINALES ===\n");

    for (unsigned int i = 0; i < numPlayers; i++)
    {
        if (player_pids[i] != -1)
        {
            int status;
            pid_t result = waitpid(player_pids[i], &status, 0);
            if (result == player_pids[i])
            {
                if (WIFEXITED(status))
                {
                    printf("Jugador %d (%s): Puntaje %u, Validos %u, Invalidos %u, Salió con código %d\n",
                           i + 1, gameState->players[i].playerName,
                           gameState->players[i].score, gameState->players[i].valid,
                           gameState->players[i].invalid, WEXITSTATUS(status));
                }
                else if (WIFSIGNALED(status))
                {
                    printf("Jugador %d (%s): Puntaje %u, Validos %u, Invalidos %u, Terminado por señal %d\n",
                           i + 1, gameState->players[i].playerName,
                           gameState->players[i].score, gameState->players[i].valid,
                           gameState->players[i].invalid, WTERMSIG(status));
                }
            }
        }
        if (pipePlayerToMaster[i][0] != -1)
        {
            close(pipePlayerToMaster[i][0]);
        }
    }

    if (vista_pid != -1)
    {
        int status;
        pid_t result = waitpid(vista_pid, &status, 0);
        if (result == vista_pid)
        {
            if (WIFEXITED(status))
            {
                printf("Vista: Salió con código %d\n", WEXITSTATUS(status));
            }
            else if (WIFSIGNALED(status))
            {
                printf("Vista: Terminada por señal %d\n", WTERMSIG(status));
            }
        }

    }

    printf("========================\n");

    // Limpieza de memoria compartida y semáforos
    cleanup_resources(width, height, numPlayers, gameState, semaphores);

    return 0;
}

GameState *createSharedMemoryState(unsigned short width, unsigned short height, unsigned int numPlayers)
{
    // Desacopla memorias compartidas anteriores
    shm_unlink("/game_state");

    int gameStateSmFd = shm_open("/game_state", O_CREAT | O_RDWR, 0666);
    if (gameStateSmFd == -1)
    {
        perror("Error al crear la memoria compartida para el estado del juego");
        exit(1);
    }

    // Configuración del tamaño de la memoria compartida
    size_t grid_size = (size_t)width * (size_t)height * sizeof(int);
    if (ftruncate(gameStateSmFd, sizeof(GameState) + grid_size) == -1)
    {
        perror("Error al configurar el tamaño de la memoria compartida");
        exit(1);
    }

    GameState *gameState = mmap(NULL, sizeof(GameState) + grid_size, PROT_READ | PROT_WRITE, MAP_SHARED, gameStateSmFd, 0);
    if (gameState == MAP_FAILED)
    {
        perror("Error al mapear la memoria compartida");
        close(gameStateSmFd);
        exit(1);
    }

    close(gameStateSmFd);

    // Inicialización del estado del juego
    gameState->width = width;
    gameState->height = height;
    gameState->playersNumber = numPlayers;
    gameState->gameOver = false;

    // Inicialización grilla con valores aleatorios
    int *cells = gameState->grid;
    for (unsigned int row = 0; row < height; row++)
    {
        for (unsigned int col = 0; col < width; col++)
        {
            cells[row * width + col] = (rand() % 9) + 1; // Valores entre 1 y 9
        }
    }

    // Distribución determinística de jugadores
    // Distribución en las esquinas y bordes para dar margen de movimiento similar
    unsigned int positions[][2] = {
        {0, 0},                  // esquina superior izquierda
        {width - 1, 0},          // esquina superior derecha
        {0, height - 1},         // esquina inferior izquierda
        {width - 1, height - 1}, // esquina inferior derecha
        {width / 2, 0},          // centro superior
        {width / 2, height - 1}, // centro inferior
        {0, height / 2},         // centro izquierda
        {width - 1, height / 2}, // centro derecha
        {width / 2, height / 2}  // centro del tablero
    };

    for (unsigned int i = 0; i < numPlayers; i++)
    {
        snprintf(gameState->players[i].playerName, sizeof(gameState->players[i].playerName), "P%u", i + 1);
        gameState->players[i].x = positions[i][0];
        gameState->players[i].y = positions[i][1];
        gameState->players[i].score = 0;
        gameState->players[i].invalid = 0;
        gameState->players[i].valid = 0;
        gameState->players[i].pid = 0;
        gameState->players[i].blocked = false;
        unsigned int pos = gameState->players[i].y * width + gameState->players[i].x;
        gameState->players[i].score += cells[pos];
        cells[pos] = -(int)i;
    }

    return gameState;
}

Semaphores *createSharedMemorySemaphores(unsigned int numPlayers)
{
    // Desacopla memorias compartidas anteriores
    shm_unlink("/game_sync");

    int semaphoresSmFd = shm_open("/game_sync", O_CREAT | O_RDWR, 0666);
    if (semaphoresSmFd == -1)
    {
        perror("Error al crear la memoria compartida para los semáforos");
        exit(1);
    }

    // Configuración del tamaño de la memoria compartida
    if (ftruncate(semaphoresSmFd, sizeof(Semaphores)) == -1)
    {
        perror("Error al configurar el tamaño de la memoria compartida");
        exit(1);
    }

    Semaphores *semaphores = mmap(NULL, sizeof(Semaphores), PROT_READ | PROT_WRITE, MAP_SHARED, semaphoresSmFd, 0);
    if (semaphores == MAP_FAILED)
    {
        perror("Error al mapear la memoria compartida");
        close(semaphoresSmFd);
        exit(1);
    }

    close(semaphoresSmFd);

    // Inicialización de los semáforos
    sem_init(&semaphores->pendingView, 1, 0);
    sem_init(&semaphores->viewEndedPrinting, 1, 0);
    sem_init(&semaphores->mutexMasterAccess, 1, 1);
    sem_init(&semaphores->mutexGameState, 1, 1);
    sem_init(&semaphores->mutexPlayerAccess, 1, 1);
    semaphores->playersReadingState = 0;
    for (unsigned int i = 0; i < numPlayers; i++)
    {
        sem_init(&semaphores->playerCanMove[i], 1, 0);
    }

    return semaphores;
}

void cleanup_resources(unsigned int width, unsigned int height, unsigned int numPlayers, GameState *gameState, Semaphores *semaphores)
{
    if (semaphores != NULL)
    {
        sem_destroy(&semaphores->pendingView);
        sem_destroy(&semaphores->viewEndedPrinting);
        sem_destroy(&semaphores->mutexMasterAccess);
        sem_destroy(&semaphores->mutexGameState);
        sem_destroy(&semaphores->mutexPlayerAccess);
        for (unsigned int i = 0; i < numPlayers; i++)
        {
            sem_destroy(&semaphores->playerCanMove[i]);
        }

        if (munmap(semaphores, sizeof(Semaphores)) == -1)
        {
            perror("Error al desmapear memoria compartida de semáforos");
        }
    }

    if (gameState != NULL)
    {
        size_t grid_size = (size_t)width * (size_t)height * sizeof(int);
        if (munmap(gameState, sizeof(GameState) + grid_size) == -1)
        {
            perror("Error al desmapear memoria compartida del estado del juego");
        }
    }

    shm_unlink("/game_state");
    shm_unlink("/game_sync");
}

void signal_handler(int sig)
{
    cleanup_resources(g_width, g_height, g_numPlayers, g_gameState, g_semaphores);
    exit(sig);
}

void masterEnters(Semaphores *semaphores)
{
    sem_wait(&semaphores->mutexMasterAccess);
    sem_wait(&semaphores->mutexGameState);
    sem_post(&semaphores->mutexMasterAccess);
}

void masterLeaves(Semaphores *semaphores)
{
    sem_post(&semaphores->mutexGameState);
}
