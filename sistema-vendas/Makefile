# Requer o PostgreSQL client (libpq) instalado:
#   Ubuntu/Debian: sudo apt install libpq-dev
#   Fedora:        sudo dnf install libpq-devel
#   macOS:         brew install libpq
CC       ?= gcc
CFLAGS   ?= -std=c99 -Wall -Wextra -pedantic -O2
PG_INC   := $(shell pg_config --includedir 2>/dev/null)
PG_LIB   := $(shell pg_config --libdir 2>/dev/null)
CPPFLAGS += $(if $(PG_INC),-I$(PG_INC))
LDFLAGS  += $(if $(PG_LIB),-L$(PG_LIB))
LDLIBS   += -lpq

SRC      = src/dados.c src/relatorios.c
HEADERS  = src/dados.h src/relatorios.h

all: sistema

sistema: $(SRC) src/main.c $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRC) src/main.c $(LDFLAGS) $(LDLIBS)

testes: $(SRC) tests/testes.c $(HEADERS)
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ $(SRC) tests/testes.c $(LDFLAGS) $(LDLIBS)

# Usa o banco vendas_teste (os testes APAGAM os dados desse banco)
test: testes
	./testes

clean:
	rm -f sistema testes sistema.exe testes.exe

.PHONY: all test clean
