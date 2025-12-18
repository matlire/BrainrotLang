#ifndef TOKENS_LIST_H
#define TOKENS_LIST_H

#define TOKEN_LIST_BASE(X)                           \
    X(TOK_EOF,             "EOF")                    \
    X(TOK_ERROR,           "ERROR")                  \
    X(TOK_IDENTIFIER,      "IDENTIFIER")             \
    X(TOK_NUMERIC_LITERAL, "NUMERIC_LITERAL")        \
    X(TOK_STRING_LITERAL,  "STRING_LITERAL")         \
                                                     \
    X(TOK_LPAREN,          "(")                      \
    X(TOK_RPAREN,          ")")                      \
    X(TOK_COMMA,           ",")                      \
    X(TOK_SEMICOLON,       ";")                      \
                                                     \
    X(TOK_OP_OR,           "||")                     \
    X(TOK_OP_AND,          "&&")                     \
    X(TOK_OP_EQ,           "==")                     \
    X(TOK_OP_NEQ,          "!=")                     \
    X(TOK_OP_GT,           ">")                      \
    X(TOK_OP_LT,           "<")                      \
    X(TOK_OP_GTE,          ">=")                     \
    X(TOK_OP_LTE,          "<=")                     \
    X(TOK_OP_PLUS,         "+")                      \
    X(TOK_OP_MINUS,        "-")                      \
    X(TOK_OP_MUL,          "*")                      \
    X(TOK_OP_DIV,          "/")                      \
    X(TOK_OP_POW,          "^")                      \
    X(TOK_OP_NOT,          "!")                      \
                                                     

#define KEYWORD_LIST(X)                              \
    X(TOK_KW_NPC,      "npc")                        \
    X(TOK_KW_HOMIE,    "homie")                      \
    X(TOK_KW_SUS,      "sus")                        \
                                                     \
    X(TOK_KW_SIMP,     "simp")                       \
                                                     \
    X(TOK_KW_YAP,      "yap")                        \
    X(TOK_KW_YAPITY,   "yapity")                     \
    X(TOK_KW_LOWKEY,   "lowkey")                     \
    X(TOK_KW_HIGHKEY,  "highkey")                    \
    X(TOK_KW_ALPHA,    "alpha")                      \
    X(TOK_KW_OMEGA,    "omega")                      \
    X(TOK_KW_SIGMA,    "sigma")                      \
    X(TOK_KW_GG,       "gg")                         \
    X(TOK_KW_MICDROP,  "micdrop")                    \
    X(TOK_KW_BRUH,     "bruh")                       \
                                                     \
    X(TOK_KW_GASLIGHT, "gaslight")                   \
                                                     \
    X(TOK_KW_BASED,    "based")                      \
    X(TOK_KW_MID,      "mid")                        \
    X(TOK_KW_PEAK,     "peak")                       \
                                                     \
    X(TOK_KW_STAN,     "stan")                       \
    X(TOK_KW_AURA,     "aura")                       \
    X(TOK_KW_DELULU,   "delulu")                     \
    X(TOK_KW_GOOBER,   "goober")                     \
    X(TOK_KW_BOZO,     "bozo")

#define TOKEN_LIST(X)                                \
    TOKEN_LIST_BASE(X)                               \
    KEYWORD_LIST(X)

#endif
