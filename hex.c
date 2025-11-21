#include "hex.h"
#include <ctype.h>

void board_print(const char* board, int size){
    printf("  ");
    for(int n=0;n<size;n++){
        printf("%c   ",'A'+n);
    }
    printf("\n");
    for(int j=0;j<size;j++){
        for(int n=0;n<j;n++) printf("  ");
        printf("%i",j+1);
        for(int i=0;i<size;i++){
	    printf("   %c",board[size*j+i]);
        } 
        printf("\n");
    }
    printf("\n");
}

void board_clear(char * board,int size){
    for(int i=0;i<size*size;i++){
        board[i]='+';
    }
}

int place_token(char * board,int size,int pos,char player){
    if(!board) return 0;
    if(player!='X' && player!='O') return 0;
    if(pos<0 || pos>=size*size) return 0;
    if(board[pos] != '+') return 0;
    board[pos]=player;
    return 1;
}

token_t board_status(const char * board,int size){
    char result = board_test(board,size);
    if(result=='X') return white;
    if(result=='O') return black;
    return none;
}
int to_ind(int size,int x,int y){
	if(x<0 || x>=size) return -1;
	if(y<0 || y>=size) return -1;
	return size*y+x;
} 
void to_xy(int size, int ind,int* x,int* y){
	*y=ind/size;
	*x=ind%size;
}
static int board_has_connection(const char *board,int size,char token,int horizontal){
	if(!board || size<=0) return 0;
	int stack[MAX_STACK_SIZE];
	char visited[MAX_BOARD_SIZE];
	int top=0;
	for(int i=0;i<size*size;i++) visited[i]=0;
	if(horizontal){
		for(int row=0;row<size;row++){
			int pos=row*size;
			if(board[pos]==token){
				stack[top++]=pos;
				visited[pos]=1;
			}
		}
	}else{
		for(int col=0;col<size;col++){
			int pos=col;
			if(board[pos]==token){
				stack[top++]=pos;
				visited[pos]=1;
			}
		}
	}
	if(top==0) return 0;
	while(top>0){
		int pos=stack[--top];
		int x,y;
		int nb[6];
		to_xy(size,pos,&x,&y);
		if(horizontal){
			if(x==size-1) return 1;
		}else{
			if(y==size-1) return 1;
		}
		nb[0]=to_ind(size,x,y-1);
		nb[1]=to_ind(size,x+1,y-1);
		nb[2]=to_ind(size,x+1,y);
		nb[3]=to_ind(size,x,y+1);
		nb[4]=to_ind(size,x-1,y+1);
		nb[5]=to_ind(size,x-1,y);
		for(int i=0;i<6;i++){
			if(nb[i]<0) continue;
			if(visited[nb[i]]) continue;
			if(board[nb[i]]==token){
				stack[top++]=nb[i];
				visited[nb[i]]=1;
			}
		}
	}
	return 0;
}

int board_test_x(const char * board,int size){
	return board_has_connection(board,size,'X',1);
}
int board_test_o(const char * board,int size){
	return board_has_connection(board,size,'O',0);
}
// Evalua el estado del tablero
// Regresa:
//  "X": si el jugador X conecto los lados izquierdo y derecho
//  "O": si el jugador O conecto los lados izquierdo y derecho
//  "+": si no hay conexion ganadora
char board_test(const char* board,int size){
	if(board_test_x(board,size)) return 'X';
	if(board_test_o(board,size)) return 'O';
	return '+';
}
// simula ubn juego a partir de la pposicion
static char game_sim(const char* board,int size,char player){
	char bcopy[MAX_BOARD_SIZE];
	for(int i=0;i<size*size;i++) bcopy[i]=board[i];
	char turn=player;
	char out;
	uint64_t move;
	int attempts=0;
	int max_attempts=(size*size)*3;
	while(attempts<max_attempts){
		move=pcg32_boundedrand(size*size);
		if(bcopy[move]!='+'){ attempts++; continue; }
		bcopy[move]=turn;
		out=board_test(bcopy,size);
		if(out!='+') break;
		turn=(turn=='X')?'O':'X';
		attempts++;
	}
	return out;
} 
//Simula muchos juegos y obtiene estadisticas, luego decido donde jugar
//Usa Monte Carlo Tree Search (MCTS) simple con distribución adaptativa
void game_stats(const char* board,int size, char player, int nsim, int64_t* stat){
	char base[MAX_BOARD_SIZE];
	int moves[MAX_BOARD_SIZE];
	int move_count=0;
	for(int i=0;i<size*size;i++){
		base[i]=board[i];
		if(board[i]=='+'){
			stat[i]=0;
			moves[move_count++]=i;
		}else{
			stat[i]=INT64_MIN;
		}
	}
	if(move_count==0 || nsim<=0) return;
	
	char other =(player=='X')?'O':'X';
	
	// Primera pasada: evaluación rápida de todos los movimientos
	int quick_sims = nsim / (move_count * 2);
	if(quick_sims < 10) quick_sims = 10;
	
	int64_t quick_results[MAX_BOARD_SIZE];
	for(int i=0;i<size*size;i++) quick_results[i]=0;
	
	for(int idx=0; idx<move_count; idx++){
		int pos = moves[idx];
		for(int r=0; r<quick_sims; r++){
			base[pos]=player;
			char out=game_sim(base,size,other);
			base[pos]='+';
			if(out==player) quick_results[pos]++;
			else quick_results[pos]--;
		}
	}
	
	// Segunda pasada: asignar simulaciones restantes a movimientos prometedores
	int64_t total_sims = nsim - (quick_sims * move_count);
	int64_t best_score = INT64_MIN;
	for(int i=0; i<move_count; i++) best_score = (quick_results[moves[i]] > best_score) ? quick_results[moves[i]] : best_score;
	
	for(int idx=0; idx<move_count; idx++){
		int pos = moves[idx];
		int64_t score = quick_results[pos];
		
		// Asignar más simulaciones a movimientos mejores (UCB-like)
		int extra_sims = 0;
		if(best_score > 0){
			extra_sims = (int)((score + best_score + 1) * total_sims / (move_count * (best_score * 2 + 1)));
		}else{
			extra_sims = (int)(total_sims / move_count);
		}
		
		for(int r=0; r<extra_sims; r++){
			base[pos]=player;
			char out=game_sim(base,size,other);
			base[pos]='+';
			if(out==player) stat[pos]++;
			else stat[pos]--;
		}
		stat[pos] += quick_results[pos];
	}
}
int game_move(int64_t* stats,int size){
	int move=-1;
	for(int i=0;i<size*size;i++){
		if(stats[i]==INT64_MIN) continue;
		if(move<0 || stats[i]> stats[move])move=i;
	}
	return (move>=0)?move:0;
}
void trim(char * str){ 
	int i=0;
	while(str[i]!='\0'){
		if(str[i]=='\n' || str[i]=='\r'){
			str[i]='\0';
			break;
		}
		i++;
	}
}
int read_move(const char * buffer,int size){
	if(!buffer) return -1;
	while(*buffer!='\0' && isspace((unsigned char)*buffer)) buffer++;
	if(*buffer=='\0') return -1;

	char col=*buffer++;
	if(col>='a' && col<='z') col= (char)(col-'a'+'A');
	if(col<'A' || col>='A'+size) return -1;
	int x=col-'A';

	while(*buffer!='\0' && isspace((unsigned char)*buffer)) buffer++;
	if(*buffer=='\0') return -1;

	char *endptr;
	long row = strtol(buffer,&endptr,10);
	if(endptr==buffer) return -1;
	while(*endptr!='\0'){
		if(!isspace((unsigned char)*endptr)) return -1;
		endptr++;
	}
	if(row<1 || row>size) return -1;
	int y=(int)row-1;
	return to_ind(size,x,y);
}
