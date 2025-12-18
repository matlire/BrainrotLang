#ifndef INSTRUCTIONS_SET_H
#define INSTRUCTIONS_SET_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <float.h>
#include <inttypes.h>

#include "instructions_list.h"

#define MAX_INSTRUCTION_LEN  16
#define MAX_LINE_LEN         64
#define MAX_INSTRUCTION_ARGS 4

#define INSTRUCTION_BINARY_MAGIC_LEN  4U

typedef int64_t  i64_t;
typedef uint64_t u64_t;
typedef double   f64_t;

typedef union
{
    i64_t i64;
    u64_t u64;
    f64_t f64;
    void* p;
} cell64_t;

#define CPU_CELL_SIZE (sizeof(cell64_t))

typedef struct
{
    unsigned char magic[INSTRUCTION_BINARY_MAGIC_LEN];
    unsigned char version_major;
    unsigned char version_minor;
    size_t        code_size;
} instruction_binary_header_t;

#define INSTRUCTION_BINARY_HEADER_SIZE (sizeof(instruction_binary_header_t))

extern const unsigned char INSTRUCTION_BINARY_MAGIC[INSTRUCTION_BINARY_MAGIC_LEN];

typedef struct
{
    unsigned int major;
    unsigned int minor;
} instruction_set_version_t;

typedef enum
{
#define INSTRUCTION_ENUM(symbol, name, args, opcode) symbol = (opcode),
    INSTRUCTION_LIST(INSTRUCTION_ENUM)
#undef INSTRUCTION_ENUM
    INSTRUCTION_TABLE_CAPACITY,
    UNDEF = 666
} instruction_set;

typedef struct
{
    const char*     name;
    instruction_set id;
    size_t          expected_args;
} instruction_t;

const instruction_t* instruction_get        (instruction_set id);
const instruction_t* instruction_table      (void);
size_t               instruction_table_size (void);

instruction_set      map_instruction        (const char* str);
size_t               expect_arg             (const instruction_set instruction);

instruction_set_version_t  instruction_set_version     (void);
unsigned int               instruction_set_version_code(void);

#endif
