#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>
#include <semaphore.h>
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
    sem_t A; // El máster le indica a la vista que hay cambios por imprimir
    sem_t B; // La vista le indica al máster que terminó de imprimir
    sem_t C; // Mutex para evitar inanición del máster al acceder al estado
    sem_t D; // Mutex para el estado del juego
    sem_t E; // Mutex para la siguiente variable
    unsigned int F; // Cantidad de jugadores leyendo el estado
    sem_t G[9]; // Le indican a cada jugador que puede enviar 1 movimiento
} semaforos;
