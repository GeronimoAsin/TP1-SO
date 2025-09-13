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
    int grid[]; // grilla almacenada en memoria compartida (width*height)
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

GameState *createSharedMemoryState(unsigned short width, unsigned short height, unsigned int numPlayers);
Semaphores *createSharedMemorySemaphores(unsigned int numPlayers);
void cleanup_resources(unsigned int width, unsigned int height, unsigned int numPlayers, GameState *gameState, Semaphores *semaphores);
void signal_handler(int sig);

// Variables globales para cleanup en señales
static GameState *g_gameState = NULL;
static Semaphores *g_semaphores = NULL;
static unsigned int g_width = 0, g_height = 0, g_numPlayers = 0;

int main(int argc, char *argv[])
{
    unsigned int width = 10, height = 10, delay = 200, timeout = 10, seed = time(NULL), numPlayers = 0;
    char *view = NULL;
    char *players[MAX_PLAYERS] = {0};

    // Validar parámetros mínimos
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
                width = 10; // Aplicar mínimo
            i++;            // Saltar el valor del flag ancho
        }
        else if (!strcmp(argv[i], "-h") && i + 1 < argc)
        {
            height = atoi(argv[i + 1]);
            if (height < 10)
                height = 10; // Aplicar mínimo
            i++;             // Saltar el valor del flag alto
        }
        else if (!strcmp(argv[i], "-d") && i + 1 < argc)
        {
            delay = atoi(argv[i + 1]);
            i++; // Saltar el valor del flag delay
        }
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
        {
            timeout = atoi(argv[i + 1]);
            i++; // Saltar el valor del flag timeout
        }
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
        {
            seed = atoi(argv[i + 1]);
            i++; // Saltar el valor del flag seed
        }
        else if (!strcmp(argv[i], "-v") && i + 1 < argc)
        {
            view = argv[i + 1];
            i++; // Saltar el valor del flag vista
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

    // Verificar que los archivos de jugadores existen
    for (unsigned int i = 0; i < numPlayers; i++)
    {
        if (access(players[i], X_OK) != 0)
        {
            fprintf(stderr, "Error: No se puede acceder al archivo del jugador '%s'\n", players[i]);
            exit(1);
        }
    }

    // Verificar que el archivo de vista existe (si se especifica)
    if (view != NULL && access(view, X_OK) != 0)
    {
        fprintf(stderr, "Error: No se puede acceder al archivo de vista '%s'\n", view);
        exit(1);
    }

    // Establecer la semilla para reproducibilidad
    srand(seed);

    // Configurar manejador de señales para limpieza
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Creación de las memorias compartidas
    GameState *gameState = createSharedMemoryState(width, height, numPlayers);
    Semaphores *semaphores = createSharedMemorySemaphores(numPlayers);

    // Configurar variables globales para cleanup en señales
    g_gameState = gameState;
    g_semaphores = semaphores;
    g_width = width;
    g_height = height;
    g_numPlayers = numPlayers;

    // Variables para tracking de procesos hijos
    pid_t vista_pid = -1;
    pid_t player_pids[MAX_PLAYERS];
    for (int i = 0; i < MAX_PLAYERS; i++)
    {
        player_pids[i] = -1;
    }

    // Creación proceso vista y canal de comunicación vista->master (si se especifica vista)
    int pipeVistaToMaster[2] = {-1, -1};
    if (view != NULL)
    {
        if (pipe(pipeVistaToMaster) == -1)
        {
            perror("pipe Vista->master");
            exit(1);
        }

        char wbuf[16], hbuf[16];
        snprintf(wbuf, sizeof wbuf, "%u", width);
        snprintf(hbuf, sizeof hbuf, "%u", height);

        vista_pid = fork();
        if (vista_pid == -1)
        {
            perror("Error en el fork de la vista\n");
            exit(1);
        }

        if (vista_pid == 0)
        {
            // Proceso hijo (vista)
            close(pipeVistaToMaster[0]); // Cierro el extremo de lectura
            if (dup2(pipeVistaToMaster[1], STDOUT_FILENO) == -1)
            {
                perror("dup2 stdout vista");
                exit(1);
            }
            close(pipeVistaToMaster[1]); // Cierro el extremo de escritura duplicado
            char *vista_argv[] = {view, wbuf, hbuf, NULL};
            // Configurar variable de entorno TERM para ncurses
            char *envp[] = {"TERM=xterm-256color", NULL};
            execve(view, vista_argv, envp);
            perror("execve vista");
            exit(1);
        }
        close(pipeVistaToMaster[1]);
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
            close(pipePlayerToMaster[i][0]);
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
        gameState->players[i].pid = pid; // Registrar PID para que el jugador encuentre su índice
        player_pids[i] = pid;            // Guardar PID para wait posterior
        close(pipePlayerToMaster[i][1]); // fd de escritura del master no se usa (master solo lee)
    }

    // Notificar estado inicial a la vista si existe
    if (view != NULL)
    {
        sem_post(&semaphores->pendingView);
        sem_wait(&semaphores->viewEndedPrinting);
        usleep(delay * 1000); // delay en microsegundos
    }

    // Lógica principal del juego con round-robin
    time_t lastValidMove = time(NULL);
    bool gameOver = false;
    unsigned int currentPlayerIndex = 0; // Para round-robin

    while (!gameOver)
    {
        // Verificar timeout
        if ((unsigned int)(time(NULL) - lastValidMove) >= timeout)
        {
            printf("Timeout alcanzado. Finalizando juego.\n");
            break;
        }

        // Verificar si todos los jugadores están bloqueados
        bool anyPlayerActive = false;
        for (unsigned int i = 0; i < numPlayers; i++)
        {
            if (!gameState->players[i].blocked)
            {
                anyPlayerActive = true;
                break;
            }
        }

        if (!anyPlayerActive)
        {
            printf("Todos los jugadores están bloqueados. Finalizando juego.\n");
            break;
        }

        // Buscar el siguiente jugador activo usando round-robin
        unsigned int playersChecked = 0;
        bool foundPlayer = false;

        while (playersChecked < numPlayers && !foundPlayer)
        {
            if (!gameState->players[currentPlayerIndex].blocked)
            {
                foundPlayer = true;
            }
            else
            {
                currentPlayerIndex = (currentPlayerIndex + 1) % numPlayers;
                playersChecked++;
            }
        }

        if (!foundPlayer)
        {
            // Esto no debería pasar dado el check anterior, pero por seguridad
            printf("No se encontraron jugadores activos. Finalizando juego.\n");
            break;
        }

        // Habilitar al jugador actual
        sem_post(&semaphores->playerCanMove[currentPlayerIndex]);

        // Leer movimiento del jugador actual - bloquear hasta que responda
        unsigned char movement;
        ssize_t bytesRead = read(pipePlayerToMaster[currentPlayerIndex][0], &movement, 1);
        if (bytesRead != 1)
        {
            // Jugador desconectado o error
            printf("Jugador %d desconectado. Marcándolo como bloqueado.\n", currentPlayerIndex + 1);
            gameState->players[currentPlayerIndex].blocked = true;
            currentPlayerIndex = (currentPlayerIndex + 1) % numPlayers;
            continue;
        }

        // Procesar el movimiento
        sem_wait(&semaphores->mutexGameState);

        bool validMove = false;
        int currentX = gameState->players[currentPlayerIndex].x;
        int currentY = gameState->players[currentPlayerIndex].y;
        int newX = currentX, newY = currentY;

        // Mapear movimiento a desplazamiento
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

        // Validar movimiento
        if (newX >= 0 && newY >= 0 &&
            (unsigned int)newX < width && (unsigned int)newY < height &&
            gameState->grid[(unsigned int)newY * width + (unsigned int)newX] > 0)
        {

            // Movimiento válido
            gameState->players[currentPlayerIndex].score +=
                gameState->grid[(unsigned int)newY * width + (unsigned int)newX];
            gameState->players[currentPlayerIndex].valid++;
            // marca celda visitada por el jugador con -(index+1) para ser consistente
            gameState->grid[(unsigned int)newY * width + (unsigned int)newX] = -(int)currentPlayerIndex - 1;
            gameState->players[currentPlayerIndex].x = (unsigned short)newX;
            gameState->players[currentPlayerIndex].y = (unsigned short)newY;

            validMove = true;
            lastValidMove = time(NULL);

            // Verificar si el jugador quedó bloqueado (sin movimientos válidos)
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
            gameState->players[currentPlayerIndex].blocked = !hasValidMoves;
        }
        else
        {
            // Movimiento inválido
            gameState->players[currentPlayerIndex].invalid++;
        }

        sem_post(&semaphores->mutexGameState);

        // Avanzar al siguiente jugador en round-robin
        currentPlayerIndex = (currentPlayerIndex + 1) % numPlayers;

        // Notificar a la vista si hay una y hubo un movimiento válido
        if (view != NULL && validMove)
        {
            sem_post(&semaphores->pendingView);
            sem_wait(&semaphores->viewEndedPrinting);
            usleep(delay * 1000); // delay en microsegundos
        }
    }

    // Marcar fin del juego
    sem_wait(&semaphores->mutexGameState);
    gameState->gameOver = true;
    sem_post(&semaphores->mutexGameState);

    // Notificar vista final si existe
    if (view != NULL)
    {
        sem_post(&semaphores->pendingView);
        sem_wait(&semaphores->viewEndedPrinting);
    }

    // Habilitar a todos los jugadores para que puedan terminar
    for (unsigned int i = 0; i < numPlayers; i++)
    {
        sem_post(&semaphores->playerCanMove[i]);
    }

    // Esperar a que terminen todos los procesos hijos
    printf("\n=== RESULTADOS FINALES ===\n");

    // Esperar jugadores
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
                    printf("Jugador %d (%s): Puntaje %u, Salió con código %d\n",
                           i + 1, gameState->players[i].playerName,
                           gameState->players[i].score, WEXITSTATUS(status));
                }
                else if (WIFSIGNALED(status))
                {
                    printf("Jugador %d (%s): Puntaje %u, Terminado por señal %d\n",
                           i + 1, gameState->players[i].playerName,
                           gameState->players[i].score, WTERMSIG(status));
                }
            }
        }
        if (pipePlayerToMaster[i][0] != -1)
        {
            close(pipePlayerToMaster[i][0]);
        }
    }

    // Esperar vista
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
        if (pipeVistaToMaster[0] != -1)
        {
            close(pipeVistaToMaster[0]);
        }
    }

    printf("========================\n");

    // Limpieza de memoria compartida y semáforos
    printf("Limpiando recursos...\n");
    cleanup_resources(width, height, numPlayers, gameState, semaphores);
    printf("Recursos liberados correctamente.\n");

    return 0;
}

GameState *createSharedMemoryState(unsigned short width, unsigned short height, unsigned int numPlayers)
{
    // Desacopla memorias compartidas anteriores con distinto tamaño
    shm_unlink("/game_state");

    int gameStateSmFd = shm_open("/game_state", O_CREAT | O_RDWR, 0666);
    if (gameStateSmFd == -1)
    {
        perror("Error al crear la memoria compartida para el estado del juego");
        exit(1);
    }

    // Configurar el tamaño de la memoria compartida
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

    // Cerrar el file descriptor ya que no lo necesitamos más
    close(gameStateSmFd);

    // Inicializar el estado del juego
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
    // Distribuir en las esquinas y bordes para dar margen de movimiento similar
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
        snprintf(gameState->players[i].playerName, sizeof(gameState->players[i].playerName), "P%d", i + 1);

        // Asignar posición determinística
        if (i < 9)
        {
            gameState->players[i].x = positions[i][0];
            gameState->players[i].y = positions[i][1];
        }
        else
        {
            // Para más de 9 jugadores, usar distribución pseudo-aleatoria determinística
            gameState->players[i].x = (i * 7) % width;
            gameState->players[i].y = (i * 11) % height;
        }

        gameState->players[i].score = 0;
        gameState->players[i].invalid = 0;
        gameState->players[i].valid = 0;
        gameState->players[i].pid = 0; // Se asignará luego
        gameState->players[i].blocked = false;

        // Marcar posición inicial en la grilla
        unsigned int pos = gameState->players[i].y * width + gameState->players[i].x;
        gameState->players[i].score += cells[pos]; // Punto inicial
        cells[pos] = -(int)i - 1;                  // Marcar como ocupada por jugador i
    }

    return gameState;
}

Semaphores *createSharedMemorySemaphores(unsigned int numPlayers)
{
    int semaphoresSmFd = shm_open("/game_sync", O_CREAT | O_RDWR, 0666);
    if (semaphoresSmFd == -1)
    {
        perror("Error al crear la memoria compartida para los semáforos");
        exit(1);
    }

    // Configurar el tamaño de la memoria compartida
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

    // Cerrar el file descriptor ya que no lo necesitamos más
    close(semaphoresSmFd);

    // Inicializar los semáforos
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
        // Destruir semáforos
        sem_destroy(&semaphores->pendingView);
        sem_destroy(&semaphores->viewEndedPrinting);
        sem_destroy(&semaphores->mutexMasterAccess);
        sem_destroy(&semaphores->mutexGameState);
        sem_destroy(&semaphores->mutexPlayerAccess);
        for (unsigned int i = 0; i < numPlayers; i++)
        {
            sem_destroy(&semaphores->playerCanMove[i]);
        }

        // Desmapear memoria de semáforos
        if (munmap(semaphores, sizeof(Semaphores)) == -1)
        {
            perror("Error al desmapear memoria compartida de semáforos");
        }
    }

    if (gameState != NULL)
    {
        // Desmapear memoria del estado del juego
        size_t grid_size = (size_t)width * (size_t)height * sizeof(int);
        if (munmap(gameState, sizeof(GameState) + grid_size) == -1)
        {
            perror("Error al desmapear memoria compartida del estado del juego");
        }
    }

    // Eliminar objetos de memoria compartida
    shm_unlink("/game_state");
    shm_unlink("/game_sync");
}

void signal_handler(int sig)
{
    printf("\nRecibida señal %d. Limpiando recursos...\n", sig);
    cleanup_resources(g_width, g_height, g_numPlayers, g_gameState, g_semaphores);
    exit(sig);
}