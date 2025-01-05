# Makefile for notes and user_manager
CC=gcc
CFLAGS=-Wall $(shell mariadb_config --cflags)
LIBS=$(shell mariadb_config --libs) -lncurses

all: notes user_manager

notes: notes.c
	$(CC) $(CFLAGS) notes.c -o notes $(LIBS)

user_manager: user_manager.c
	$(CC) $(CFLAGS) user_manager.c -o user_manager $(LIBS)

clean:
	rm -f notes user_manager

