#include <stdio.h>
#include <stdbool.h>
#include <time.h>
#include <sys/socket.h>
#include <dirent.h>

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#include "csapp.h"

// Dimensions for the drawn grid (should be GRIDSIZE * texture dimensions)
#define GRID_DRAW_WIDTH 640
#define GRID_DRAW_HEIGHT 640

#define WINDOW_WIDTH GRID_DRAW_WIDTH
#define WINDOW_HEIGHT (HEADER_HEIGHT + GRID_DRAW_HEIGHT)

// Header displays current score
#define HEADER_HEIGHT 50

// Number of cells vertically/horizontally in the grid
#define GRIDSIZE 10

#define START_OF_MESSAGE 10352040

typedef struct
{
    int x;
    int y;
} Position;

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
    RIGHT
} DIRECTION;

typedef struct _playerInfo {
    int playerID;
    int x; 
    int y;
    struct _playerInfo* next;
} playerInfo;
void printBoard();


TILETYPE grid[GRIDSIZE][GRIDSIZE];

Position playerPosition;
int score;
int level;
int numTomatoes;
int players;

bool shouldExit = false;

TTF_Font* font;
int clientfd;
playerInfo* head = NULL;
int AI = 0;
playerInfo closestPlayer;
playerInfo closestTomato;
void moveRandom();
void moveTowardsClosestPlayer();
void moveTowardsClosestObject(int differenceHorizontal, int differenceVertical);
int expectedPlayerPositionX = 0;
int expectedPlayerPositionY = 0;
int desync = 0;
// get a random value in the range [0, 1]
double rand01()
{
    return (double) rand() / (double) RAND_MAX;
}

/*
 * open_clientfd - Open connection to server at <hostname, port> and
 *     return a socket descriptor ready for reading and writing. This
 *     function is reentrant and protocol-independent.
 *
 *     On error, returns: 
 *       -2 for getaddrinfo error
 *       -1 with errno set for other errors.
 */
/* $begin open_clientfd */
int open_clientfd(char *hostname, char *port) {
    int clientfd, rc;
    struct addrinfo hints, *listp, *p;

    /* Get a list of potential server addresses */
    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_socktype = SOCK_STREAM;  /* Open a connection */
    hints.ai_flags = AI_NUMERICSERV;  /* ... using a numeric port arg. */
    hints.ai_flags |= AI_ADDRCONFIG;  /* Recommended for connections */
    if ((rc = getaddrinfo(hostname, port, &hints, &listp)) != 0) {
        fprintf(stderr, "getaddrinfo failed (%s:%s): %s\n", hostname, port, gai_strerror(rc));
        return -2;
    }
  
    /* Walk the list for one that we can successfully connect to */
    for (p = listp; p; p = p->ai_next) {
        /* Create a socket descriptor */
        if ((clientfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) 
            continue; /* Socket failed, try the next */

        /* Connect to the server */
        if (connect(clientfd, p->ai_addr, p->ai_addrlen) != -1) 
            break; /* Success */
        if (close(clientfd) < 0) { /* Connect failed, try another */  //line:netp:openclientfd:closefd
            fprintf(stderr, "open_clientfd: close failed: %s\n", strerror(errno));
            return -1;
        } 
    } 

    /* Clean up */
    freeaddrinfo(listp);
    if (!p) /* All connects failed */
        return -1;
    else    /* The last connect succeeded */
        return clientfd;
}
/* $end open_clientfd */

int readInt(int fd) {
    int status = 0;
    int bytesRead = 0;
    int value = 0;
    char* data = (char*)&value;
    while (bytesRead < sizeof(int)) {
        status = read(fd, data + bytesRead, (sizeof(int) - bytesRead));
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

void processServerResponse() {
	/*
	int start = readInt(clientfd);
	if (start != START_OF_MESSAGE) {
		printf("Garbage: ");
		while (start != START_OF_MESSAGE) {
			printf("%d ", start);
			start = readInt(clientfd);
		}
		printf("\n");
		exit(0);
	}
	*/
    printf("Reading Player Position: ");
    playerPosition.x = readInt(clientfd);
    playerPosition.y = readInt(clientfd);
    printf("%d %d\n", playerPosition.x, playerPosition.y);
    if (expectedPlayerPositionX != playerPosition.x || expectedPlayerPositionY != playerPosition.y) {
    	printf("Desync: Expected Position [%d %d] | Received [%d %d]\n", expectedPlayerPositionX, expectedPlayerPositionY, playerPosition.x, playerPosition.y);
    	expectedPlayerPositionX = playerPosition.x;
    	expectedPlayerPositionY = playerPosition.y;
    	desync = 1;
    } else {
    	desync = 0;
    }
    
    int closest = 100000;
    int closestTom = 100000;
    if (head != NULL) {
    	printf("ERROR: head was not NULL");
    	exit(0);
    }
	printf("Reading board\n");
    players = 0;
    numTomatoes = 0;
    for (int row = 0; row < GRIDSIZE; row++) {
        for (int col = 0; col < GRIDSIZE; col++) {
            grid[row][col] = readInt(clientfd);
            if (grid[row][col] == TILE_PLAYER) {
                if (row == playerPosition.x && col == playerPosition.y) {
                    continue;
                }
                players++;
                playerInfo* otherPlayer = malloc(sizeof(playerInfo) * 1);
                printf("Other Player: ");
                otherPlayer->x = row;
                otherPlayer->y = col;
                if (AI == 1) {
                    int distance = abs(playerPosition.x - otherPlayer->x) + abs(playerPosition.y - otherPlayer->y);
                    if (distance < closest) {
                        closestPlayer.x = otherPlayer->x;
                        closestPlayer.y = otherPlayer->y;
                        closest = distance;
                    }
                }
                printf("%d, %d\n", otherPlayer->x, otherPlayer->y);  
                otherPlayer->next = head;
                head = otherPlayer;         
            } else if (grid[row][col] == TILE_TOMATO) {
                numTomatoes++;
                if (AI == 3) {
                    int distance = abs(playerPosition.x - row) + abs(playerPosition.y - col);
                    if (distance < closestTom) {
                        closestTomato.x = row;
                        closestTomato.y = col;
                        closestTom = distance;
                    }
                }
            }
        }
    }
    printBoard();
    printf("Number of Other Players %d\n", players);
    printf("Tomatoes: %d\n", numTomatoes);
    printf("Finish reading board\n");
    score = readInt(clientfd);
    printf("Score: %d\n", score);
    level = readInt(clientfd);
    printf("Level: %d\n", level);
    printf("Finished processing\n");
}

void initGrid()
{
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            double r = rand01();
            if (r < 0.1) {
                grid[i][j] = TILE_TOMATO;
                numTomatoes++;
            }
            else
                grid[i][j] = TILE_GRASS;
        }
    }

    // force player's position to be grass
    if (grid[playerPosition.x][playerPosition.y] == TILE_TOMATO) {
        grid[playerPosition.x][playerPosition.y] = TILE_GRASS;
        numTomatoes--;
    }

    // ensure grid isn't empty
    while (numTomatoes == 0)
        initGrid();
}

void initSDL()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Error initializing SDL: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    int rv = IMG_Init(IMG_INIT_PNG);
    if ((rv & IMG_INIT_PNG) != IMG_INIT_PNG) {
        fprintf(stderr, "Error initializing IMG: %s\n", IMG_GetError());
        exit(EXIT_FAILURE);
    }

    if (TTF_Init() == -1) {
        fprintf(stderr, "Error initializing TTF: %s\n", TTF_GetError());
        exit(EXIT_FAILURE);
    }
}

void moveTo(int x, int y, DIRECTION direction)
{
    // Prevent falling off the grid
    if (x < 0 || x >= GRIDSIZE || y < 0 || y >= GRIDSIZE)
        return;

    // Sanity check: player can only move to 4 adjacent squares
    if (!(abs(playerPosition.x - x) == 1 && abs(playerPosition.y - y) == 0) &&
        !(abs(playerPosition.x - x) == 0 && abs(playerPosition.y - y) == 1)) {
        fprintf(stderr, "Invalid move attempted from (%d, %d) to (%d, %d)\n", playerPosition.x, playerPosition.y, x, y);
        return;
    }

    playerPosition.x = x;
    playerPosition.y = y;
    sendInt(clientfd, MOVE);
    sendInt(clientfd, direction);
}

void moveTowardsClosestPlayer() {

	int differenceHorizontal = playerPosition.x - closestPlayer.x;
	int differenceVertical = playerPosition.y - closestPlayer.y;
	if (differenceHorizontal < 0) {
		sendInt(clientfd, MOVE);
		sendInt(clientfd, RIGHT);
		expectedPlayerPositionX++;
	}
	
	if (differenceHorizontal > 0) {
		sendInt(clientfd, MOVE);
		sendInt(clientfd, LEFT);
		expectedPlayerPositionX--;
	}
	
	if (differenceVertical < 0) {
		sendInt(clientfd, MOVE);
		sendInt(clientfd, DOWN);
		expectedPlayerPositionY++;
	}
	
	if (differenceVertical > 0) {
		sendInt(clientfd, MOVE);
		sendInt(clientfd, UP);
		expectedPlayerPositionY--;
	}
	
}

void moveTowardsClosestObject(int differenceHorizontal, int differenceVertical) {
    printf("%d %d\n", differenceHorizontal, differenceVertical);
	if (differenceHorizontal < 0) {
		sendInt(clientfd, MOVE);
		sendInt(clientfd, RIGHT);
		printf("[S] Right\n");
		expectedPlayerPositionX++;
		return;
	}
	
	if (differenceHorizontal > 0) {
		sendInt(clientfd, MOVE);
		sendInt(clientfd, LEFT);
		printf("[S] Left\n");
		expectedPlayerPositionX--;
		return;
	}
	
	if (differenceVertical < 0) {
		sendInt(clientfd, MOVE);
		sendInt(clientfd, DOWN);
		printf("[S] Down\n");
		expectedPlayerPositionY++;
		return;
	}
	
	if (differenceVertical > 0) {
		sendInt(clientfd, MOVE);
		sendInt(clientfd, UP);
		printf("[S] Up\n");
		expectedPlayerPositionY--;
		return;
	}
}

void moveRandom() {
	sendInt(clientfd, MOVE);
	sendInt(clientfd, rand() % 4);
}

void handleKeyDown(SDL_KeyboardEvent* event)
{
    // ignore repeat events if key is held down
    if (event->repeat)
        return;

    if (event->keysym.scancode == SDL_SCANCODE_Q || event->keysym.scancode == SDL_SCANCODE_ESCAPE)
        shouldExit = true;

    if (event->keysym.scancode == SDL_SCANCODE_UP || event->keysym.scancode == SDL_SCANCODE_W)
        moveTo(playerPosition.x, playerPosition.y - 1, UP);

    if (event->keysym.scancode == SDL_SCANCODE_DOWN || event->keysym.scancode == SDL_SCANCODE_S)
        moveTo(playerPosition.x, playerPosition.y + 1, DOWN);

    if (event->keysym.scancode == SDL_SCANCODE_LEFT || event->keysym.scancode == SDL_SCANCODE_A)
        moveTo(playerPosition.x - 1, playerPosition.y, LEFT);

    if (event->keysym.scancode == SDL_SCANCODE_RIGHT || event->keysym.scancode == SDL_SCANCODE_D)
        moveTo(playerPosition.x + 1, playerPosition.y, RIGHT);
}

void processInputs()
{
	SDL_Event event;

	while (SDL_PollEvent(&event)) {
		switch (event.type) {
			case SDL_QUIT:
				shouldExit = true;
				break;

            case SDL_KEYDOWN:
                handleKeyDown(&event.key);
                printBoard();
				break;

			default:
				break;
		}
	}
    
}

void drawGrid(SDL_Renderer* renderer, SDL_Texture* grassTexture, SDL_Texture* tomatoTexture, SDL_Texture* playerTexture, SDL_Texture* otherPlayerTexture)
{
    SDL_Rect dest;
    for (int i = 0; i < GRIDSIZE; i++) {
        for (int j = 0; j < GRIDSIZE; j++) {
            dest.x = 64 * i;
            dest.y = 64 * j + HEADER_HEIGHT;
            SDL_Texture* texture = grid[i][j] == TILE_TOMATO ? tomatoTexture : grassTexture;
            SDL_QueryTexture(texture, NULL, NULL, &dest.w, &dest.h);
            SDL_RenderCopy(renderer, texture, NULL, &dest);
        }
    }

    dest.x = 64 * playerPosition.x;
    dest.y = 64 * playerPosition.y + HEADER_HEIGHT;
    SDL_QueryTexture(playerTexture, NULL, NULL, &dest.w, &dest.h);
    SDL_RenderCopy(renderer, playerTexture, NULL, &dest);
    playerInfo* previous = head; 
    while (head != NULL) {
        previous = head;
        SDL_Rect otherPlayer;
        otherPlayer.x = 64 * head->x;
        otherPlayer.y = 64 * head->y + HEADER_HEIGHT;
        printf("Rendering %d %d\n", otherPlayer.x, otherPlayer.y);
        SDL_QueryTexture(otherPlayerTexture, NULL, NULL, &otherPlayer.w, &otherPlayer.h);
        SDL_RenderCopy(renderer, otherPlayerTexture, NULL, &otherPlayer);
        head = head->next;
        free(previous);
    }
}

void drawUI(SDL_Renderer* renderer)
{
    // largest score/level supported is 2147483647
    char scoreStr[18];
    char levelStr[18];
    sprintf(scoreStr, "Score: %d", score);
    sprintf(levelStr, "Level: %d", level);

    SDL_Color white = {255, 255, 255};
    SDL_Surface* scoreSurface = TTF_RenderText_Solid(font, scoreStr, white);
    SDL_Texture* scoreTexture = SDL_CreateTextureFromSurface(renderer, scoreSurface);

    SDL_Surface* levelSurface = TTF_RenderText_Solid(font, levelStr, white);
    SDL_Texture* levelTexture = SDL_CreateTextureFromSurface(renderer, levelSurface);

    SDL_Rect scoreDest;
    TTF_SizeText(font, scoreStr, &scoreDest.w, &scoreDest.h);
    scoreDest.x = 0;
    scoreDest.y = 0;

    SDL_Rect levelDest;
    TTF_SizeText(font, levelStr, &levelDest.w, &levelDest.h);
    levelDest.x = GRID_DRAW_WIDTH - levelDest.w;
    levelDest.y = 0;

    SDL_RenderCopy(renderer, scoreTexture, NULL, &scoreDest);
    SDL_RenderCopy(renderer, levelTexture, NULL, &levelDest);

    SDL_FreeSurface(scoreSurface);
    SDL_DestroyTexture(scoreTexture);

    SDL_FreeSurface(levelSurface);
    SDL_DestroyTexture(levelTexture);
}

void printBoard() {
	for (int row = 0; row < GRIDSIZE; row++) {
		for (int col = 0; col < GRIDSIZE; col++) {
			printf("%d ", grid[col][row]);
		}
		printf("\n");
	}
	printf("\n");
}

int main(int argc, char* argv[])
{
    if(argc < 3) {
        fprintf(stderr, "usage: %s <host> <port>\n", argv[0]);
        exit(0);
    }
    
    srand(time(NULL));
    if (argc > 3) {
        AI = atoi(argv[3]);
    }

    

    level = 1;

    initSDL();

    font = TTF_OpenFont("resources/Burbank-Big-Condensed-Bold-Font.otf", HEADER_HEIGHT);
    if (font == NULL) {
        fprintf(stderr, "Error loading font: %s\n", TTF_GetError());
        exit(EXIT_FAILURE);
    }

    playerPosition.x = playerPosition.y = GRIDSIZE / 2;
	
    SDL_Window* window = SDL_CreateWindow("Client", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, WINDOW_WIDTH, WINDOW_HEIGHT, 0);

    if (window == NULL) {
        fprintf(stderr, "Error creating app window: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, 0);

	if (renderer == NULL)
	{
		fprintf(stderr, "Error creating renderer: %s\n", SDL_GetError());
        exit(EXIT_FAILURE);
	}

    SDL_Texture *grassTexture = IMG_LoadTexture(renderer, "resources/grass.png");
    SDL_Texture *tomatoTexture = IMG_LoadTexture(renderer, "resources/tomato.png");
    SDL_Texture *otherPlayerTexture = IMG_LoadTexture(renderer, "resources/player.png");
    SDL_Texture *playerTexture = IMG_LoadTexture(renderer, "resources/player1.png");

    // server connection
    char* host, *port;

    host = argv[1];
    port = argv[2];

    clientfd = open_clientfd(host, port);
	if (clientfd < 0) {
		fprintf(stderr, "Error connecting to the server: %d\n", clientfd);
		exit(0);
	}
	
    // main game loop
	
    while (!shouldExit) {
        SDL_SetRenderDrawColor(renderer, 0, 105, 6, 255);
        SDL_RenderClear(renderer);

        processInputs();

		sendInt(clientfd, UPDATE);
		processServerResponse();
		if (desync == 0 && AI == 1) {
			moveTowardsClosestPlayer();
		} else if (desync == 0 && AI == 2) {
			moveRandom();
		} else if (desync == 0 && AI == 3) {
		    int differenceHorizontal = playerPosition.x - closestTomato.x;
			int differenceVertical = playerPosition.y - closestTomato.y;
			moveTowardsClosestObject(differenceHorizontal, differenceVertical);
	    }

        drawGrid(renderer, grassTexture, tomatoTexture, playerTexture, otherPlayerTexture);
        drawUI(renderer);
        SDL_RenderPresent(renderer);
        SDL_Delay(32); // 16 ms delay to limit display to 60 fps
    }

    // clean up everything
    SDL_DestroyTexture(grassTexture);
    SDL_DestroyTexture(tomatoTexture);
    SDL_DestroyTexture(otherPlayerTexture);
    SDL_DestroyTexture(playerTexture);

    TTF_CloseFont(font);
    TTF_Quit();

    IMG_Quit();

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    close(clientfd);
}
