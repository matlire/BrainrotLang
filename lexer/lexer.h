#ifndef LEXER_H
#define LEXER_H

#include <stddef.h>
#include <stdint.h>

#include "../libs/types.h"
#include "../libs/hash/hash.h"
#include "../libs/instruction_set/instruction_set.h"
#include "token_list.h"
#include "../libs/io/io.h"

typedef enum
{
#define TOKEN_ENUM(sym, text) sym,
    TOKEN_LIST(TOKEN_ENUM)
#undef TOKEN_ENUM

    TOK_COUNT
} token_kind_t;

typedef enum
{
    LIT_NONE = 0,
    LIT_INT,
    LIT_FLOAT,
} literal_type_t;

typedef struct
{
    size_t line;
    size_t column;
    size_t offset;
} token_pos_t;

typedef struct
{
    token_kind_t kind;
    token_pos_t  pos;

    const char*  buffer;
    size_t       length;

    literal_type_t lit_type;
    cell64_t       lit;

    size_t         name_id;
} token_t;

typedef struct
{
    char*  name;
    size_t length;
    size_t hash;
} nametable_entry_t;

typedef struct
{
    nametable_entry_t* data;
    size_t             amount;
    size_t             capacity;
} nametable_t;

typedef struct
{
    operational_data_t* op_data;

    size_t pos;
    size_t line;
    size_t column;

    nametable_t* nametable;

    token_t current;
    int     has_current;
} lexer_t;

err_t nametable_ctor(nametable_t* nametable);
err_t nametable_dtor(nametable_t* nametable);

size_t nametable_insert(nametable_t* nametable, const char* start, const size_t length);

err_t lexer_ctor (lexer_t* lexer, operational_data_t* op_data, nametable_t* nametable);
err_t lexer_dtor (lexer_t* lexer);
err_t lexer_reset(lexer_t* lexer);
err_t lexer_next (lexer_t* lexer, token_t* out);
err_t lexer_peek (lexer_t* lexer, token_t* out);

int lexer_is_eof (const token_t* tok);

const char* token_kind_to_cstr(token_kind_t kind);

err_t lexer_stream(operational_data_t* op_data,
                   token_t**           out_tokens,
                   size_t*             out_count,
                   nametable_t*        out_nametable);

#endif
