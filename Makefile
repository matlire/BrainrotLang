CC          ?= gcc
ASM_TARGET  ?= nasm
SYNTAX      ?= sane
BUILD       ?= debug
DUMP_IR       ?= 0
NASM_GRAPHICS ?= 0

STTY_SIZE := $(shell stty size 2>/dev/null)

STTY_ROWS := $(word 1,$(STTY_SIZE))
STTY_COLS := $(word 2,$(STTY_SIZE))

ifeq ($(NASM_GRAPHICS),1)
    ifeq ($(STTY_COLS),)
        NASM_SCREEN_WIDTH ?= 128
    else
        NASM_SCREEN_WIDTH ?= $(STTY_COLS)
    endif

    ifeq ($(STTY_ROWS),)
        NASM_SCREEN_HEIGHT ?= 32
    else
        NASM_SCREEN_HEIGHT ?= $(shell echo $$(( $(STTY_ROWS) - 1 )))
    endif
else
    NASM_SCREEN_WIDTH  ?= 128
    NASM_SCREEN_HEIGHT ?= 32
endif

OBJ_DIR  := build/$(ASM_TARGET)-$(SYNTAX)-$(BUILD)-gfx$(NASM_GRAPHICS)
DIST_DIR := dist/$(ASM_TARGET)-$(SYNTAX)-$(BUILD)-gfx$(NASM_GRAPHICS)

WARN_FLAGS := -Wall -Wextra -Wpedantic

STD_FLAGS := -std=c23

DEBUG_FLAGS   := -g -O0
RELEASE_FLAGS := -O2 -DNDEBUG

SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer

INCLUDES := 			   \
    -I. 			  	   \
    -Ilexer 			   \
    -Itree 				   \
    -Itree/dump 		   \
    -Ilibs/hash 		   \
    -Ilibs/instruction_set \
    -Ilibs/io 			   \
    -Ilibs/logging 		   \
    -Ilibs/stack 		   \
    -Iast 				   \
    -Iast/dump 			   \
    -Iast/diff-tree 	   \
    -Imiddleend 		   \
    -Ireverse-frontend

CPPFLAGS += $(INCLUDES) -MMD -MP

CFLAGS += $(STD_FLAGS) $(WARN_FLAGS)

LDFLAGS += -lm

ifeq ($(BUILD),debug)
    CFLAGS  += $(DEBUG_FLAGS) $(SAN_FLAGS)
    LDFLAGS += $(SAN_FLAGS)
else ifeq ($(BUILD),release)
    CFLAGS += $(RELEASE_FLAGS)
else
    $(error BUILD must be debug or release)
endif

ifeq ($(DUMP_IR),1)
    CPPFLAGS += -DBRL_DUMP_IR=1
else ifeq ($(DUMP_IR),0)
else
    $(error DUMP_IR must be 0 or 1)
endif

ifeq ($(NASM_GRAPHICS),1)
    CPPFLAGS += -D__NASM_SIM_GRAPHICS=1
    CPPFLAGS += -DBE_SCREEN_WIDTH=$(NASM_SCREEN_WIDTH)
    CPPFLAGS += -DBE_SCREEN_HEIGHT=$(NASM_SCREEN_HEIGHT)
else ifeq ($(NASM_GRAPHICS),0)
else
    $(error NASM_GRAPHICS must be 0 or 1)
endif

BACKEND_COMMON_SRC := backend/backend_common.c backend/backend_dispatch.c backend/ir/backend_ir.c

ifeq ($(ASM_TARGET),tasm)
  CPPFLAGS += -D__BACKEND_TASM=1
  BACKEND_SRC := $(BACKEND_COMMON_SRC) backend/backend_tasm.c
else ifeq ($(ASM_TARGET),nasm)
  CPPFLAGS += -D__BACKEND_NASM=1
  BACKEND_SRC := $(BACKEND_COMMON_SRC) backend/backend_nasm.c
else
  $(error ASM_TARGET must be tasm or nasm)
endif

ifeq ($(SYNTAX),am_tiktok)
    CPPFLAGS += -D__SYNTAX_AM_TIKTOK=1
else ifeq ($(SYNTAX),sane)
    CPPFLAGS += -D__SYNTAX_SANE=1
else
    $(error SYNTAX must be am_tiktok or sane)
endif

LIB_SRC := 									\
    lexer/lexer.c 							\
    libs/hash/hash.c 						\
    libs/instruction_set/instruction_set.c  \
    libs/io/io.c 							\
    libs/logging/logging.c 					\
    libs/stack/stack.c 						\
    ast/ast.c 								\
    ast/syntax_analyzer.c 					\
    ast/dump/dump.c 						\
    ast/diff-tree/diff-tree.c 				\
    ast/diff-tree/differentiation.c 		\
    ast/diff-tree/optimizations.c

MIDDLEEND_SRC := 						\
    middleend/middleend.c

REVERSE_FRONTEND_SRC := 				\
    reverse-frontend/reverse-frontend.c

FRONTEND_MAIN_SRC 		  := frontend-main.c
BACKEND_MAIN_SRC 		  := backend-main.c
MIDDLEEND_MAIN_SRC		  := middleend-main.c
REVERSE_FRONTEND_MAIN_SRC := reverse-frontend-main.c

FRONTEND_SRC := 		 \
    $(LIB_SRC) 			 \
    $(FRONTEND_MAIN_SRC)

BACKEND_EXE_SRC := 		 \
    $(LIB_SRC) 			 \
    $(BACKEND_SRC) 		 \
    $(BACKEND_MAIN_SRC)

MIDDLEEND_EXE_SRC := 	 \
    $(LIB_SRC) 			 \
    $(MIDDLEEND_SRC)	 \
    $(MIDDLEEND_MAIN_SRC)

REVERSE_FRONTEND_EXE_SRC := 	\
    $(LIB_SRC) 					\
    $(REVERSE_FRONTEND_SRC) 	\
    $(REVERSE_FRONTEND_MAIN_SRC)

define src_to_obj
$(patsubst %.c,$(OBJ_DIR)/%.o,$(1))
endef

FRONTEND_OBJS         := $(call src_to_obj,$(FRONTEND_SRC))
BACKEND_OBJS          := $(call src_to_obj,$(BACKEND_EXE_SRC))
MIDDLEEND_OBJS        := $(call src_to_obj,$(MIDDLEEND_EXE_SRC))
REVERSE_FRONTEND_OBJS := $(call src_to_obj,$(REVERSE_FRONTEND_EXE_SRC))

ALL_OBJS := 						\
    $(sort 							\
        $(FRONTEND_OBJS) 			\
        $(BACKEND_OBJS) 			\
        $(MIDDLEEND_OBJS) 			\
        $(REVERSE_FRONTEND_OBJS))

ALL_DEPS := $(ALL_OBJS:.o=.d)

FRONTEND_BIN         := $(DIST_DIR)/frontend
BACKEND_BIN          := $(DIST_DIR)/backend
MIDDLEEND_BIN        := $(DIST_DIR)/middleend
REVERSE_FRONTEND_BIN := $(DIST_DIR)/reverse-frontend

ALL_BINS := \
    $(FRONTEND_BIN) \
    $(BACKEND_BIN) \
    $(MIDDLEEND_BIN) \
    $(REVERSE_FRONTEND_BIN)

all: $(ALL_BINS)

frontend: 		  $(FRONTEND_BIN)
backend: 		  $(BACKEND_BIN)
middleend: 		  $(MIDDLEEND_BIN)
reverse-frontend: $(REVERSE_FRONTEND_BIN)

$(FRONTEND_BIN): $(FRONTEND_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BACKEND_BIN): $(BACKEND_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(MIDDLEEND_BIN): $(MIDDLEEND_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(REVERSE_FRONTEND_BIN): $(REVERSE_FRONTEND_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

print-config:
	@echo "CC         = $(CC)"
	@echo "ASM_TARGET = $(ASM_TARGET)"
	@echo "SYNTAX     = $(SYNTAX)"
	@echo "BUILD      = $(BUILD)"
	@echo "DUMP_IR    = $(DUMP_IR)"
	@echo "OBJ_DIR    = $(OBJ_DIR)"
	@echo "DIST_DIR   = $(DIST_DIR)"
	@echo "CPPFLAGS   = $(CPPFLAGS)"
	@echo "CFLAGS     = $(CFLAGS)"
	@echo "LDFLAGS    = $(LDFLAGS)"
	@echo "NASM_GRAPHICS      = $(NASM_GRAPHICS)"
	@echo "NASM_SCREEN_WIDTH  = $(NASM_SCREEN_WIDTH)"
	@echo "NASM_SCREEN_HEIGHT = $(NASM_SCREEN_HEIGHT)"
	@echo "STTY_SIZE          = $(STTY_SIZE)"
clean:
	rm -rf $(OBJ_DIR) $(DIST_DIR)

clean-all:
	rm -rf build dist

rebuild: clean all

help:
	@echo "BrainrotLang build"
	@echo ""
	@echo "Main targets:"
	@echo "  make                         Build all tools"
	@echo "  make all                     Build all tools"
	@echo "  make frontend                Build frontend only"
	@echo "  make backend                 Build backend only"
	@echo "  make middleend               Build middleend only"
	@echo "  make reverse-frontend        Build reverse frontend only"
	@echo "  make rebuild                 Clean current variant and rebuild"
	@echo ""
	@echo "Configuration:"
	@echo "  ASM_TARGET=tasm              Use toy ASM backend, currently supported"
	@echo "  ASM_TARGET=nasm              Reserved for point II"
	@echo "  SYNTAX=sane                  Use sane C-like keywords"
	@echo "  SYNTAX=am_tiktok             Use old meme keywords"
	@echo "  BUILD=debug                  Debug build with sanitizers"
	@echo "  make backend ASM_TARGET=nasm SYNTAX=sane DUMP_IR=1"
	@echo "  BUILD=release                Optimized release build"
	@echo "  NASM_GRAPHICS=1              Enable simulated NASM graphics"
	@echo "  NASM_SCREEN_WIDTH=120        Override auto stty screen width"
	@echo "  NASM_SCREEN_HEIGHT=40        Override auto stty screen height"
	@echo ""
	@echo "Examples:"
	@echo "  make SYNTAX=sane"
	@echo "  make SYNTAX=am_tiktok"
	@echo "  make BUILD=release"
	@echo "  make backend SYNTAX=sane BUILD=debug"
	@echo "  make clean"
	@echo "  make clean-all"
	@echo ""
	@echo "Utility targets:"
	@echo "  make print-config            Show selected config and flags"
	@echo "  make clean                   Remove current variant build/dist"
	@echo "  make clean-all               Remove all build/dist directories"

.PHONY: 			 \
    all 			 \
    frontend 		 \
    backend 		 \
    middleend 		 \
    reverse-frontend \
    print-config 	 \
	help			 \
    clean 			 \
    clean-all 		 \
    rebuild

-include $(ALL_DEPS)
