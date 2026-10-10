CC      = cc
CFLAGS  = -std=c17 -Wall -Wextra -Wpedantic -Wshadow -g -O0 -D_POSIX_C_SOURCE=200809L
SAN     = -fsanitize=address,undefined -fno-omit-frame-pointer

SRC     = $(wildcard src/*.c)
HDR		= $(wildcard src/*.h)
BIN     = build/server

TEST_SRC = tests/test_parse.c $(filter-out src/server.c, $(SRC))
TEST_BIN = build/test_parse


$(BIN): $(SRC) $(HDR)
	mkdir -p build
	$(CC) $(CFLAGS) $(SAN) $(SRC) -o $@

$(TEST_BIN): $(TEST_SRC) $(HDR)
	mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -I src $(TEST_SRC) -o $@

test: $(TEST_BIN)
	./$(TEST_BIN)

clean:
	rm -rf build

.PHONY: test clean
