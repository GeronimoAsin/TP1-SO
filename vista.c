/*
  Proceso Vista
  -------------------------------------
  
  Responsabilidades básicas:
  1. Recibir como parámetros el ancho y alto del tablero
  2. Conectarse a ambas memorias compartidas
  3. Esperar cambios en el estado del juego e imprimirlos
  4. Notificar al máster que el estado fue impreso
  

*/

#include "estructuras.h"

// Adjunta las memorias compartidas creadas por el máster
static int adjuntar_memorias(shared_memories *sm) {
    // Conectar a /game_state (solo lectura)
    int fd_state = shm_open("/game_state", O_RDONLY, 0666);
    if (fd_state == -1) { 
        perror("vista: shm_open /game_state"); 
        return -1; 
    }
    
    struct stat st = {0};
    if (fstat(fd_state, &st) == -1) { 
        perror("vista: fstat /game_state"); 
        close(fd_state); 
        return -1; 
    }
    
    sm->state_size = (size_t)st.st_size;
    sm->game_state = (tablero*)mmap(NULL, sm->state_size, PROT_READ, MAP_SHARED, fd_state, 0);
    close(fd_state);
    
    if (sm->game_state == MAP_FAILED) { 
        perror("vista: mmap state"); 
        return -1; 
    }
    
    // Conexión a /game_sync (lectura/escritura para semáforos)
    int fd_sync = shm_open("/game_sync", O_RDWR, 0666);
    if (fd_sync == -1) { 
        perror("vista: shm_open /game_sync"); 
        munmap(sm->game_state, sm->state_size); 
        return -1; 
    }
    
    sm->game_sync = (semaforos*)mmap(NULL, sizeof(semaforos), PROT_READ | PROT_WRITE, MAP_SHARED, fd_sync, 0);
    close(fd_sync);
    
    if (sm->game_sync == MAP_FAILED) { 
        perror("vista: mmap sync"); 
        munmap(sm->game_state, sm->state_size); 
        return -1; 
    }
    
    return 0;
}

static void desacoplar_memorias(shared_memories *sm) {
    if (sm->game_sync) munmap(sm->game_sync, sizeof(semaforos));
    if (sm->game_state) munmap(sm->game_state, sm->state_size);
}

// Función base para imprimir el estado del tablero
// Despues la podemos remplazar con la implementación de ncurses
static void imprimir_estado_base(const tablero *state) {
    printf("\n=== ESTADO DEL JUEGO ===\n");
    printf("Tablero: %dx%d | Jugadores: %d\n", 
           state->ancho, state->alto, state->cantidad_jugadores);
    
    // Mostrar tablero (formato simple)
    for (int y = 0; y < state->alto; y++) {
        for (int x = 0; x < state->ancho; x++) {
            int valor = state->tablero[y * state->ancho + x];
            if (valor < 0) {
                printf("P%d ", -valor);  // Jugador
            } else {
                printf("%2d ", valor);   // Recompensa
            }
        }
        printf("\n");
    }
    
    //  información de jugadores
    printf("\nJugadores:\n");
    for (int i = 0; i < state->cantidad_jugadores; i++) {
        const jugador *j = &state->jugadores[i];
        printf("  %d: %s - Puntaje: %u, Pos: (%u,%u)\n", 
               i, j->nombre, j->puntaje, j->coordenadas[0], j->coordenadas[1]);
    }
    
    if (state->juego_terminado) {
        printf("\n*** JUEGO TERMINADO ***\n");
    }
    printf("=======================\n\n");
}



//================== TEST IMPRESION - construye un tablero en memoria y usa imprimir_estado_base ====================
static int selftest(int ancho, int alto) {
    if (ancho <= 0) ancho = 5;
    if (alto <= 0) alto = 5;

    size_t size = sizeof(tablero) + (size_t)ancho * (size_t)alto * sizeof(int);
    tablero *st = (tablero*)malloc(size);
    if (!st) {
        perror("vista selftest: malloc tablero");
        return 1;
    }

    st->ancho = (unsigned short)ancho;
    st->alto = (unsigned short)alto;
    st->cantidad_jugadores = 2;
    st->juego_terminado = false;

    // Inicializar jugadores demo
    snprintf(st->jugadores[0].nombre, sizeof st->jugadores[0].nombre, "Player_A");
    st->jugadores[0].puntaje = 10;
    st->jugadores[0].coordenadas[0] = 0;
    st->jugadores[0].coordenadas[1] = 0;

    snprintf(st->jugadores[1].nombre, sizeof st->jugadores[1].nombre, "Player_B");
    st->jugadores[1].puntaje = 20;
    st->jugadores[1].coordenadas[0] = (unsigned short)(ancho - 1);
    st->jugadores[1].coordenadas[1] = (unsigned short)(alto - 1);

    // Llenar tablero con valores de ejemplo
    for (int y = 0; y < alto; y++) {
        for (int x = 0; x < ancho; x++) {
            st->tablero[y * ancho + x] = (x + y) % 9 + 1; // 1..9
        }
    }
    // Marcar posiciones de los jugadores
    st->tablero[st->jugadores[0].coordenadas[1] * ancho + st->jugadores[0].coordenadas[0]] = -1;
    st->tablero[st->jugadores[1].coordenadas[1] * ancho + st->jugadores[1].coordenadas[0]] = -2;

    printf("Vista selftest - Tablero: %dx%d\n", ancho, alto);
    imprimir_estado_base(st);

    // Simular fin de juego y volver a imprimir
    st->juego_terminado = true;
    printf("Marcando fin de juego...\n");
    imprimir_estado_base(st);

    free(st);
    return 0;
}

//==================== TEST DE ACTUALIZACION: simula varios pasos moviendo jugadores y reimprime =====================
static int demo_animada(int ancho, int alto, int pasos, int delay_ms) {
    if (ancho <= 0) ancho = 6;
    if (alto <= 0) alto = 4;
    if (pasos <= 0) pasos = 6;
    if (delay_ms < 0) delay_ms = 300;

    size_t size = sizeof(tablero) + (size_t)ancho * (size_t)alto * sizeof(int);
    tablero *st = (tablero*)malloc(size);
    if (!st) { perror("vista demo: malloc tablero"); return 1; }

    st->ancho = (unsigned short)ancho;
    st->alto = (unsigned short)alto;
    st->cantidad_jugadores = 2;
    st->juego_terminado = false;

    // Inicializar nombres y puntajes
    snprintf(st->jugadores[0].nombre, sizeof st->jugadores[0].nombre, "Alice");
    st->jugadores[0].puntaje = 0;
    snprintf(st->jugadores[1].nombre, sizeof st->jugadores[1].nombre, "Bob");
    st->jugadores[1].puntaje = 0;

    // Rellenar recompensas base
    for (int y = 0; y < alto; y++) {
        for (int x = 0; x < ancho; x++) {
            st->tablero[y * ancho + x] = ((x + 1) * (y + 2)) % 9 + 1;
        }
    }

    // Posiciones iniciales
    st->jugadores[0].coordenadas[0] = 0;               st->jugadores[0].coordenadas[1] = 0;
    st->jugadores[1].coordenadas[0] = (unsigned short)(ancho - 1); st->jugadores[1].coordenadas[1] = (unsigned short)(alto - 1);

    for (int step = 0; step < pasos; step++) {
        // Restaurar celdas previas si eran negativas
        for (int y = 0; y < alto; y++) {
            for (int x = 0; x < ancho; x++) {
                if (st->tablero[y * ancho + x] < 0) {
                    st->tablero[y * ancho + x] = ((x + 1) * (y + 2)) % 9 + 1;
                }
            }
        }

        // Mover jugadores (trayectorias simples)
        unsigned short *ax = &st->jugadores[0].coordenadas[0];
        unsigned short *ay = &st->jugadores[0].coordenadas[1];
        unsigned short *bx = &st->jugadores[1].coordenadas[0];
        unsigned short *by = &st->jugadores[1].coordenadas[1];

        if (*ax + 1 < (unsigned short)ancho) (*ax)++; else if (*ay + 1 < (unsigned short)alto) (*ay)++; else { *ax = 0; *ay = 0; }
        if (*bx > 0) (*bx)--; else if (*by > 0) (*by)--; else { *bx = (unsigned short)(ancho - 1); *by = (unsigned short)(alto - 1); }

        // Recolectar recompensa: sumar puntaje de la celda donde cae cada jugador y poner a 0
        st->jugadores[0].puntaje += (unsigned int)st->tablero[*ay * ancho + *ax];
        st->tablero[*ay * ancho + *ax] = 0;
        st->jugadores[1].puntaje += (unsigned int)st->tablero[*by * ancho + *bx];
        st->tablero[*by * ancho + *bx] = 0;

        // Marcar jugadores
        st->tablero[*ay * ancho + *ax] = -1;
        st->tablero[*by * ancho + *bx] = -2;

        printf("\n[Demo paso %d/%d] Puntajes: %s=%u, %s=%u\n",
               step + 1, pasos, st->jugadores[0].nombre, st->jugadores[0].puntaje,
               st->jugadores[1].nombre, st->jugadores[1].puntaje);
        imprimir_estado_base(st);

        // Pausa
        usleep((useconds_t)(delay_ms * 1000));
    }

    st->juego_terminado = true;
    printf("\n[Demo] Juego terminado. Estado final:\n");
    imprimir_estado_base(st);

    free(st);
    return 0;
}




// ======================= ACA ESTAN LAS RESPONSABILIDADES DE LA VISTA ===================

int main(int argc, char *argv[]) {
    // TEST DE IMPRESION: ./vista --test [ancho] [alto]
    if (argc >= 2 && strcmp(argv[1], "--test") == 0) {
        int ancho = (argc >= 3) ? atoi(argv[2]) : 5;
        int alto  = (argc >= 4) ? atoi(argv[3]) : 5;
        return selftest(ancho, alto);
    }
    // TEST DE ACTUALIZACION: ./vista --demo [ancho] [alto] [pasos] [delay_ms]
    if (argc >= 2 && strcmp(argv[1], "--demo") == 0) {
        int ancho = (argc >= 3) ? atoi(argv[2]) : 6;
        int alto  = (argc >= 4) ? atoi(argv[3]) : 4;
        int pasos = (argc >= 5) ? atoi(argv[4]) : 6;
        int delay = (argc >= 6) ? atoi(argv[5]) : 300;
        return demo_animada(ancho, alto, pasos, delay);
    }

    // Responsabilidad 1. Recibir parámetros: ancho y alto del tablero
    if (argc < 3) {
        fprintf(stderr, "Uso: %s <ancho> <alto>\n", argv[0]);
        return 1;
    }
    
    int ancho = atoi(argv[1]);
    int alto = atoi(argv[2]);
    
    if (ancho <= 0 || alto <= 0) {
        fprintf(stderr, "Error: ancho y alto deben ser positivos\n");
        return 1;
    }
    
    printf("Vista iniciada - Tablero: %dx%d\n", ancho, alto);
    
    // Responsabilidad 2. Conectarse a ambas memorias compartidas
    shared_memories sm = {0};
    if (adjuntar_memorias(&sm) != 0) {
        fprintf(stderr, "Error: No se pudo conectar a las memorias compartidas\n");
        return 1;
    }
    
    printf("Vista conectada a memorias compartidas\n");
    
    // Responsabilidad 3. Bucle principal: esperar cambios en el estado del juego
    while (1) {
        // Esperar notificación del máster (semáforo notificar_vista)
        if (sem_wait(&sm.game_sync->notificar_vista) == -1) {
            perror("vista: sem_wait notificar_vista");
            break;
        }
        
        // Verificar si el juego terminó
        if (sm.game_state->juego_terminado) {
            imprimir_estado_base(sm.game_state);
            // 4. Notificar al máster que el estado fue impreso
            sem_post(&sm.game_sync->impresion_completada);
            break;
        }
        
        // Imprimir estado actual del juego
        imprimir_estado_base(sm.game_state);
        
        // Responsabilidad 4. Notificar al máster que el estado fue impreso
        sem_post(&sm.game_sync->impresion_completada);
    }
    
    printf("Vista terminando\n");
    desacoplar_memorias(&sm);
    return 0;
}
