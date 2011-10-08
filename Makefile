CC = gcc
CFLAGS =  -Wall
DEBUG = -g
aniSH: aniSH.c clean
	$(CC) $(CFLAGS) $(DEBUG) -o aniSH aniSH.c
clean:
	rm -rf aniSH
