#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "hex.h"
#include "ui.h"

#define BUFLEN 128
#define DEFAULT_BOARD_SIZE 7
#define DEFAULT_SIMULATIONS 4000
#define MIN_BOARD_SIZE 7
#define MIN_SIMULATIONS 100
#define MAX_SIMULATIONS 1000000

typedef struct {
	int to_child[2];
	int from_child[2];
	pid_t pid;
} worker_t;

typedef struct {
	int cmd;
	int size;
	int nsim;
	char player;
	char board[MAX_BOARD_SIZE];
} worker_request_t;

typedef struct {
	int cmd;
	int size;
	int64_t stats[MAX_BOARD_SIZE];
} worker_response_t;

static worker_t workers[MAX_PROC];
static int worker_count = 0;
static int gui_enabled = 0;
static char status_line[256];
static volatile int shutdown_requested = 0;

static void signal_handler(int sig) {
	(void)sig;
	shutdown_requested = 1;
}

static int board_has_free(const char * board,int size);
static int determine_default_workers(void);
static ssize_t read_full(int fd, void *buf, size_t count);
static ssize_t write_full(int fd, const void *buf, size_t count);
static void worker_loop(int read_fd, int write_fd);
static int spawn_workers(int count);
static void stop_workers(void);
static int parallel_stats(const char *board,int size,char player,int total_sims,int64_t *stats);
static int get_line(const char *prompt, char *buffer, size_t len);
static void announce_board(const char *board, int size);

int main(int argc, char **argv) {
	int use_gui = 1;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--no-gui") == 0) {
			use_gui = 0;
		} else if (strcmp(argv[i], "--gui") == 0) {
			use_gui = 1;
		}
	}

	if (use_gui) {
		if (isatty(STDIN_FILENO) && isatty(STDOUT_FILENO) && ui_init() == 0) {
			gui_enabled = 1;
		} else {
			fprintf(stderr, "No se pudo iniciar la interfaz grafica. Continuando en modo texto.\n");
		}
	}

	signal(SIGPIPE, SIG_IGN);
	signal(SIGINT, signal_handler);
	signal(SIGTERM, signal_handler);

	char board[MAX_BOARD_SIZE];
	int64_t stats[MAX_BOARD_SIZE];
	char buffer[BUFLEN];
	int size = DEFAULT_BOARD_SIZE;
	int nsim = DEFAULT_SIMULATIONS;
	int requested_workers = determine_default_workers();

	board_clear(board, size);
	pcg32_srandom((uint64_t)time(NULL), (uint64_t)getpid());
	snprintf(status_line, sizeof(status_line), "Configura el tamano, simulaciones y procesos.");

	announce_board(board, size);

	// Board size prompt
	while (1) {
		if (!get_line("Tamano del tablero (2-26) [7]: ", buffer, sizeof(buffer))) {
			buffer[0] = '\0';
		}
		if (buffer[0] == '\0') break;
		char *endptr = NULL;
		long value = strtol(buffer, &endptr, 10);
		if (endptr != buffer && *endptr == '\0' && value >= MIN_BOARD_SIZE && value <= MAX_BOARD_SIDE) {
			size = (int)value;
			break;
		}
		snprintf(status_line, sizeof(status_line), "Valor invalido. Usa un numero entre %d y %d.", MIN_BOARD_SIZE, MAX_BOARD_SIDE);
		announce_board(board, size);
	}
	board_clear(board, size);

	// Simulations prompt
	while (1) {
		if (!get_line("Simulaciones totales [4000] (hasta 1M): ", buffer, sizeof(buffer))) {
			buffer[0] = '\0';
		}
		if (buffer[0] == '\0') break;
		char *endptr = NULL;
		long value = strtol(buffer, &endptr, 10);
		if (endptr != buffer && *endptr == '\0' && value >= MIN_SIMULATIONS && value <= MAX_SIMULATIONS) {
			nsim = (int)value;
			break;
		}
		snprintf(status_line, sizeof(status_line), "Valor invalido. Usa un numero entre %d y %d.", MIN_SIMULATIONS, MAX_SIMULATIONS);
		announce_board(board, size);
	}

	// Worker prompt
	while (1) {
		if (!get_line("Procesos de simulacion (1-32) [auto]: ", buffer, sizeof(buffer))) {
			buffer[0] = '\0';
		}
		if (buffer[0] == '\0') break;
		char *endptr = NULL;
		long value = strtol(buffer, &endptr, 10);
		if (endptr != buffer && *endptr == '\0' && value >= 1 && value <= MAX_PROC) {
			requested_workers = (int)value;
			break;
		}
		snprintf(status_line, sizeof(status_line), "Valor invalido. Introduce un numero entre 1 y %d.", MAX_PROC);
		announce_board(board, size);
	}

	if (spawn_workers(requested_workers) != 0) {
		fprintf(stderr, "No se pudieron crear trabajadores. Ejecutando en modo secuencial.\n");
	}

	snprintf(status_line, sizeof(status_line), "Tu eres X (izquierda-derecha), la computadora es O (arriba-abajo).");
	announce_board(board, size);

	char turn = 'X';
	int aborted = 0;

	while (1) {
		if (shutdown_requested) {
			aborted = 1;
			break;
		}
		token_t status = board_status(board, size);
		if (status == white) {
			snprintf(status_line, sizeof(status_line), "Gana X (izquierda-derecha).");
			break;
		} else if (status == black) {
			snprintf(status_line, sizeof(status_line), "Gana O (arriba-abajo).");
			break;
		}

		if (!board_has_free(board, size)) {
			snprintf(status_line, sizeof(status_line), "No quedan movimientos disponibles.");
			break;
		}

		announce_board(board, size);

		if (turn == 'X') {
			int move = -1;
			if (gui_enabled) {
				snprintf(status_line, sizeof(status_line), "Haz clic en una casilla (Q para salir).");
				announce_board(board, size);
				int ok = ui_wait_move(size, &move);
				if (!ok) {
					aborted = 1;
					break;
				}
				if (!place_token(board, size, move, 'X')) {
					snprintf(status_line, sizeof(status_line), "Casilla ocupada o invalida.");
					continue;
				}
			} else {
				if (!get_line("Juega X. Ingresa movimiento (A1) o Q para salir: ", buffer, sizeof(buffer))) {
					aborted = 1;
					break;
				}
				if (buffer[0] == '\0') {
					snprintf(status_line, sizeof(status_line), "Entrada vacia.");
					continue;
				}
				if ((buffer[0] == 'Q' || buffer[0] == 'q') && buffer[1] == '\0') {
					aborted = 1;
					break;
				}
				move = read_move(buffer, size);
				if (move < 0) {
					snprintf(status_line, sizeof(status_line), "Movimiento invalido. Usa formato letra-numero.");
					continue;
				}
				if (!place_token(board, size, move, 'X')) {
					snprintf(status_line, sizeof(status_line), "Casilla ocupada o invalida.");
					continue;
				}
			}
			int x, y;
			to_xy(size, move, &x, &y);
			snprintf(status_line, sizeof(status_line), "Colocaste X en %c%d.", 'A' + x, y + 1);
			turn = 'O';
		} else {
			snprintf(status_line, sizeof(status_line), "La computadora esta pensando...");
			announce_board(board, size);
			if (parallel_stats(board, size, 'O', nsim, stats) != 0) {
				game_stats(board, size, 'O', nsim, stats);
			}
			int move = game_move(stats, size);
			if (!place_token(board, size, move, 'O')) {
				for (int i = 0; i < size * size; i++) {
					if (board[i] == '+') {
						move = i;
						place_token(board, size, move, 'O');
						break;
					}
				}
			}
			int x, y;
			to_xy(size, move, &x, &y);
			snprintf(status_line, sizeof(status_line), "La computadora juega %c%d.", 'A' + x, y + 1);
			turn = 'X';
		}
	}

	stop_workers();

	if (aborted) {
		snprintf(status_line, sizeof(status_line), "Juego terminado por el usuario.");
	}

	if (gui_enabled) {
		announce_board(board, size);
		char dummy[4];
		get_line("Presiona ENTER para salir.", dummy, sizeof(dummy));
		ui_shutdown();
	} else {
		announce_board(board, size);
	}

	return 0;
}

static int board_has_free(const char * board,int size){
	for(int i=0;i<size*size;i++){
		if(board[i]=='+') return 1;
	}
	return 0;
}

static int determine_default_workers(void) {
	long cpus = sysconf(_SC_NPROCESSORS_ONLN);
	if (cpus <= 0) return 2;
	if (cpus > MAX_PROC) cpus = MAX_PROC;
	return (int)cpus;
}

static ssize_t read_full(int fd, void *buf, size_t count) {
	size_t left = count;
	char *ptr = buf;
	while (left > 0) {
		ssize_t r = read(fd, ptr, left);
		if (r < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		if (r == 0) return count - left;
		left -= (size_t)r;
		ptr += r;
	}
	return (ssize_t)count;
}

static ssize_t write_full(int fd, const void *buf, size_t count) {
	size_t left = count;
	const char *ptr = buf;
	while (left > 0) {
		ssize_t r = write(fd, ptr, left);
		if (r < 0) {
			if (errno == EINTR) continue;
			return -1;
		}
		left -= (size_t)r;
		ptr += r;
	}
	return (ssize_t)count;
}

static void worker_loop(int read_fd, int write_fd) {
	worker_request_t request;
	worker_response_t response;
	pcg32_srandom((uint64_t)time(NULL), (uint64_t)getpid());
	while (1) {
		memset(&request, 0, sizeof(request));
		ssize_t got = read_full(read_fd, &request, sizeof(request));
		if (got != (ssize_t)sizeof(request)) break;
		if (request.cmd == 0) break;
		if (request.cmd == 1) {
			if (request.size <= 0 || request.nsim <= 0) continue;
			memset(&response, 0, sizeof(response));
			response.cmd = 1;
			response.size = request.size;
			game_stats(request.board, request.size, request.player, request.nsim, response.stats);
			if (write_full(write_fd, &response, sizeof(response)) != (ssize_t)sizeof(response)) {
				break;
			}
		}
	}
	close(read_fd);
	close(write_fd);
	_exit(0);
}

static int spawn_workers(int count) {
	if (count < 1) count = 1;
	if (count > MAX_PROC) count = MAX_PROC;
	for (int i = 0; i < count; i++) {
		if (pipe(workers[i].to_child) != 0) {
			stop_workers();
			return -1;
		}
		if (pipe(workers[i].from_child) != 0) {
			close(workers[i].to_child[0]);
			close(workers[i].to_child[1]);
			stop_workers();
			return -1;
		}
		pid_t pid = fork();
		if (pid < 0) {
			close(workers[i].to_child[0]);
			close(workers[i].to_child[1]);
			close(workers[i].from_child[0]);
			close(workers[i].from_child[1]);
			stop_workers();
			return -1;
		} else if (pid == 0) {
			close(workers[i].to_child[1]);
			close(workers[i].from_child[0]);
			worker_loop(workers[i].to_child[0], workers[i].from_child[1]);
		} else {
			workers[i].pid = pid;
			close(workers[i].to_child[0]);
			close(workers[i].from_child[1]);
			worker_count++;
		}
	}
	return 0;
}

static void stop_workers(void) {
	worker_request_t request = {0};
	request.cmd = 0;
	for (int i = 0; i < worker_count; i++) {
		write_full(workers[i].to_child[1], &request, sizeof(request));
		close(workers[i].to_child[1]);
		close(workers[i].from_child[0]);
	}
	for (int i = 0; i < worker_count; i++) {
		int status;
		waitpid(workers[i].pid, &status, 0);
	}
	worker_count = 0;
}

static int parallel_stats(const char *board,int size,char player,int total_sims,int64_t *stats) {
	if (worker_count <= 0 || total_sims <= 0 || size <= 0) {
		game_stats(board, size, player, total_sims, stats);
		return 0;
	}

	for (int i = 0; i < size * size; i++) {
		stats[i] = (board[i] == '+') ? 0 : INT64_MIN;
	}

	worker_request_t request;
	memset(&request, 0, sizeof(request));
	request.cmd = 1;
	request.size = size;
	request.player = player;
	memcpy(request.board, board, (size_t)size * (size_t)size);

	int base = total_sims / worker_count;
	int remainder = total_sims % worker_count;
	int used_workers[MAX_PROC];
	int active = 0;

	for (int i = 0; i < worker_count; i++) {
		int share = base + (i < remainder ? 1 : 0);
		if (share < 1) share = 1;
		request.nsim = share;
		if (write_full(workers[i].to_child[1], &request, sizeof(request)) != (ssize_t)sizeof(request)) {
			stop_workers();
			game_stats(board, size, player, total_sims, stats);
			return -1;
		}
		used_workers[active++] = i;
	}

	if (active == 0) {
		game_stats(board, size, player, total_sims, stats);
		return 0;
	}

	for (int idx = 0; idx < active; idx++) {
		int worker_index = used_workers[idx];
		worker_response_t response;
		memset(&response, 0, sizeof(response));
		if (read_full(workers[worker_index].from_child[0], &response, sizeof(response)) != (ssize_t)sizeof(response)) {
			stop_workers();
			game_stats(board, size, player, total_sims, stats);
			return -1;
		}
		for (int k = 0; k < size * size; k++) {
			if (response.stats[k] == INT64_MIN) continue;
			if (stats[k] == INT64_MIN) continue;
			stats[k] += response.stats[k];
		}
	}
	return 0;
}

static int get_line(const char *prompt, char *buffer, size_t len) {
	if (gui_enabled) {
		if (!ui_prompt(prompt, buffer, len)) {
			buffer[0] = '\0';
			return 0;
		}
		trim(buffer);
		return 1;
	}
	printf("%s", prompt);
	fflush(stdout);
	if (!fgets(buffer, (int)len, stdin)) {
		buffer[0] = '\0';
		return 0;
	}
	trim(buffer);
	return 1;
}

static void announce_board(const char *board, int size) {
	if (gui_enabled) {
		ui_draw_board(board, size, status_line);
	} else {
		board_print(board, size);
		printf("%s\n", status_line);
	}
}
