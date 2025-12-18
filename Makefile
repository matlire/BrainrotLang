CC      = gcc
CFLAGS  = -std=c23 -Wall -Wextra -Wpedantic -g \
          -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS = -fsanitize=address,undefined -lm

OBJ_DIR  = build
DIST_DIR = dist

INCLUDES = -I. -Ilexer -Itree -Itree/dump \
           -Ilibs/hash -Ilibs/instruction_set -Ilibs/io -Ilibs/logging -Ilibs/stack -Iast -Ibackend -Imiddleend \
		   -Ireverse-frontend -Iast/dump -Iast/diff-tree

SRC_COMMON = 							   \
    lexer/lexer.c 						   \
    libs/hash/hash.c 					   \
    libs/instruction_set/instruction_set.c \
    libs/io/io.c 						   \
    libs/logging/logging.c 				   \
    libs/stack/stack.c 					   \
	ast/ast.c 							   \
	ast/syntax_analyzer.c				   \
	backend/backend.c					   \
	middleend/middleend.c				   \
	reverse-frontend/reverse-frontend.c	   \
	ast/dump/dump.c						   \
	ast/diff-tree/diff-tree.c			   \
	ast/diff-tree/differentiation.c		   \
	ast/diff-tree/optimizations.c

COMMON_OBJS = 					 \
    $(OBJ_DIR)/lexer.o 			 \
    $(OBJ_DIR)/hash.o 			 \
    $(OBJ_DIR)/instruction_set.o \
    $(OBJ_DIR)/io.o 			 \
    $(OBJ_DIR)/logging.o 		 \
    $(OBJ_DIR)/stack.o 			 \
	$(OBJ_DIR)/ast.o 			 \
	$(OBJ_DIR)/syntax_analyzer.o \
	$(OBJ_DIR)/backend.o		 \
	$(OBJ_DIR)/middleend.o		 \
	$(OBJ_DIR)/reverse-frontend.o\
	$(OBJ_DIR)/dump.o			 \
	$(OBJ_DIR)/diff-tree.o		 \
	$(OBJ_DIR)/differentiation.o \
	$(OBJ_DIR)/optimizations.o

FRONTEND_OBJ  = $(OBJ_DIR)/frontend-main.o
BACKEND_OBJ   = $(OBJ_DIR)/backend-main.o
MIDDLEEND_OBJ = $(OBJ_DIR)/middleend-main.o

REVERSE_FRONTEND_OBJ = $(OBJ_DIR)/reverse-frontend-main.o

FRONTEND_OBJS  = $(COMMON_OBJS) $(FRONTEND_OBJ)
BACKEND_OBJS   = $(COMMON_OBJS) $(BACKEND_OBJ)
MIDDLEEND_OBJS = $(COMMON_OBJS) $(MIDDLEEND_OBJ)

REVERSE_FRONTEND_OBJS = $(COMMON_OBJS) $(REVERSE_FRONTEND_OBJ)

all: $(DIST_DIR)/frontend $(DIST_DIR)/backend $(DIST_DIR)/middleend $(DIST_DIR)/reverse-frontend 

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(DIST_DIR):
	mkdir -p $(DIST_DIR)

$(DIST_DIR)/frontend: $(FRONTEND_OBJS) | $(DIST_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(DIST_DIR)/backend: $(BACKEND_OBJS) | $(DIST_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(DIST_DIR)/middleend: $(MIDDLEEND_OBJS) | $(DIST_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(DIST_DIR)/reverse-frontend: $(REVERSE_FRONTEND_OBJS) | $(DIST_DIR)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/lexer.o: lexer/lexer.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/hash.o: libs/hash/hash.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/instruction_set.o: libs/instruction_set/instruction_set.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/io.o: libs/io/io.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/logging.o: libs/logging/logging.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/stack.o: libs/stack/stack.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/ast.o: ast/ast.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/dump.o: ast/dump/dump.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/diff-tree.o: ast/diff-tree/diff-tree.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/differentiation.o: ast/diff-tree/differentiation.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/optimizations.o: ast/diff-tree/optimizations.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/syntax_analyzer.o: ast/syntax_analyzer.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/backend.o: backend/backend.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/middleend.o: middleend/middleend.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/reverse-frontend.o: reverse-frontend/reverse-frontend.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/frontend-main.o: frontend-main.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/backend-main.o: backend-main.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/middleend-main.o: middleend-main.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(OBJ_DIR)/reverse-frontend-main.o: reverse-frontend-main.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(DIST_DIR)

.PHONY: all clean
