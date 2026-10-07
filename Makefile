CC      = cc
CFLAGS  = -std=c17 -Wall -Wextra -Wpedantic -Wshadow -g -O0 -D_POSIX_C_SOURCE=200809L
SAN     = -fsanitize=address,undefined -fno-omit-frame-pointer
SRC     = $(wildcard src/*.c)
BIN     = server

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SAN) $^ -o $@

clean:
	rm -f $(BIN)

.PHONY: clean
