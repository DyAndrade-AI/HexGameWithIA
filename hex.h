#ifndef HEX_H
#define HEX_H
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "pcg_basic.h"

#define MAX_BOARD_SIDE 26
#define MAX_BOARD_SIZE (MAX_BOARD_SIDE * MAX_BOARD_SIDE)
#define MAX_STACK_SIZE MAX_BOARD_SIZE
#define MAX_PROC 32

typedef enum {
	none,
	white,
	black
} token_t;

typedef enum {
	TL,
	TR,
	ML,
	MR,
	BL,
	BR
} dir_t;

void board_print(const char * board,int size);
void board_clear(char * board,int size);
int place_token(char * board,int size,int pos,char player);
token_t board_status(const char * board,int size);
int to_ind(int size,int x,int y);
void to_xy(int size, int ind,int* x,int* y);
void game_stats(const char* board,int size, char player, int nsim, int64_t* stat);
int board_test_x(const char * board,int size);
int board_test_o(const char * board,int size);
char board_test(const char* board,int size);
int game_move(int64_t* stats,int size);
int read_move(const char* buffer,int size);
void trim(char * str);
#endif 
