CC      ?= gcc
CFLAGS  ?= -std=c99 -Wall -Wextra -pedantic -O2
SRC      = src/dados.c src/relatorios.c
HEADERS  = src/dados.h src/relatorios.h

all: sistema

sistema: $(SRC) src/main.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SRC) src/main.c

testes: $(SRC) tests/testes.c $(HEADERS)
	$(CC) $(CFLAGS) -o $@ $(SRC) tests/testes.c

test: testes
	./testes

clean:
	rm -f sistema testes sistema.exe testes.exe

.PHONY: all test clean
