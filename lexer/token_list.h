#ifndef TOKENS_LIST_H
#define TOKENS_LIST_H

#define TOKEN_LIST_BASE(X)                    \
    X(TOK_EOF,             "EOF")             \
    X(TOK_ERROR,           "ERROR")           \
    X(TOK_IDENTIFIER,      "IDENTIFIER")      \
    X(TOK_NUMERIC_LITERAL, "NUMERIC_LITERAL") \
    X(TOK_STRING_LITERAL,  "STRING_LITERAL")  \
                                              \
    X(TOK_LPAREN,          "(")               \
    X(TOK_RPAREN,          ")")               \
    X(TOK_LBRACE,          "{")               \
    X(TOK_RBRACE,          "}")               \
    X(TOK_COMMA,           ",")               \
    X(TOK_SEMICOLON,       ";")               \
                                              \
    X(TOK_OP_ASSIGN,       "=")               \
    X(TOK_OP_OR,           "||")              \
    X(TOK_OP_AND,          "&&")              \
    X(TOK_OP_EQ,           "==")              \
    X(TOK_OP_NEQ,          "!=")              \
    X(TOK_OP_GT,           ">")               \
    X(TOK_OP_LT,           "<")               \
    X(TOK_OP_GTE,          ">=")              \
    X(TOK_OP_LTE,          "<=")              \
    X(TOK_OP_PLUS,         "+")               \
    X(TOK_OP_MINUS,        "-")               \
    X(TOK_OP_MUL,          "*")               \
    X(TOK_OP_DIV,          "/")               \
    X(TOK_OP_POW,          "^")               \
    X(TOK_OP_NOT,          "!")

#define KEYWORD_LIST_SANE(X)                  \
    X(TOK_KW_INT,      "int")                 \
    X(TOK_KW_FLOAT,    "float")               \
    X(TOK_KW_PTR,      "ptr")                 \
                                              \
    X(TOK_KW_VOID,     "void")                \
                                              \
    X(TOK_KW_WHILE,    "while")               \
    X(TOK_KW_FOR,      "for")                 \
    X(TOK_KW_IF,       "if")                  \
    X(TOK_KW_ELIF,     "elif")                \
    X(TOK_KW_ELSE,     "else")                \
    X(TOK_KW_BREAK,    "break")               \
    X(TOK_KW_RETURN,   "return")              \
    X(TOK_KW_CALL,     "call")                \
                                              \
    X(TOK_KW_PRINT,    "print")               \
    X(TOK_KW_IPRINT,   "iprint")              \
    X(TOK_KW_FPRINT,   "fprint")              \
                                              \
    X(TOK_KW_FLOOR,    "floor")               \
    X(TOK_KW_CEIL,     "ceil")                \
    X(TOK_KW_ROUND,    "round")               \
    X(TOK_KW_ITOF,     "itof")                \
    X(TOK_KW_FTOI,     "ftoi")

#define KEYWORD_LIST_AM_TIKTOK(X)             \
    X(TOK_KW_INT,      "npc")                 \
    X(TOK_KW_FLOAT,    "homie")               \
    X(TOK_KW_PTR,      "sus")                 \
                                              \
    X(TOK_KW_VOID,     "simp")                \
                                              \
    X(TOK_KW_WHILE,    "lowkey")              \
    X(TOK_KW_FOR,      "highkey")             \
    X(TOK_KW_IF,       "alpha")               \
    X(TOK_KW_ELIF,     "omega")               \
    X(TOK_KW_ELSE,     "sigma")               \
    X(TOK_KW_BREAK,    "gg")                  \
    X(TOK_KW_RETURN,   "micdrop")             \
    X(TOK_KW_CALL,     "bruh")                \
                                              \
    X(TOK_KW_PRINT,    "based")               \
    X(TOK_KW_IPRINT,   "mid")                 \
    X(TOK_KW_FPRINT,   "peak")                \
                                              \
    X(TOK_KW_FLOOR,    "stan")                \
    X(TOK_KW_CEIL,     "aura")                \
    X(TOK_KW_ROUND,    "delulu")              \
    X(TOK_KW_ITOF,     "goober")              \
    X(TOK_KW_FTOI,     "bozo")

#if defined(__SYNTAX_SANE) && defined(__SYNTAX_AM_TIKTOK)
    #error "Choose only one syntax: BRL_SYNTAX_SANE or BRL_SYNTAX_AM_TIKTOK"
#elif defined(__SYNTAX_SANE)
    #define KEYWORD_LIST(X) KEYWORD_LIST_SANE(X)
#elif defined(__SYNTAX_AM_TIKTOK)
    #define KEYWORD_LIST(X) KEYWORD_LIST_AM_TIKTOK(X)
#else
    #error "No syntax selected. Define BRL_SYNTAX_SANE or BRL_SYNTAX_AM_TIKTOK"
#endif

#define TOKEN_LIST(X)  \
    TOKEN_LIST_BASE(X) \
    KEYWORD_LIST(X)

#endif
