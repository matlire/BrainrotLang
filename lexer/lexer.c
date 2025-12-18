#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdio.h>

static int is_ident_start(int c)
{
    return isalpha(c) || c == '_';
}

static int is_ident_char(int c)
{
    return isalnum(c) || c == '_';
}

err_t nametable_ctor(nametable_t* nametable)
{
    if (!nametable)
        return ERR_BAD_ARG;

    nametable->data     = NULL;
    nametable->amount   = 0;
    nametable->capacity = 0;

    return OK;
}

err_t nametable_dtor(nametable_t* nametable)
{
    if (!nametable)
        return ERR_BAD_ARG;

    if (nametable->data)
    {
        for (size_t i = 0; i < nametable->amount; ++i)
        {
            free(nametable->data[i].name);
            nametable->data[i].name = NULL;
        }

        free(nametable->data);
        nametable->data = NULL;
    }

    nametable->amount   = 0;
    nametable->capacity = 0;

    return OK;
}

static err_t nametable_ensure_capacity(nametable_t* nametable, size_t min_capacity)
{
    if (!nametable)
        return ERR_BAD_ARG;

    if (nametable->capacity >= min_capacity)
        return OK;

    size_t new_cap = (nametable->capacity > 0 ? nametable->capacity * 2 : 8);
    if (new_cap < min_capacity)
        new_cap = min_capacity;

    nametable_entry_t* new_data = (nametable_entry_t*)realloc(nametable->data, new_cap * sizeof(nametable_entry_t));
    if (!new_data)
        return ERR_ALLOC;

    if (new_cap > nametable->capacity)
    {
        memset(new_data + nametable->capacity, 0,
               (new_cap - nametable->capacity) * sizeof(nametable_entry_t));
    }

    nametable->data     = new_data;
    nametable->capacity = new_cap;

    return OK;
}

size_t nametable_insert(nametable_t* nametable, const char* buffer, size_t length)
{
    if (!nametable || !buffer)
        return ERR_BAD_ARG;

    size_t h = sdbm_n(buffer, length);

    for (size_t i = 0; i < nametable->amount; ++i)
    {
        if (nametable->data[i].hash == h &&
            nametable->data[i].length == length)
        {
            return i;
        }
    }

    if (nametable_ensure_capacity(nametable, nametable->amount + 1) != OK)
        return SIZE_MAX;

    nametable_entry_t* entry = &nametable->data[nametable->amount];

    entry->name = (char*)calloc(length + 1, 1);
    if (!entry->name)
        return SIZE_MAX;

    memcpy(entry->name, buffer, length);
    entry->name[length] = '\0';
    entry->length       = length;
    entry->hash         = h;

    return nametable->amount++;
}

typedef struct
{
    const char*  text;
    size_t       hash;
    token_kind_t kind;
} keyword_entry_t;

static keyword_entry_t KEYWORDS[] = {
#define KEYWORD_ENTRY(sym, text) { text, 0u, sym },
    KEYWORD_LIST(KEYWORD_ENTRY)
#undef KEYWORD_ENTRY
};

static int kw_initialized = 0;

static void init_keywords(void)
{
    if (kw_initialized)
        return;

    kw_initialized = 1;

    size_t n = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);
    for (size_t i = 0; i < n; ++i)
    {
        keyword_entry_t* e = &KEYWORDS[i];
        e->hash = sdbm_n(e->text, strlen(e->text));
    }
}

static token_kind_t lookup_keyword(const char* buffer, size_t len)
{
    init_keywords();

    size_t h = sdbm_n(buffer, len);
    size_t n = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

    for (size_t i = 0; i < n; ++i)
    {
        const keyword_entry_t* e = &KEYWORDS[i];

        if (e->hash == h)
            return e->kind;
    }

    return TOK_IDENTIFIER;
}

static char lexer_peek_char(const lexer_t* lexer)
{
    if (!lexer || !lexer->op_data || !lexer->op_data->buffer)
        return '\0';

    if (lexer->pos >= lexer->op_data->buffer_size)
        return '\0';

    return lexer->op_data->buffer[lexer->pos];
}

static char lexer_peek_next_char(const lexer_t* lexer)
{
    if (!lexer || !lexer->op_data || !lexer->op_data->buffer)
        return '\0';

    if (lexer->pos + 1 >= lexer->op_data->buffer_size)
        return '\0';

    return lexer->op_data->buffer[lexer->pos + 1];
}

static err_t lexer_advance(lexer_t* lexer)
{
    if (!lexer || !lexer->op_data || !lexer->op_data->buffer)
        return ERR_BAD_ARG;

    if (lexer->pos >= lexer->op_data->buffer_size)
        return ERR_CORRUPT;

    char c = lexer->op_data->buffer[lexer->pos++];

    if (c == '\n')
    {
        lexer->line++;
        lexer->column = 1;
    }
    else
        lexer->column++;

    return OK;
}

static void lexer_skip(lexer_t* lexer)
{
    for (;;)
    {
        char c = lexer_peek_char(lexer);

        if (isspace((unsigned char)c))
        {
            lexer_advance(lexer);
            continue;
        }

        if (c == '/' && lexer_peek_next_char(lexer) == '/')
        {
            lexer_advance(lexer);
            lexer_advance(lexer);

            while (1)
            {
                char d = lexer_peek_char(lexer);
                if (d == '\0' || d == '\n')
                    break;
                lexer_advance(lexer);
            }

            if (lexer_peek_char(lexer) == '\n')
                lexer_advance(lexer);

            continue;
        }

        break;
    }
}

static void token_ctor(token_t*  tok,        token_kind_t kind,
                       lexer_t*  lexer,      size_t start_pos,
                       size_t    start_line, size_t start_col)
{
    tok->kind       = kind;
    tok->pos.offset = start_pos;
    tok->pos.line   = start_line;
    tok->pos.column = start_col;

    const char* src = (lexer && lexer->op_data) ? lexer->op_data->buffer : NULL;
    tok->buffer     = src ? src + start_pos : NULL;
    tok->length     = lexer->pos - start_pos;

    tok->lit_type   = LIT_NONE;
    tok->lit.i64    = 0;
    tok->name_id    = SIZE_MAX;
}

static void scan_identifier(lexer_t* lexer,   token_t* tok,
                            size_t start_pos, size_t start_line, size_t start_col)
{
    while (is_ident_char((unsigned char)lexer_peek_char(lexer)))
        lexer_advance(lexer);

    token_ctor(tok, TOK_IDENTIFIER, lexer, start_pos, start_line, start_col);

    token_kind_t kw = lookup_keyword(tok->buffer, tok->length);
    tok->kind = kw;

    if (kw == TOK_IDENTIFIER && lexer->nametable)
    {
        size_t id = nametable_insert(lexer->nametable, tok->buffer, tok->length);
        tok->name_id = id;
    }
}

#define LEXER_SET_ERROR(lexer_ptr, pos_, fmt, ...)           \
    block_begin                                              \
        lexer_t* _lx = (lexer_ptr);                          \
        if (_lx && _lx->op_data)                             \
        {                                                    \
            operational_data_t* _op = _lx->op_data;          \
            _op->error_pos = (pos_);                         \
            snprintf(_op->error_msg, sizeof(_op->error_msg), \
                     fmt, ##__VA_ARGS__);                    \
            log_printf(ERROR, "%s", _op->error_msg);         \
        }                                                    \
    block_end

static void scan_number(lexer_t* lexer,   token_t* tok,
                        size_t start_pos, size_t start_line, size_t start_col)
{
    while (1)
    {
        char c = lexer_peek_char(lexer);
        if (isdigit((unsigned char)c) ||
            isalpha((unsigned char)c) ||
            c == '_' || c == '.')
        {
            lexer_advance(lexer);
        }
        else
            break;
    }

    token_ctor(tok, TOK_NUMERIC_LITERAL, lexer, start_pos, start_line, start_col);

    char* buf = NULL;
    if (tok->buffer)
    {
        buf = strndup(tok->buffer, tok->length);
        buf[tok->length] = '\0';
    }

    int  dots = 0;
    int  ok   = 1;

    if (tok->length == 0 || !isdigit((unsigned char)buf[0]))
        ok = 0;
    else
    {
        for (size_t i = 1; i < tok->length; ++i)
        {
            char ch = buf[i];
            if (isdigit((unsigned char)ch))
                continue;

            if (ch == '.')
            {
                dots++;
                if (dots > 1)
                {
                    ok = 0;
                    break;
                }
                continue;
            }

            ok = 0;
            break;
        }

        if (ok && dots == 1)
        {
            char* dot_pos = strchr(buf, '.');
            if (!dot_pos || !isdigit((unsigned char)dot_pos[1]))
                ok = 0;
        }
    }

    if (!ok)
    {
        tok->kind = TOK_ERROR;

        LEXER_SET_ERROR(lexer, start_pos,
                        "Invalid numeric literal at line %zu, column %zu: \"%s\"",
                        start_line, start_col, buf);

        free(buf);
        return;
    }

    if (dots == 0)
    {
        tok->lit_type = LIT_INT;
        tok->lit.i64  = (i64_t)strtoll(buf, NULL, 10);
    }
    else
    {
        tok->lit_type = LIT_FLOAT;
        tok->lit.f64  = strtod(buf, NULL);
    }

    free(buf);
}

static int is_valid_escape_char(char e)
{
    switch (e)
    {
        case 'n':
        case 't':
        case 'r':
        case '0':
        case '"':
        case '\\':
            return 1;
        default:
            return 0;
    }
}

static void scan_string(lexer_t* lexer,   token_t* tok,
                        size_t start_pos, size_t start_line, size_t start_col)
{
    lexer_advance(lexer);

    size_t content_start = lexer->pos;

    while (1)
    {
        char c = lexer_peek_char(lexer);

        if (c == '\0')
        {
            LEXER_SET_ERROR(lexer, start_pos,
                            "Unterminated string literal starting at line %zu, column %zu",
                             start_line, start_col);

            token_ctor(tok, TOK_ERROR, lexer, start_pos, start_line, start_col);
            return;
        }

        if (c == '\\')
        {
            size_t esc_offset = lexer->pos;
            size_t esc_line   = lexer->line;
            size_t esc_col    = lexer->column;

            lexer_advance(lexer);
            char e = lexer_peek_char(lexer);

            if (e == '\0')
            {
                LEXER_SET_ERROR(lexer, start_pos,
                                "Unterminated string literal starting at line %zu, column %zu",
                                start_line, start_col);
                token_ctor(tok, TOK_ERROR, lexer, start_pos, start_line, start_col);
                return;
            }

            if (!is_valid_escape_char(e))
            {
                LEXER_SET_ERROR(lexer, esc_offset,
                                "Invalid escape sequence \"\\%c\" at line %zu, column %zu",
                                e, esc_line, esc_col);

                lexer_advance(lexer);
                token_ctor(tok, TOK_ERROR, lexer, start_pos, start_line, start_col);
                return;
            }

            lexer_advance(lexer);
            continue;
        }

        if (c == '"')
            break;

        lexer_advance(lexer);
    }

    lexer_advance(lexer);

    tok->kind       = TOK_STRING_LITERAL;
    tok->pos.offset = start_pos;
    tok->pos.line   = start_line;
    tok->pos.column = start_col;

    const char* src = (lexer && lexer->op_data) ? lexer->op_data->buffer : NULL;
    if (src)
    {
        tok->buffer = src + content_start;
        tok->length = (lexer->pos - 1) - content_start;
    }
    else
    {
        tok->buffer = NULL;
        tok->length = 0;
    }

    tok->lit_type = LIT_NONE;
    tok->lit.i64  = 0;
    tok->name_id  = SIZE_MAX;
}

err_t lexer_ctor(lexer_t* lexer, operational_data_t* op_data, nametable_t*  nametable)
{
    if (!lexer || !op_data || !op_data->buffer)
        return ERR_BAD_ARG;

    lexer->op_data = op_data;

    lexer->pos     = 0;
    lexer->line    = 1;
    lexer->column  = 1;

    lexer->nametable   = nametable;
    lexer->has_current = 0;

    memset(&lexer->current, 0, sizeof(lexer->current));

    return OK;
}

err_t lexer_dtor(lexer_t* lexer)
{
    if (!lexer)
        return ERR_BAD_ARG;

    lexer->op_data   = NULL;
    lexer->nametable = NULL;
    lexer->pos       = 0;
    lexer->line      = 0;
    lexer->column    = 0;
    lexer->has_current = 0;
    memset(&lexer->current, 0, sizeof(lexer->current));
    return OK;
}

err_t lexer_reset(lexer_t* lexer)
{
    if (!lexer)
        return ERR_BAD_ARG;

    lexer->pos         = 0;
    lexer->line        = 1;
    lexer->column      = 1;
    lexer->has_current = 0;

    return OK;
}

static void make_eof_token(lexer_t* lexer, token_t* tok)
{
    tok->kind        = TOK_EOF;
    tok->pos.offset  = lexer->pos;
    tok->pos.line    = lexer->line;
    tok->pos.column  = lexer->column;

    tok->buffer      = NULL;
    tok->length      = 0;

    tok->lit_type    = LIT_NONE;
    tok->lit.i64     = 0;
    tok->name_id     = SIZE_MAX;
}

#define LEXER_SCAN_BRANCH(_cond, _do_advance, _scan_fn)             \
    block_begin                                                     \
        if ((_cond)) {                                              \
            if (_do_advance)                                        \
                lexer_advance(lexer);                               \
            _scan_fn(lexer, out, start_pos, start_line, start_col); \
            lexer->current     = *out;                              \
            lexer->has_current = 1;                                 \
            return OK;                                              \
        }                                                           \
    block_end

#define LEXER_TRY_TWO_CHAR_OP(ch1, ch2, tok_kind)                             \
    if (c == (ch1) && next == (ch2))                                          \
    {                                                                         \
        lexer_advance(lexer);                                                 \
        lexer_advance(lexer);                                                 \
        token_ctor(out, (tok_kind), lexer, start_pos, start_line, start_col); \
        goto done_token;                                                      \
    }

err_t lexer_next(lexer_t* lexer, token_t* out)
{
    if (!lexer || !out)
        return ERR_BAD_ARG;

    lexer_skip(lexer);

    size_t start_pos  = lexer->pos;
    size_t start_line = lexer->line;
    size_t start_col  = lexer->column;

    char c = lexer_peek_char(lexer);
    if (c == '\0')
    {
        make_eof_token(lexer, out);
        lexer->current     = *out;
        lexer->has_current = 1;
        return OK;
    }

    LEXER_SCAN_BRANCH(is_ident_start((unsigned char)c), 1, scan_identifier);
    LEXER_SCAN_BRANCH(isdigit((unsigned char)c),        1, scan_number);
    LEXER_SCAN_BRANCH(c == '"',                         0, scan_string);

    char next = lexer_peek_next_char(lexer);

    LEXER_TRY_TWO_CHAR_OP('|', '|', TOK_OP_OR);
    LEXER_TRY_TWO_CHAR_OP('&', '&', TOK_OP_AND);
    LEXER_TRY_TWO_CHAR_OP('=', '=', TOK_OP_EQ);
    LEXER_TRY_TWO_CHAR_OP('!', '=', TOK_OP_NEQ);
    LEXER_TRY_TWO_CHAR_OP('<', '=', TOK_OP_LTE);
    LEXER_TRY_TWO_CHAR_OP('>', '=', TOK_OP_GTE);

    lexer_advance(lexer);

    switch (c)
    {
        case '(':
            token_ctor(out, TOK_LPAREN,    lexer, start_pos, start_line, start_col);
            break;
        case ')':
            token_ctor(out, TOK_RPAREN,    lexer, start_pos, start_line, start_col);
            break;
        case ',':
            token_ctor(out, TOK_COMMA,     lexer, start_pos, start_line, start_col);
            break;
        case ';':
            token_ctor(out, TOK_SEMICOLON, lexer, start_pos, start_line, start_col);
            break;
        
        case '+':
            token_ctor(out, TOK_OP_PLUS,   lexer, start_pos, start_line, start_col);
            break;
        case '-':
            token_ctor(out, TOK_OP_MINUS,  lexer, start_pos, start_line, start_col);
            break;
        case '*':
            token_ctor(out, TOK_OP_MUL,    lexer, start_pos, start_line, start_col);
            break;
        case '/':
            token_ctor(out, TOK_OP_DIV,    lexer, start_pos, start_line, start_col);
            break;
        case '^':
            token_ctor(out, TOK_OP_POW,    lexer, start_pos, start_line, start_col);
            break;
        case '!':
            token_ctor(out, TOK_OP_NOT,    lexer, start_pos, start_line, start_col);
            break;

        case '<':
            token_ctor(out, TOK_OP_LT,     lexer, start_pos, start_line, start_col);
            break;
        case '>':
            token_ctor(out, TOK_OP_GT,     lexer, start_pos, start_line, start_col);
            break;

        default:
            token_ctor(out, TOK_ERROR, lexer, start_pos, start_line, start_col);

            if (lexer->op_data && lexer->op_data->error_msg[0] == '\0')
            {
                lexer->op_data->error_pos = start_pos;

                char display[8] = {0};
                if (c >= 32 && c < 127)
                {
                    display[0] = c;
                    display[1] = '\0';
                }
                else
                    strcpy(display, "?");

                LEXER_SET_ERROR(lexer, start_pos,
                                "Invalid character '%s' at line %zu, column %zu",
                                display, start_line, start_col);
            }
        break;
    }

    done_token:
    lexer->current     = *out;
    lexer->has_current = 1;

    return OK;
}

#undef LEXER_SCAN_BRANCH
#undef LEXER_TRY_TWO_CHAR_OP

err_t lexer_peek(lexer_t* lexer, token_t* out)
{
    if (!lexer || !out)
        return ERR_BAD_ARG;

    if (!lexer->has_current)
    {
        err_t rc = lexer_next(lexer, &lexer->current);
        if (rc != OK)
            return rc;

        lexer->has_current = 1;
    }

    *out = lexer->current;
    return OK;
}

int lexer_is_eof(const token_t* tok)
{
    return tok && tok->kind == TOK_EOF;
}

const char* token_kind_to_cstr(token_kind_t kind)
{
    switch (kind)
    {
#define TOKEN_STR(sym, text) case sym: return text;
        TOKEN_LIST(TOKEN_STR)
#undef TOKEN_STR

        default:
            return "<unknown-token>";
    }
}

#define LEXER_LOG_TOKEN(_extra_fmt, ...)             \
    log_printf(DEBUG,                                \
               "TOKEN %-18s at %zu:%zu " _extra_fmt, \
               kind_str,                             \
               tok->pos.line,                        \
               tok->pos.column,                      \
               __VA_ARGS__)

static err_t lexer_dump_token(const token_t* tok)
{
    if (!tok)
        return ERR_BAD_ARG;

    const char* kind_str = token_kind_to_cstr(tok->kind);

    char* snippet = NULL;
    if (tok->buffer)
    {
        snippet = strdup(tok->buffer);
        snippet[tok->length] = '\0';
    }

    if (tok->kind == TOK_NUMERIC_LITERAL)
    {
        if (tok->lit_type == LIT_INT)
            LEXER_LOG_TOKEN("int=%lld text=\"%s\"",
                            (long long)tok->lit.i64,
                            snippet);
        else if (tok->lit_type == LIT_FLOAT)
            LEXER_LOG_TOKEN("float=%g text=\"%s\"",
                            tok->lit.f64,
                            snippet);
        else
            LEXER_LOG_TOKEN("text=\"%s\"",
                            snippet);
    }
    else
        LEXER_LOG_TOKEN("text=\"%s\"",
                        snippet); 
    free(snippet);
    return OK;
}


err_t lexer_stream(operational_data_t* op_data,
                   token_t**           out_tokens,
                   size_t*             out_count,
                   nametable_t*        out_nametable)
{
    if (!CHECK(ERROR, op_data != NULL, "lexer_stream: op_data is NULL"))
        return ERR_BAD_ARG;

    if (!CHECK(ERROR, op_data->buffer != NULL, "lexer_stream: op_data->buffer is NULL"))
        return ERR_BAD_ARG;

    if (!CHECK(ERROR, out_tokens != NULL, "lexer_stream: out_tokens is NULL"))
        return ERR_BAD_ARG;

    if (!CHECK(ERROR, out_count != NULL, "lexer_stream: out_count is NULL"))
        return ERR_BAD_ARG;

    if (!CHECK(ERROR, out_nametable != NULL, "lexer_stream: out_nametable is NULL"))
        return ERR_BAD_ARG;

    *out_tokens = NULL;
    *out_count  = 0;

    op_data->error_pos    = 0;
    op_data->error_msg[0] = '\0';

    err_t rc = nametable_ctor(out_nametable);
    if (rc != OK)
    {
        snprintf(op_data->error_msg, sizeof(op_data->error_msg),
                 "Failed to construct name table (err=%d)", rc);
        log_printf(ERROR, "%s", op_data->error_msg);
        return rc;
    }

    lexer_t lexer = { 0 };
    rc = lexer_ctor(&lexer, op_data, out_nametable);
    if (rc != OK)
    {
        snprintf(op_data->error_msg, sizeof(op_data->error_msg),
                 "Failed to initialize lexer (err=%d)", rc);
        log_printf(ERROR, "%s", op_data->error_msg);

        nametable_dtor(out_nametable);
        return rc;
    }

    token_t* tokens   = NULL;
    size_t   capacity = 0;
    size_t   count    = 0;

    token_t tok = { 0 };

    for (;;)
    {
        rc = lexer_next(&lexer, &tok);
        if (rc != OK)
        {
            if (op_data->error_msg[0] == '\0')
            {
                LEXER_SET_ERROR(&lexer, lexer.pos, 
                                "Lexer internal error at line %zu, column %zu (err=%d)", 
                                lexer.line, lexer.column, rc);
            }

            log_printf(ERROR, "%s", op_data->error_msg);

            free(tokens);
            lexer_dtor(&lexer);
            nametable_dtor(out_nametable);
            return rc;
        }

        lexer_dump_token(&tok);

        if (tok.kind == TOK_ERROR)
        {
            if (op_data->error_msg[0] == '\0')
            {
                op_data->error_pos = tok.pos.offset;

                const char* start = tok.buffer ? tok.buffer : "?";
                size_t len        = tok.buffer ? tok.length : 1;

                char* snippet = NULL;
                snippet = strdup(start);
                snippet[len] = '\0';

                LEXER_SET_ERROR(&lexer, tok.pos.offset, 
                                "Lexical error at line %zu, column %zu near \"%s\"", 
                                tok.pos.line, tok.pos.column, snippet[0] ? snippet : "?");
                free(snippet);
            }

            log_printf(ERROR, "%s", op_data->error_msg);

            free(tokens);
            lexer_dtor(&lexer);
            nametable_dtor(out_nametable);
            return ERR_SYNTAX;
        }

        if (count >= capacity)
        {
            size_t new_cap = capacity ? capacity * 2 : 8;
            token_t* new_tokens = (token_t*)realloc(tokens, new_cap * sizeof(token_t));
            if (!new_tokens)
            {
                snprintf(op_data->error_msg, sizeof(op_data->error_msg),
                         "Out of memory in lexer_stream while growing token buffer");
                log_printf(ERROR, "%s", op_data->error_msg);

                free(tokens);
                lexer_dtor(&lexer);
                nametable_dtor(out_nametable);
                return ERR_ALLOC;
            }

            tokens   = new_tokens;
            capacity = new_cap;
        }

        tokens[count++] = tok;

        if (tok.kind == TOK_EOF)
            break;
    }

    lexer_dtor(&lexer);

    *out_tokens = tokens;
    *out_count  = count;

    return OK;
}

#undef LEXER_SET_ERROR
