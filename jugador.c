/*
  Proceso Jugador
  ----------------
  Contrato de IPC con el máster:
  - Conexión por pipes ya configurados por el máster con dup2() antes del execv():
	  stdin  <- comandos del máster (texto por líneas)
	  stdout -> respuestas del jugador (texto por líneas)
  - Memoria compartida:
	  /game_state: estado del tablero y jugadores (solo lectura en el jugador)
	  /game_sync : semáforos para sincronización (jugador usa permiso_movimiento[i] y el patrón lectores)

  Protocolo mínimo por línea (texto):
  - El máster puede enviar al inicio: "ID <n>\n" para indicar el índice del jugador. Alternativa: pasar <n> en argv[1].
  - En cada turno: el máster envía "GO\n" cuando el semáforo permiso_movimiento[i] fue posteado.
  - El jugador responde una línea con un movimiento, por ejemplo: "MOVE UP|DOWN|LEFT|RIGHT\n".

  Este archivo implementa:
  - Adjuntar a las 2 memorias compartidas.
  - Lectura del índice del jugador (argv o pipe).
  - Uso de semáforos: espera de turno (permiso_movimiento[i]) y patrón lectores para leer el estado.
  - Selección de un movimiento trivial de ejemplo y envío por stdout.
*/
#include "estructuras.h"


// Adjunta las memorias compartidas creadas por el máster
static int adjuntar_memorias(shared_memories *sm) {
	int fd_state = shm_open("/game_state", O_RDWR, 0666);
	if (fd_state == -1) { 
        perror("jugador: shm_open /game_state"); return -1; 
    }
	struct stat st = {0};
	if (fstat(fd_state, &st) == -1) { 
        perror("jugador: fstat /game_state"); 
        close(fd_state); 
        return -1; 
    }
	sm->state_size = (size_t)st.st_size;
	sm->game_state = (tablero*)mmap(NULL, sm->state_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd_state, 0);
	close(fd_state);
	if (sm->game_state == MAP_FAILED) { 
        perror("jugador: mmap state"); 
        return -1; 
    }
	int fd_sync = shm_open("/game_sync", O_RDWR, 0666);
	if (fd_sync == -1) { 
        perror("jugador: shm_open /game_sync"); 
        munmap(sm->game_state, sm->state_size); 
        return -1; 
    }
	sm->game_sync = (semaforos*)mmap(NULL, sizeof(semaforos), PROT_READ | PROT_WRITE, MAP_SHARED, fd_sync, 0);
	close(fd_sync);
	if (sm->game_sync == MAP_FAILED) { 
        perror("jugador: mmap sync"); 
        munmap(sm->game_state, sm->state_size); return -1; }
	return 0;
}

static void desacoplar_memorias(shared_memories *sm) {
	if (sm->game_sync) munmap(sm->game_sync, sizeof(semaforos));
	if (sm->game_state) munmap(sm->game_state, sm->state_size);
}

// Patrón lectores para acceder al estado en sólo lectura
static void empezar_lectura(semaforos *sync) {
	// Anti-inanición + entrada de lector
	sem_wait(&sync->mutex_anti_inanicion);
	sem_wait(&sync->mutex_contador_lectores);
	if (sync->lectores_activos++ == 0) {
		sem_wait(&sync->mutex_estado_juego);
	}
	sem_post(&sync->mutex_contador_lectores);
	sem_post(&sync->mutex_anti_inanicion);
}

static void terminar_lectura(semaforos *sync) {
	sem_wait(&sync->mutex_contador_lectores);
	if (--sync->lectores_activos == 0) {
		sem_post(&sync->mutex_estado_juego);
	}
	sem_post(&sync->mutex_contador_lectores);
}

// Lee una línea de stdin (pipe del máster) ignorando líneas vacías
static int leer_linea(char *buf, size_t n) {
	while (fgets(buf, (int)n, stdin)) {
		// Limpia CR/LF
		buf[strcspn(buf, "\r\n")] = 0;
		if (buf[0] == '\0') continue;
		return 1;
	}
	return 0; // EOF o error
}

// Elige un movimiento trivial en base a la posición actual (ejemplo)
static const char *elegir_movimiento(const tablero *st, int idx) {
	unsigned short x = st->jugadores[idx].coordenadas[0];
	unsigned short y = st->jugadores[idx].coordenadas[1];
	if (x + 1 < st->ancho) return "RIGHT";
	if (y + 1 < st->alto)  return "DOWN";
	if (x > 0)             return "LEFT";
	if (y > 0)             return "UP";
	return "STAY";
}

int main(int argc, char *argv[]) {
	// 1) Determinar índice del jugador
	int idx = -1;
	if (argc >= 2 && argv[1] && isdigit((unsigned char)argv[1][0])) {
		idx = atoi(argv[1]);
	}

	// Si no vino por argv, esperar una línea "ID <n>" desde stdin
	char line[128];
	if (idx < 0) {
		if (leer_linea(line, sizeof line)) {
			if (sscanf(line, "ID %d", &idx) != 1) {
				// Si no llegó ID, asumimos 0 para no abortar la demo
				idx = 0;
			}
		} else {
			// Sin datos: fallback a 0
			idx = 0;
		}
	}
	if (idx < 0 || idx >= MAX_PLAYERS) {
		fprintf(stderr, "jugador: índice fuera de rango (%d)\n", idx);
		return 1;
	}

	// 2) Adjuntar memorias compartidas
	shared_memories sm = {0};
	if (adjuntar_memorias(&sm) != 0) {
		return 1;
	}

	// Opcional: registrar mi PID si no está seteado (protegiendo escritura)
	// Nota: en el TP usualmente lo hace el máster; mantenemos esto comentado.
	// sem_wait(&sm.sync->mutex_estado_juego);
	// if (sm.state->jugadores[idx].pid == 0) sm.state->jugadores[idx].pid = getpid();
	// sem_post(&sm.sync->mutex_estado_juego);

	// 3) Bucle de turnos: espera semáforo + GO y responde MOVE
	while (1) {
		// Espera de permiso de movimiento (señal del máster)
		if (sem_wait(&sm.game_sync->permiso_movimiento[idx]) == -1) {
			perror("jugador: sem_wait permiso_movimiento");
			break;
		}

		// Espera comando textual (e.g., "GO") por pipe
		if (!leer_linea(line, sizeof line)) {
			// EOF del máster: fin de la partida
			break;
		}

		if (strcmp(line, "GO") != 0) {
			// Protocolo simple: ignoramos líneas que no sean GO
			continue;
		}

		// Leer estado bajo patrón lectores
		empezar_lectura(sm.game_sync);
		bool terminado = sm.game_state->juego_terminado;
		int mi_idx = idx; // local para claridad
		const char *mov = terminado ? "STAY" : elegir_movimiento(sm.game_state, mi_idx);
		terminar_lectura(sm.game_sync);

		// Enviar movimiento al máster
		printf("MOVE %s\n", mov);
		fflush(stdout);

		if (terminado) {
			break;
		}
	}

	desacoplar_memorias(&sm);
	return 0;
}