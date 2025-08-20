
#include "estructuras.h"


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


    //creación pipe master --> player
    int pipe_master_to_player[2]; 
    if(pipe(pipe_master_to_player)==-1) //--> obtengo los fd del proceso master
    {
        perror("Error creando el pipe"); 
        exit(1); 
    }

    //creación pipe player --> master
    int pipe_player_to_master[2]; 
     if(pipe(pipe_player_to_master)==-1) //--> obtengo los fd del proceso master
    {
        perror("Error creando el pipe"); 
        exit(1); 
    }

    char *argv[] = {"./jugador", ancho, alto, NULL};
    for(int i=0; i<10;i++)
    {
        if(fork()==0)
        {

            close(pipe_master_to_player[1]); // extremo que no lo necesitamos
            close(pipe_player_to_master[0]); // extremo que no lo necesitamos
            dup2(pipe_master_to_player[0],STDOUT_FILENO); //jugador lee del pipe al master
            dup2(pipe_player_to_master[1], STDOUT_FILENO);//jugador escribe en el pipe al master

            // Cerrar los extremos originales de los pipes (dup2 los duplica)
            close(pipe_master_to_player[0]);
            close(pipe_player_to_master[1]);

            execv("./jugador", argv);
            perror("execv");
            exit(1);
            //empieza a ejecutarse el proceso jugador
            //execve("procesoHijo",NULL,NULL);
        }
    }



    // Distribución de jugadores en el tablero
    distribuir_jugadores(sm);

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

    // limpiamos recursos
    limpiar_memorias_compartidas(sm);

    return 0;
    
}