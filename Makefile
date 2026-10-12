CC      = cc
CFLAGS  = -std=c17 -Wall -Wextra -Wpedantic -Wshadow -g -O0 -D_POSIX_C_SOURCE=200809L
SAN     = -fsanitize=address,undefined -fno-omit-frame-pointer

SRC     = $(wildcard src/*.c)
HDR		= $(wildcard src/*.h)
BIN     = build/server

LIB_SRC = $(filter-out src/server.c, $(SRC))

TEST_PARSE_SRC = tests/test_parse.c
TEST_PARSE_BIN = build/test_parse

TEST_HANDLER_SRC = tests/test_handler.c
TEST_HANDLER_BIN = build/test_handler

$(BIN): $(SRC) $(HDR)
	mkdir -p build
	$(CC) $(CFLAGS) $(SAN) $(SRC) -o $@

$(TEST_PARSE_BIN): $(TEST_PARSE_SRC) $(LIB_SRC) $(HDR)
	mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -I src $(TEST_PARSE_SRC) $(LIB_SRC) -o $@

$(TEST_HANDLER_BIN): $(TEST_HANDLER_SRC) $(LIB_SRC) $(HDR)
	mkdir -p build
	$(CC) $(CFLAGS) $(SAN) -I src $(TEST_HANDLER_SRC) $(LIB_SRC) -o $@

test: $(TEST_PARSE_BIN) $(TEST_HANDLER_BIN)
	./$(TEST_PARSE_BIN)
	./$(TEST_HANDLER_BIN)

clean:
	rm -rf build

.PHONY: test clean
