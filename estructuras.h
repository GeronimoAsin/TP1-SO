
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <stdbool.h>
#include <ctype.h>

#define MAX_PLAYERS 9

typedef struct {
    char nombre[16]; // Nombre del jugador
    unsigned int puntaje; // Puntaje
    unsigned int movimientos_invalidos; // Cantidad de solicitudes de movimientos inválidas realizadas
    unsigned int movimientos_validos; // Cantidad de solicitudes de movimientos válidas realizadas
    unsigned short coordenadas[2]; // Coordenadas x e y en el tablero
    pid_t pid; // Identificador de proceso
    bool bloqueado; // Indica si el jugador está bloqueado
} jugador;


typedef struct {
    unsigned short ancho; // Ancho del tablero
    unsigned short alto; // Alto del tablero
    unsigned int cantidad_jugadores; // Cantidad de jugadores
    jugador jugadores[MAX_PLAYERS]; // Lista de jugadores
    bool juego_terminado; // Indica si el juego se ha terminado
    int tablero[]; // Puntero al comienzo del tablero. fila-0, fila-1, ..., fila-n-1
} tablero;

typedef struct {
    sem_t notificar_vista;           // A-El máster le indica a la vista que hay cambios por imprimir
    sem_t impresion_completada;      // B-La vista le indica al máster que terminó de imprimir
    sem_t mutex_anti_inanicion;      // C-Mutex para evitar inanición del máster al acceder al estado
    sem_t mutex_estado_juego;        // D-Mutex para el estado del juego
    sem_t mutex_contador_lectores;   // E-Mutex para la variable lectores_activos
    unsigned int lectores_activos;   // F-Cantidad de jugadores leyendo el estado
    sem_t permiso_movimiento[9];     // G-Indican a cada jugador que puede enviar 1 movimiento
} semaforos;


// Estructura para manejar ambas memorias compartidas game_state y game_sync
typedef struct {
    tablero *game_state;
    semaforos *game_sync;
    size_t state_size;
} shared_memories;
