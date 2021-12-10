#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <sys/socket.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <netdb.h>
#include <dirent.h>
#include <errno.h>

#define BOARD_SIZE 10
#define LISTENQ  1024  /* Second argument to listen() */
#define START_OF_MESSAGE 10352040
typedef struct sockaddr SA;

typedef enum
{
    TILE_GRASS,
    TILE_TOMATO,
    TILE_PLAYER
} TILETYPE;

typedef enum
{
    QUIT,
    MOVE,
    UPDATE
} COMMAND;

typedef enum
{
    UP,
    DOWN,
    LEFT,
    RIGHT,
    NONE
} DIRECTION;


TILETYPE board[BOARD_SIZE][BOARD_SIZE] = {};

typedef struct _playerInfo {
    int id;
    int x; 
    int y;
    struct _playerInfo* next;
} playerInfo;

typedef struct _boardInfo {
    int score;
    int level;
    int numTomatoes;
    int players;
    playerInfo* head;
} boardMetadata;

int receiveInt(int fd);
void sendInt(int fd, int value);
void initGame();
void sendInt(int fd, int value);
int receiveInt(int fd);
void generateBoard();
void sendStart(int clientfd);

int getPlayerID();
int checkValidMove(DIRECTION* move, playerInfo* player, int* x, int* y);
void updateBoard(int x, int y, playerInfo* player);
int receiveCommand(int clientfd, DIRECTION* move, COMMAND* command);
void addPlayer(playerInfo* player);
int updatePlayer(playerInfo* player);
void generateRandomUnoccupiedPosition(int* x, int* y);
void sendPlayerPosition(int clientfd, playerInfo* player);
void sendBoard(int clientfd);
void sendBoardInfo(int clientfd);
void sendPlayers(int clientfd, playerInfo* player);
void updateBoard(int x, int y, playerInfo* player);
void* game(void* clientfdptr);
int removePlayer(playerInfo* player);
void printBoard();


int playerIDs = 0;
pthread_mutex_t playerIDsLock;
pthread_mutex_t boardLock;
boardMetadata boardInfo; 

/*  
 * open_listenfd - Open and return a listening socket on port. This
 *     function is reentrant and protocol-independent.
 *
 *     On error, returns: 
 *       -2 for getaddrinfo error
 *       -1 with errno set for other errors.
 */
/* $begin open_listenfd */
int open_listenfd(char *port) 
{
    struct addrinfo hints, *listp, *p;
    int listenfd, rc, optval=1;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;             /* Accept connections */
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG; /* ... on any IP address */
    hints.ai_flags |= AI_NUMERICSERV;            /* ... using port number */
    if ((rc = getaddrinfo(NULL, port, &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo failed (port %s): %s\n", port, gai_strerror(rc));
        return -2;
    }

    /* Walk the list for one that we can bind to */
    for (p = listp; p; p = p->ai_next) {
        /* Create a socket descriptor */
        if ((listenfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) 
            continue;  /* Socket failed, try the next */

        /* Eliminates "Address already in use" error from bind */
        setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR,    //line:netp:csapp:setsockopt
                   (const void *)&optval , sizeof(int));

        /* Bind the descriptor to the address */
        if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
            break; /* Success */
        if (close(listenfd) < 0) { /* Bind failed, try the next */
            fprintf(stderr, "open_listenfd close failed: %s\n", strerror(errno));
            return -1;
        }
    }


    /* Clean up */
    freeaddrinfo(listp);
    if (!p) /* No address worked */
        return -1;

    /* Make it a listening socket ready to accept connection requests */
    if (listen(listenfd, LISTENQ) < 0) {
        close(listenfd);
	return -1;
    }
    return listenfd;
}
/* $end open_listenfd */

int main(int argc, char** argv) {
    if (argc != 2) {
        fprintf(stderr, "Please add a port to the arguments\n");
        exit(0);
    }
    int listenfd, *clientSocket;
    listenfd = open_listenfd(argv[1]);
	if (listenfd < 0) {
		fprintf(stderr, "Error setting up listening socket\n");
        exit(0);
	}
	srand(time(NULL));
	initGame();

    socklen_t clientlen = sizeof(struct sockaddr_storage);
    struct sockaddr_storage clientaddr;
    

    while (1) {
        clientSocket = malloc(sizeof(int) * 1);
        *clientSocket = accept(listenfd, (SA *) &clientaddr, &clientlen);
        pthread_t tid;
        pthread_create(&tid, NULL, game, clientSocket);
    }
}

int receiveInt(int fd) {
    int status = 0;
    int bytesRead = 0;
    int value = 0;
    char* data = (char*)&value;
    while (bytesRead < sizeof(int)) {
        status = read (fd, data + bytesRead, (sizeof(int) - bytesRead));
        if (status < 0) {
            fprintf(stderr, "Error Code: %d\nError reading the integer\n", status);
            return -1;
        }
        bytesRead += status;
    }
    return *((int*) data);
}

void sendInt(int fd, int value) {
    char* data = (char*)&value;
    int bytesWritten = 0;
    int status = 0;
    while (bytesWritten < sizeof(int)) {
        status = write(fd, (data + bytesWritten), (sizeof(int) - bytesWritten));
        if (status < 0) {
            fprintf(stderr, "ErrorCode: %d\nCould not write to the client fd %d with value %d \n", status, fd, value);
            return;
        }
        bytesWritten += status;
    }
}

void initGame() {
    pthread_mutex_init(&boardLock, NULL);
    pthread_mutex_init(&playerIDsLock, NULL);
    boardInfo.level = 0;
    boardInfo.score = 0;
    boardInfo.numTomatoes = 0;
    boardInfo.players = 0;
    boardInfo.head = NULL;
    generateBoard();
}

void generateBoard() {
    boardInfo.numTomatoes = 0;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (board[i][j] == TILE_PLAYER) {
                continue;
            }
            double r = ((double) rand()) / ((double) RAND_MAX);
            if (r < 0.1) {
                board[i][j] = TILE_TOMATO;
                boardInfo.numTomatoes++;
            } else {
                board[i][j] = TILE_GRASS;
            }
        }
    }
    while (boardInfo.numTomatoes == 0) {
        generateBoard();
    }
}

int getPlayerID() {
    pthread_mutex_lock(&playerIDsLock);
    int id = playerIDs++;
    pthread_mutex_unlock(&playerIDsLock);
    return id;
}

void* game(void* clientfdptr) {
    int clientfd = *((int*) clientfdptr);
    pthread_detach(pthread_self());
    free(clientfdptr);
    
    playerInfo* player = malloc(sizeof(playerInfo) * 1);
    player->id = getPlayerID();
    
    pthread_mutex_lock(&boardLock);
    generateRandomUnoccupiedPosition(&player->x, &player->y);
    board[player->x][player->y] = TILE_PLAYER;
    // addPlayer(player);
    boardInfo.players++;
    pthread_mutex_unlock(&boardLock);
    
    COMMAND command = UPDATE;
	DIRECTION move = NONE;
    do {
        pthread_mutex_lock(&boardLock);
        printf("Player ID %d with Command %d\n", player->id, command);
        if (command == MOVE) {
            int moveToX;
        	int moveToY;
            if (checkValidMove(&move, player, &moveToX, &moveToY) == 1) {
                updateBoard(moveToX, moveToY, player);
            }
        }
        if (command == UPDATE) {
        	// sendStart(clientfd);
            sendPlayerPosition(clientfd, player);
            sendBoard(clientfd);
        	sendBoardInfo(clientfd);
        	// sendPlayers(clientfd, player);
        }
        // printBoard();
        pthread_mutex_unlock(&boardLock);
    } while (receiveCommand(clientfd, &move, &command) == 1);
    printf("Player ID %d is no longer connected\n", player->id);
    pthread_mutex_lock(&boardLock);
    boardInfo.players--;
    //removePlayer(player);
    board[player->x][player->y] = TILE_GRASS;
    pthread_mutex_unlock(&boardLock);
    // TODO: Clean up the malloced player (will have to fix the LL)
    printf("Done with cleanup of ID %d\n", player->id);
    free(player);
    return NULL;
}


int checkValidMove(DIRECTION* move, playerInfo* player, int* x, int* y) {
    switch (*move) {
    	case UP:
    		*x = player->x;
    		*y = player->y - 1;
    		break;
    	case DOWN:
    		*x = player->x;
    		*y = player->y + 1;
    		break;
    	case LEFT:
    		*x = player->x - 1;
    		*y = player->y;
    		break;
    	case RIGHT:
    	   	*x = player->x + 1;
    		*y = player->y;
    		break;
		default:
			printf("ERROR in checking move\n");
			return -1;
    }
    
    if (*x < 0 || *x >= BOARD_SIZE || *y < 0 || *y >= BOARD_SIZE || board[*x][*y] == TILE_PLAYER) {
    	return -1;
    }
    return 1;
} 


void updateBoard(int x, int y, playerInfo* player) {
	board[player->x][player->y] = TILE_GRASS;
	player->x = x;
	player->y = y;
    if (board[x][y] == TILE_TOMATO) {
        boardInfo.score++;
        boardInfo.numTomatoes--;
    }
    board[x][y] = TILE_PLAYER;
    if (boardInfo.numTomatoes == 0) {
        boardInfo.level++;
        generateBoard();
    }
}

int receiveCommand(int clientfd, DIRECTION* move, COMMAND* command) {
    *command = receiveInt(clientfd);
    switch (*command) {
        case MOVE: 
            *move = receiveInt(clientfd);
            printf("[Move] ClientID %d with move %d\n", clientfd, *move);
            return 1;
        case QUIT:
            // Not supported currently, could clean up?
            return -1;
        case UPDATE:
        	printf("[Update] ClientID %d\n", clientfd);
        	return 1;
        default:
            fprintf(stderr, "Invalid command from client %d\n", *command);
            return -1;
    }
}


void addPlayer(playerInfo* player) {
	player->next = boardInfo.head;
    boardInfo.head = player;
}

int removePlayer(playerInfo* player) {
	playerInfo* current = boardInfo.head;
	playerInfo* previous = NULL;
	printf("Removing player %d\n", player->id);
    while (current != NULL) {
    	printf("Current player %d\n", current->id);
        if (current->id == player->id) {
        	if (boardInfo.head == current) {
        		boardInfo.head = current->next;
        	} else {
            	previous->next = current->next;
            }
            return 1;
        }
        previous = current;
        current = current->next;
    }
    return -1;
}

int updatePlayer(playerInfo* player) {
    playerInfo* current = boardInfo.head;
    while (current != NULL) {
        if (current->id == player->id) {
            current->x = player->x;
            current->y = player->y;
            return 1;
        }
        current = current->next;
    }
    return -1;
}

void generateRandomUnoccupiedPosition(int* x, int* y) {
    int randX = rand() % BOARD_SIZE;
    int randY = rand() % BOARD_SIZE;
    // TODO: What happens if the whole board is filled with players + Tomatoes
    while (board[randX][randY] != TILE_GRASS) {
        randX = rand() % BOARD_SIZE;
        randY = rand() % BOARD_SIZE;
    }
    *x = randX;
    *y = randY;
}

void sendStart(int clientfd) {
	sendInt(clientfd, START_OF_MESSAGE);
}

void sendPlayerPosition(int clientfd, playerInfo* player) {
    sendInt(clientfd, player->x);
    sendInt(clientfd, player->y);
}

void sendBoard(int clientfd) {
    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            sendInt(clientfd, board[row][col]);
        }
    }
}

void sendBoardInfo(int clientfd) {
    sendInt(clientfd, boardInfo.score);
    sendInt(clientfd, boardInfo.level);
    // sendInt(clientfd, boardInfo.numTomatoes);
}

void sendPlayers(int clientfd, playerInfo* player) {
    playerInfo* current = boardInfo.head;
    sendInt(clientfd, boardInfo.players - 1);
    printf("Other players %d\n", boardInfo.players - 1);
    while (current != NULL) {
        if (current->id != player->id) {
        	printf("Sent [%d, %d]\n", current->x, current->y);
            sendInt(clientfd, current->x);
            sendInt(clientfd, current->y);
        }
        current = current->next;
    }
}

void printBoard() {
	printf("Score: %d\nLevel: %d\nTomatoes: %d\nPlayers: %d\n", boardInfo.score, boardInfo.level, boardInfo.numTomatoes, boardInfo.players);
	for (int col = 0; col < BOARD_SIZE; col++) {
        for (int row = 0; row < BOARD_SIZE; row++) {
            printf("%d ", board[row][col]);
        }
        printf("\n");
    }
    printf("\n");
}

