OUTPUT = client
CFLAGS = -g -Wall -Wvla -I inc -D_REENTRANT
LFLAGS = -L lib -lSDL2 -lSDL2_image -lSDL2_ttf
PATH = INSERT PATH
PORT = 16000

%.o: %.c %.h
	gcc $(CFLAGS) -c -o $@ $<

%.o: %.c
	gcc $(CFLAGS) -c -o $@ $<

all: $(OUTPUT)

runclient: $(OUTPUT)
	LD_LIBRARY_PATH=$(PATH) ./client localhost $(PORT)

runclientC: $(OUTPUT)
	LD_LIBRARY_PATH=$(PATH) ./client localhost $(PORT) 1
	
runclientR: $(OUTPUT)
	LD_LIBRARY_PATH=$(PATH) ./client localhost $(PORT) 2

runclientS: $(OUTPUT)
	LD_LIBRARY_PATH=$(PATH) ./client localhost $(PORT) 3

client: client.o
	gcc $(CFLAGS) -o $@ $^ $(LFLAGS)

server:
	gcc $(CFLAGS) -c -o server.o server.c -lpthread;
	gcc $(CFLAGS) -o server server.o -lpthread

clean:
	rm -f $(OUTPUT) *.o;
	rm -f server
