# the compiler
CC = gcc-12

# the compiler flags
CFLAGS += -std=c17
CFLAGS += -no-pie
CFLAGS += -g3
CFLAGS += -ggdb
CFLAGS += -Wall
CFLAGS += -Wextra
CFLAGS += -Warray-bounds
CFLAGS += -Wconversion
CFLAGS += -Wmissing-braces
CFLAGS += -Wno-parentheses
CFLAGS += -Wno-format-truncation
CFLAGS += -Wpedantic
CFLAGS += -Wstrict-prototypes
CFLAGS += -Wwrite-strings
CFLAGS += -Winline
CFLAGS += -s

#CFLAGS += -fanalyzer
#CFLAGS += -fno-builtin
#CFLAGS += -fno-common
#CFLAGS += -fno-omit-frame-pointer
#CFLAGS += -fsanitize=address
#CFLAGS += -fsanitize=undefined
#CFLAGS += -fsanitize=bounds-strict
#CFLAGS += -fsanitize=leak
#CFLAGS += -fsanitize=null
#CFLAGS += -fsanitize=signed-integer-overflow
#CFLAGS += -fsanitize=bool
#CFLAGS += -fsanitize=pointer-overflow
#CFLAGS += -fsanitize-address-use-after-scope
#CFLAGS += -O3

SRC = src
OBJ = obj
BINDIR = bin
BIN = $(BINDIR)/Verse
SRCS = $(wildcard $(SRC)/*.c)
OBJS = $(patsubst $(SRC)/%.c, $(OBJ)/%.o, $(SRCS))

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) -D_FORTIFY_SOURCE=2 -o $@ $^ $(CFLAGS) 

$(OBJ)/%.o: $(SRC)/%.c
	$(CC) $(CFLAGS) -c $< -o $@ 

clean:
	$(RM) -rf $(OBJ)/* $(BINDIR)/*

.PHONY: clean
.DELETE_ON_ERROR:
