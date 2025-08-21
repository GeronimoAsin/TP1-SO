#include "estructuras.h"
#include <sys/wait.h>
#include <signal.h>
#include <time.h>


int inicializar_estructuras(shared_memories *sm, int ancho, int alto, int num_jugadores);
void limpiar_memorias_compartidas(shared_memories *sm);

/*
  Función principal para crear e inicializar ambas memorias compartidas:
  game_state y game_sync
 */
shared_memories* crear_memorias_compartidas(int ancho, int alto, int num_jugadores) {
    shared_memories *sm = malloc(sizeof(shared_memories));
    if (!sm) {
        perror("malloc shared_memories");
        return NULL;
    }

    // ===============================
    //  MEMORIA COMPARTIDA GAME_STATE
    // ===============================

    // Crea objeto de memoria compartida
    int shm_state_fd = shm_open("/game_state", O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_state_fd == -1) {
        perror("shm_open /game_state");
        free(sm);
        return NULL;
    }

    // Calcula tamaño total de la memoria: estructura + tablero dinámico
    sm->state_size = sizeof(tablero) + (ancho * alto * sizeof(int));

    // Establece el tamaño de la memoria compartida
    //con ftruncate asignamos el tamaño de la memoria compartida
    if (ftruncate(shm_state_fd, sm->state_size) == -1) {
        perror("ftruncate game_state");
        close(shm_state_fd);
        shm_unlink("/game_state");
        free(sm);
        return NULL;
    }

    // Mapea la memoria compartida
   //mapeo de game_state a espacio en memoria determinado por el kernel
    sm->game_state = (tablero*)mmap(NULL, sm->state_size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED, shm_state_fd, 0);
    if (sm->game_state == MAP_FAILED) {
        perror("mmap game_state");
        close(shm_state_fd);
        shm_unlink("/game_state");
        free(sm);
        return NULL;
    }

    close(shm_state_fd); // Ya no necesitamos el descriptor

    // ===============================
    // MEMORIA COMPARTIDA GAME_SYNC
    // ===============================

    // Crear objeto de memoria compartida para sincronización
    int shm_sync_fd = shm_open("/game_sync", O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_sync_fd == -1) {
        perror("shm_open /game_sync");
        munmap(sm->game_state, sm->state_size);
        shm_unlink("/game_state");
        free(sm);
        return NULL;
    }

    // Establecer tamaño de la estructura de semáforos
    if (ftruncate(shm_sync_fd, sizeof(semaforos)) == -1) {
        perror("ftruncate game_sync");
        close(shm_sync_fd);
        munmap(sm->game_state, sm->state_size);
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        free(sm);
        return NULL;
    }

    // Mapear la memoria de sincronización
    //mapeo de game_sync a espacio en memoria determinado por el kernel
    sm->game_sync = (semaforos*)mmap(NULL, sizeof(semaforos),
                                     PROT_READ | PROT_WRITE,
                                     MAP_SHARED, shm_sync_fd, 0);
    if (sm->game_sync == MAP_FAILED) {
        perror("mmap game_sync");
        close(shm_sync_fd);
        munmap(sm->game_state, sm->state_size);
        shm_unlink("/game_state");
        shm_unlink("/game_sync");
        free(sm);
        return NULL;
    }

    close(shm_sync_fd); // Ya no necesitamos el descriptor

    // ===============================
    // INICIALIZACIÓN DE MEMORIAS COMPARTIDAS
    // ===============================

    if (inicializar_estructuras(sm, ancho, alto, num_jugadores) != 0) {
        limpiar_memorias_compartidas(sm);
        return NULL;
    }

    printf("Memorias compartidas creadas e inicializadas correctamente\n");
    printf("- /game_state: %zu bytes\n", sm->state_size);
    printf("- /game_sync: %zu bytes\n", sizeof(semaforos));

    return sm;
}

/**
 * Inicialización de todas las estructuras
 */
int inicializar_estructuras(shared_memories *sm, int ancho, int alto, int num_jugadores) {

    tablero *state = sm->game_state;
    semaforos *sync = sm->game_sync;

    // ===============================
    // INICIALIZAR ESTADO DEL JUEGO
    // ===============================

    // Configurar dimensiones del tablero
    state->ancho = ancho;
    state->alto = alto;
    state->cantidad_jugadores = num_jugadores;
    state->juego_terminado = false;

    // Inicializar jugadores
    for (int i = 0; i < num_jugadores; i++) {
        snprintf(state->jugadores[i].nombre, 16, "Player_%d", i + 1);
        state->jugadores[i].puntaje = 0;
        state->jugadores[i].movimientos_invalidos = 0;
        state->jugadores[i].movimientos_validos = 0;
        state->jugadores[i].coordenadas[0] = 0; // x
        state->jugadores[i].coordenadas[1] = 0; // y
        state->jugadores[i].pid = 0; // Se asignará cuando se creen los procesos
        state->jugadores[i].bloqueado = false;
    }

    // Inicializa el tablero con recompensas aleatorias (1-9)
    srand(time(NULL)); // Usar semilla por defecto o la pasada por parámetro

    for (int i = 0; i < ancho * alto; i++) {
        state->tablero[i] = (rand() % 9) + 1; // Asigno valores entre 1 y 9 aleatorios
    }

    // ===============================
    // INICIALIZAR SEMÁFOROS
    // ===============================

    // Semáforos para comunicación máster-vista



    /*
        FUNCIONES SEMAFOROS:

        sem_init inicializa un semaforo
        int sem_init(sem_t *sem, int pshared, unsigned int value);
        -sem= donde vive el semaforo
        -pshared, 0 si esta compartida por las thread de 1 unico proceso , !=0 si esta compartido entre procesos
        -value  valor inicial del semaforo. Arranca en 0 (los que esperan estan bloqueado)

        sem_wait(sem) decrementa el valor actual del semaforo --> bloquea al proceso si su valor es =0
        sem_post(sem) incrementa el valor actual del semaforno    --> despierta a un proceso bloqueado si lo hay
*/


    if (sem_init(&sync->notificar_vista, 1, 0) != 0) { // 1 = compartido entre procesos
        perror("sem_init notificar_vista");
        return -1;
    }

    if (sem_init(&sync->impresion_completada, 1, 0) != 0) {
        perror("sem_init impresion_completada");
        return -1;
    }

    // Semáforos para patrón lectores-escritores
    if (sem_init(&sync->mutex_anti_inanicion, 1, 1) != 0) { // Mutex anti-inanición
        perror("sem_init mutex_anti_inanicion");
        return -1;
    }

    if (sem_init(&sync->mutex_estado_juego, 1, 1) != 0) { // Mutex del estado
        perror("sem_init mutex_estado_juego");
        return -1;
    }

    if (sem_init(&sync->mutex_contador_lectores, 1, 1) != 0) { // Mutex para variable lectores_activos
        perror("sem_init mutex_contador_lectores");
        return -1;
    }

    // Variable compartida para contar lectores
    sync->lectores_activos = 0;

    // Semáforos individuales para cada jugador (máximo 9)
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (sem_init(&sync->permiso_movimiento[i], 1, 1) != 0) { // Cada jugador puede enviar 1 movimiento
            perror("sem_init permiso_movimiento[i]");
            return -1;
        }
    }

    return 0;
}

/**
 * Distribuir jugadores en posiciones iniciales del tablero
 */
void distribuir_jugadores(shared_memories *sm) {
    tablero *state = sm->game_state;
    int ancho = state->ancho;
    int alto = state->alto;
    int num_jugadores = state->cantidad_jugadores;

    // Distribución simple: en las esquinas y bordes
    int posiciones[][2] = {
        {0, 0},                    // Esquina superior izquierda
        {ancho-1, 0},              // Esquina superior derecha
        {0, alto-1},               // Esquina inferior izquierda
        {ancho-1, alto-1},         // Esquina inferior derecha
        {ancho/2, 0},              // Centro superior
        {ancho/2, alto-1},         // Centro inferior
        {0, alto/2},               // Centro izquierdo
        {ancho-1, alto/2},         // Centro derecho
        {ancho/2, alto/2}          // Centro del tablero
    };

    for (int i = 0; i < num_jugadores && i < MAX_PLAYERS; i++) {
        int x = posiciones[i][0];
        int y = posiciones[i][1];

        // Asigna coordenadas al jugador
        state->jugadores[i].coordenadas[0] = x;
        state->jugadores[i].coordenadas[1] = y;

        // Marca la celda como ocupada por el jugador con un numero negativo
        // -1 para jugador 0, -2 para jugador 1, etc
        state->tablero[y * ancho + x] = -(i + 1);

        printf("Jugador %d posicionado en (%d, %d)\n", i, x, y);
    }
}

/**
 * Función para limpiar las memorias compartidas
 */
void limpiar_memorias_compartidas(shared_memories *sm) {
    if (!sm) return;

    if (sm->game_sync) {
        // Destruir todos los semáforos
        sem_destroy(&sm->game_sync->notificar_vista);
        sem_destroy(&sm->game_sync->impresion_completada);
        sem_destroy(&sm->game_sync->mutex_anti_inanicion);
        sem_destroy(&sm->game_sync->mutex_estado_juego);
        sem_destroy(&sm->game_sync->mutex_contador_lectores);

        for (int i = 0; i < MAX_PLAYERS; i++) {
            sem_destroy(&sm->game_sync->permiso_movimiento[i]);
        }

        // Desmapear memoria de sincronización
        munmap(sm->game_sync, sizeof(semaforos));
    }

    if (sm->game_state) {
        // Desmapear memoria de estado
        munmap(sm->game_state, sm->state_size);
    }

    // Eliminar objetos de memoria compartida del sistema
    shm_unlink("/game_state");
    shm_unlink("/game_sync");

    free(sm);

    printf("Memorias compartidas limpiadas\n");
}

/**
 * Ejemplo de uso en el main del máster
 */
int main(int argc, char *argv[]) {
    // Parámetros por defecto
    unsigned int ancho = 10, alto = 10, num_jugadores = 2, delay = 200, timeout = 10, seed = time(NULL);
    char * vista = NULL;
    char * jugadores[9]={0};

    for(int i = 0; i<argc; i++){
        if(!strcmp(argv[i],"-w")){
            if(isdigit(argv[i + 1])){
                ancho = argv[i + 1];
            }
        }
        if(!strcmp(argv[i],"-h")){
            if(isdigit(argv[i + 1])){
                alto = argv[i + 1];
            }
        }
        if(!strcmp(argv[i],"-d")){
            if(isdigit(argv[i + 1])){
                delay = argv[i + 1];
            }
        }
        if(!strcmp(argv[i],"-t")){
            if(isdigit(argv[i + 1])){
                timeout = argv[i + 1];
            }
        }
        if(!strcmp(argv[i],"-s")){
            if(isdigit(argv[i + 1])){
                seed = argv[i + 1];
            }
        }
        if(!strcmp(argv[i],"-v")){
            vista = argv[i+1];
        }
        if(!strcmp(argv[i],"-p")){
            num_jugadores = argc - i - 1;
            if(num_jugadores < 1){
                perror("Debe haber al menos un jugador");
                exit(1);
            }
            for(int j=0; j<num_jugadores; j++){
                jugadores[j] = argv[i + j + 1];
            }
        }
    }
    
    

    // Creación y inicialización de memorias compartidas
    shared_memories *sm = crear_memorias_compartidas(ancho, alto, num_jugadores);
    if (!sm) {
        fprintf(stderr, "Error creando memorias compartidas\n");
        return 1;
    }

    // ==========================
    // Lanzar proceso de la vista
    // ==========================
    pid_t pid_vista = -1;
    if (vista && vista[0] != '\0') {
        pid_t vp = fork();
        if (vp == -1) {
            perror("fork vista");
            limpiar_memorias_compartidas(sm);
            return 1;
        }
        if (vp == 0) {
            // ejecutar binario de vista con parámetros ancho y alto
            char wbuf[16], hbuf[16];
            snprintf(wbuf, sizeof wbuf, "%u", ancho);
            snprintf(hbuf, sizeof hbuf, "%u", alto);
            char *argv_vista[] = { vista, wbuf, hbuf, NULL };
            execv(vista, argv_vista);
            perror("execv vista");
            exit(1);
        }
        pid_vista = vp;
    }

     // ==========================
    // Lanzar procesos de los jugadores
    // ==========================

    //  pipes para cada jugador (comunicación bidireccional)
    // Usamos arreglos de tamaño fijo según MAX_PLAYERS (máximo 9)
    // en la practica habían dicho de evitar malloc si no es necesario
    int pipes_master_to_player[MAX_PLAYERS][2];
    int pipes_player_to_master[MAX_PLAYERS][2];

    // Inicializar pipes para cada jugador efectivo
    for (int i = 0; i < num_jugadores; i++) {
        // Crear pipe master -> player
        if (pipe(pipes_master_to_player[i]) == -1) {
            perror("pipe master->player");
            // Cerrar cualquier pipe previamente creado
            for (int j = 0; j < i; j++) {
                close(pipes_master_to_player[j][0]);
                close(pipes_master_to_player[j][1]);
                close(pipes_player_to_master[j][0]);
                close(pipes_player_to_master[j][1]);
            }
            limpiar_memorias_compartidas(sm);
            return 1;
        }

        // Creación pipe player -> master
        if (pipe(pipes_player_to_master[i]) == -1) {
            perror("pipe player->master");
            // Cierro cualquier pipe previamente creado (incluyendo el recién creado master->player)
            for (int j = 0; j <= i; j++) {
                close(pipes_master_to_player[j][0]);
                close(pipes_master_to_player[j][1]);
            }
            for (int j = 0; j < i; j++) {
                close(pipes_player_to_master[j][0]);
                close(pipes_player_to_master[j][1]);
            }
            limpiar_memorias_compartidas(sm);
            return 1;
        }
    }

    // Crear procesos jugadores (arreglo fijo)
    pid_t pids_jugadores[MAX_PLAYERS] = {0};

    for (int i = 0; i < num_jugadores; i++) {
        pid_t pid = fork();

        if (pid == -1) {
            perror("fork");
            // Limpiar recursos y terminar
            for (int j = 0; j < i; j++) {
                kill(pids_jugadores[j], SIGTERM);
            }
            for (int j = 0; j <= i; j++) {
                close(pipes_master_to_player[j][0]);
                close(pipes_master_to_player[j][1]);
                close(pipes_player_to_master[j][0]);
                close(pipes_player_to_master[j][1]);
            }
            limpiar_memorias_compartidas(sm);
            return 1;
        }

        if (pid == 0) {
            // Proceso hijo (jugador)

            // Cerrar extremos de pipes que no necesita
            for (int j = 0; j < num_jugadores; j++) {
                if (j != i) {
                    close(pipes_master_to_player[j][0]);
                    close(pipes_master_to_player[j][1]);
                    close(pipes_player_to_master[j][0]);
                    close(pipes_player_to_master[j][1]);
                }
            }

            
            close(pipes_master_to_player[i][1]); // Cerrar extremo de escritura del pipe master->player
            close(pipes_player_to_master[i][0]); // Cerrar extremo de lectura del pipe player->master



            // stdin del jugador <- pipe master->player (el jugador lee comandos del master)
            if (dup2(pipes_master_to_player[i][0], STDIN_FILENO) == -1) {
                perror("dup2 stdin");
                exit(1);
            }



            // stdout del jugador -> pipe player->master (el jugador escribe respuestas al master)
            if (dup2(pipes_player_to_master[i][1], STDOUT_FILENO) == -1) {
                perror("dup2 stdout");
                exit(1);
            }



            // Cerrar los extremos originales
            close(pipes_master_to_player[i][0]);
            close(pipes_player_to_master[i][1]);



            // Preparar argumentos para el jugador
            char idx_str[16];
            snprintf(idx_str, sizeof(idx_str), "%d", i);
            char *argv_jugador[] = {"./jugador", idx_str, NULL};

            execv("./jugador", argv_jugador);
            perror("execv jugador");
            exit(1);
        } else {

            // Proceso padre (master)
            pids_jugadores[i] = pid;

            // Cerrar extremos de pipes que no necesita en el master
            close(pipes_master_to_player[i][0]); // Cerrar extremo de lectura del pipe master->player
            close(pipes_player_to_master[i][1]); // Cerrar extremo de escritura del pipe player->master
        }
    }

    // Distribución de jugadores en el tablero (no random)
    distribuir_jugadores(sm);

    // Si hay vista, notificar estado inicial y esperar impresión
    if (pid_vista > 0) {
        sem_post(&sm->game_sync->notificar_vista);
        sem_wait(&sm->game_sync->impresion_completada);
    }



    /*


        ==================== TESTS VIEJOS ==================== 



    // TEST - manejo del juego
    tablero *state = sm->game_state;
    printf("\n--- Estado inicial del tablero ---\n");
    printf("Tamaño: %dx%d, Jugadores: %d\n", state->ancho, state->alto, state->cantidad_jugadores);
    for (int j = 0; j < state->cantidad_jugadores; j++) {
        printf("Jugador %d: nombre='%s', puntaje=%u, pos=(%u,%u)\n", j, state->jugadores[j].nombre, state->jugadores[j].puntaje, state->jugadores[j].coordenadas[0], state->jugadores[j].coordenadas[1]);
    }

    //TEST- vista del tablero, los jugadores 1 y 2 se los marca como -1 y -2
    // (despues los resuelve la vista)
    printf("Tablero (primeras filas):\n");
    for (int y = 0; y < (state->alto < 5 ? state->alto : 5); y++) {
        for (int x = 0; x < (state->ancho < 10 ? state->ancho : 10); x++) {
            printf("%2d ", state->tablero[y * state->ancho + x]);
        }
        printf("\n");
    }



    // TEST- Modificar una celda del tablero
    int test_x = 1, test_y = 1;
    int idx = test_y * state->ancho + test_x;
    printf("\nModificando celda (%d,%d) de %d a 9...\n", test_x, test_y, state->tablero[idx]);
    state->tablero[idx] = 9;
    printf("Nuevo valor: %d\n", state->tablero[idx]);

    // TEST- Modificar puntaje de un jugador
    printf("\nSumando 10 puntos al Jugador 0 (puntaje actual: %u)...\n", state->jugadores[0].puntaje);
    state->jugadores[0].puntaje += 10;
    printf("Nuevo puntaje Jugador 0: %u\n", state->jugadores[0].puntaje);

    // Muestro estado final
    printf("\n--- Estado final del tablero ---\n");
    for (int j = 0; j < state->cantidad_jugadores; j++) {
        printf("Jugador %d: nombre='%s', puntaje=%u, pos=(%u,%u)\n", j, state->jugadores[j].nombre, state->jugadores[j].puntaje, state->jugadores[j].coordenadas[0], state->jugadores[j].coordenadas[1]);
    }
    printf("Tablero (primeras filas):\n");
    for (int y = 0; y < (state->alto < 5 ? state->alto : 5); y++) {
        for (int x = 0; x < (state->ancho < 10 ? state->ancho : 10); x++) {
            printf("%2d ", state->tablero[y * state->ancho + x]);
        }
        printf("\n");
    }
    printf("\n--- Fin del test  ---\n\n");


    */



    //=================== EXIT MASTER ==================== 
    

    // Marcar fin del juego y notificar a la vista (si existe)
    if (pid_vista > 0) {
        sm->game_state->juego_terminado = true;
        sem_post(&sm->game_sync->notificar_vista);
        sem_wait(&sm->game_sync->impresion_completada);
    }

    // Esperar a que terminen los procesos jugadores
    for (int i = 0; i < num_jugadores; i++) {
        int status;
        waitpid(pids_jugadores[i], &status, 0);
        printf("Jugador %d terminó con status %d\n", i, WEXITSTATUS(status));
    }

    // Esperar a que termine la vista
    if (pid_vista > 0) {
        int vstatus;
        waitpid(pid_vista, &vstatus, 0);
        printf("Vista terminó con status %d\n", WEXITSTATUS(vstatus));
    }

    // Cerrar extremos de pipes que aún están abiertos en el proceso padre
    for (int i = 0; i < num_jugadores; i++) {
        close(pipes_master_to_player[i][1]); // Extremo de escritura master->player
        close(pipes_player_to_master[i][0]); // Extremo de lectura player->master
    }

    // limpiamos recursos
    limpiar_memorias_compartidas(sm);

    return 0;
    
}