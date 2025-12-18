#ifndef INSTRUCTIONS_LIST
#define INSTRUCTIONS_LIST

#define INSTRUCTION_SET_VERSION_MAJOR 3U
#define INSTRUCTION_SET_VERSION_MINOR 1U

#define INSTRUCTION_LIST(X)        \
    X(NOP,    "NOP",    0,   0)    \
                                   \
    X(HLT,    "HLT",    0,   1)    \
    X(PUSH,   "PUSH",   1,   2)    \
    X(POP,    "POP",    0,   3)    \
    X(OUT,    "OUT",    0,   4)    \
    X(TOPOUT, "TOPOUT", 0,   5)    \
    X(IN,     "IN",     0,   6)    \
    X(CALL,   "CALL",   1,   7)    \
    X(RET,    "RET",    0,   8)    \
    X(DRAW,   "DRAW",   0,   9)    \
                                   \
    X(ADD,    "ADD",    0,  10)    \
    X(SUB,    "SUB",    0,  11)    \
    X(MUL,    "MUL",    0,  12)    \
    X(DIV,    "DIV",    0,  13)    \
    X(POW,    "POW",    0,  14)    \
    X(POWF,   "POWF",   0,  15)    \
                                   \
    X(JMP,    "JMP",    1,  16)    \
    X(JB,     "JB",     1,  17)    \
    X(JBE,    "JBE",    1,  18)    \
    X(JA,     "JA",     1,  19)    \
    X(JAE,    "JAE",    1,  20)    \
    X(JE,     "JE",     1,  21)    \
    X(JNE,    "JNE",    1,  22)    \
                                   \
    X(DUMP,   "DUMP",   0,  23)    \
                                   \
    X(PUSHR,  "PUSHR",  1,  33)    \
    X(POPR,   "POPR",   1,  34)    \
                                   \
    X(PUSHM,  "PUSHM",  1,  35)    \
    X(POPM,   "POPM",   1,  36)    \
    X(PUSHVM, "PUSHVM", 1,  37)    \
    X(POPVM,  "POPVM",  1,  38)    \
    X(CLEANVM,"CLEANVM",0,  39)    \
                                   \
    X(NOT,    "NOT",    0,  42)    \
    X(OR,     "OR",     0,  43)    \
    X(AND,    "AND",    0,  44)    \
    X(XOR,    "XOR",    0,  45)    \
    X(SHL,    "SHL",    0,  46)    \
    X(SHR,    "SHR",    0,  47)    \
                                   \
    X(FADD,   "FADD",   0,  64)    \
    X(FSUB,   "FSUB",   0,  65)    \
    X(FMUL,   "FMUL",   0,  66)    \
    X(FDIV,   "FDIV",   0,  67)    \
    X(FPOW,   "FPOW",   0,  68)    \
    X(FPOWF,  "FPOWF",  0,  69)    \
                                   \
    X(FIN,    "FIN",    0,  70)    \
    X(FOUT,   "FOUT",   0,  71)    \
    X(FTOPOUT,"FTOPOUT",0,  72)    \
                                   \
    X(FPUSHR, "FPUSHR", 1,  76)    \
    X(FPOPR,  "FPOPR",  1,  77)    \
                                   \
    X(FLOOR,  "FLOOR",  0,  80)    \
    X(CEIL,   "CEIL",   0,  81)    \
    X(ROUND,  "ROUND",  0,  82)    \
    X(FCMP,   "FCMP",   0,  83)    \
                                   \
    X(ITOF,   "ITOF",   0,  90)    \
    X(FTOI,   "FTOI",   0,  91)    \
                                   \
    X(CIN,    "CIN",    0,  100)   \
    X(CTOPOUT,"CTOPOUT",0,  101)

#endif
