#include "instruction_set.h"

const unsigned char INSTRUCTION_BINARY_MAGIC[INSTRUCTION_BINARY_MAGIC_LEN] =
{
    'T', 'A', 'S', 'M'
};

static const instruction_t INSTRUCTIONS[INSTRUCTION_TABLE_CAPACITY] =
{
#define INSTRUCTION_INIT(symbol, label, args, opcode) \
    [symbol] = { .name = label, .id = symbol, .expected_args = (size_t)(args) },
    INSTRUCTION_LIST(INSTRUCTION_INIT)
#undef INSTRUCTION_INIT
};


// sdbm hashing for faster instruction mapping
static inline unsigned long instruction_hash(const unsigned char *str) {
    unsigned long hash = 0ul;
    char          c    = 0;

    while ((c = *str++) != '\0') {
        hash = (unsigned long)c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

typedef struct {
    unsigned long   hash;
    instruction_set id;
} instr_hash_t;

static instr_hash_t INSTR_HASH_TABLE[INSTRUCTION_TABLE_CAPACITY] = { 0 };
static int          INSTR_HASHES_BUILT                           = 0;

static void instruction_build_hashes_once(void)
{
    if (INSTR_HASHES_BUILT) return;

    for (size_t i = 0; i < INSTRUCTION_TABLE_CAPACITY; ++i) {
        const instruction_t* meta = &INSTRUCTIONS[i];

        if (meta && meta->name) {
            INSTR_HASH_TABLE[i].hash = instruction_hash((const unsigned char*)meta->name);
            INSTR_HASH_TABLE[i].id   = meta->id;
        } else {
            INSTR_HASH_TABLE[i].hash = 0;
            INSTR_HASH_TABLE[i].id   = UNDEF;
        }
    }
    INSTR_HASHES_BUILT = 1;
}

static instruction_set instruction_lookup_hash(const unsigned char* name)
{
    if (!name) return UNDEF;

    instruction_build_hashes_once();

    const unsigned long hash = instruction_hash(name);

    for (size_t i = 0; i < INSTRUCTION_TABLE_CAPACITY; ++i) // binary search, over engineering
    {
        if (INSTR_HASH_TABLE[i].hash == hash)
            return INSTR_HASH_TABLE[i].id;
    }
    return UNDEF;
}

static int instruction_id_is_valid(const instruction_set id)
{
    return id >= 0 && id < INSTRUCTION_TABLE_CAPACITY;
}

const instruction_t* instruction_get(const instruction_set id)
{
    if (!instruction_id_is_valid(id)) return NULL;
    const instruction_t* meta = &INSTRUCTIONS[id];
    return (meta->name != NULL) ? meta : NULL;
}

const instruction_t* instruction_table(void)
{
    return INSTRUCTIONS;
}

size_t instruction_table_size(void)
{
    return INSTRUCTION_TABLE_CAPACITY;
}

instruction_set_version_t instruction_set_version(void)
{
    instruction_set_version_t version =
    {
        .major = INSTRUCTION_SET_VERSION_MAJOR,
        .minor = INSTRUCTION_SET_VERSION_MINOR
    };

    return version;
}

unsigned int instruction_set_version_code(void)
{
    return (INSTRUCTION_SET_VERSION_MAJOR << 16) | INSTRUCTION_SET_VERSION_MINOR;
}

instruction_set map_instruction(const char* str)
{
    if (!str) return UNDEF;
    return instruction_lookup_hash((const unsigned char*)str);
}

size_t expect_arg(const instruction_set instruction)
{
    const instruction_t* meta = instruction_get(instruction);
    if (!meta) return 0;

    return (size_t)meta->expected_args;
}
