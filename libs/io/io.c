#include "io.h"

size_t parse_arguments(const int argc, char* const argv[],          \
                       const char** in_file, const char** out_file)
{
    if (!CHECK(ERROR, argv != NULL, "Argv is NULL")) return 0;

    size_t parsed = 0;
    for (int i = 1; i < argc; i++)
    {
        const char* current = argv[i];

        if (strcmp(current, "--infile") == 0)
        {
            if (!CHECK(ERROR, in_file != NULL, 
                       "--infile provided but op_data in_file pointer is NULL")) return 0;
            if (!CHECK(ERROR, i + 1 < argc,     
                       "--infile flag requires a file")) return 0;
            if (!CHECK(ERROR, *in_file == NULL, 
                       "--infile specified multiple times")) return 0;

            *in_file = argv[++i];
            parsed++;
            continue;
        }

        if (strcmp(current, "--outfile") == 0)
        {
            if (!CHECK(ERROR, out_file != NULL, 
                       "--outfile is not supported in this mode")) return 0;
            if (!CHECK(ERROR, i + 1 < argc,      
                       "--outfile flag requires a value")) return 0;
            if (!CHECK(ERROR, *out_file == NULL, 
                       "--outfile specified multiple times")) return 0;

            *out_file = argv[++i];
            parsed++;
            continue;
        }

        log_printf(WARN, "Unknown argument '%s' ignored", current);
    }

    return parsed;
}

FILE *load_file (const char * const name, const char * const mode)
{
    if (!CHECK(ERROR, name != NULL && mode != NULL, "Invalid file or mode pointer")) return NULL;

    FILE* f = fopen(name, mode);
    if(!CHECK(ERROR, f != NULL,
          "Can't open file %s in mode %s", name, mode)) return NULL;

    return f;
}

ssize_t get_file_size_stat(const char * const filename) {
    if (!CHECK(ERROR, filename != NULL,
          "No filename provided!")) return -1;

    struct stat st;
    memset(&st, 0, sizeof(st));
    if (stat(filename, &st) == 0) {
        return (ssize_t)st.st_size;
    } else {
        log_printf(ERROR, "Error getting file stats for %s", filename);
        return -1;
    }
}

size_t read_file(FILE *file, operational_data_t* op_data)
{
    if (!CHECK(ERROR, file != NULL && op_data != NULL, 
          "Error reading file, some data is missing")) return 0;
    if (!CHECK(ERROR, op_data->buffer != NULL, "Operational buffer is NULL")) return 0;
    if (!CHECK(ERROR, op_data->buffer_size > 0, "Operational buffer size is zero")) return 0;

    size_t read_bytes = fread(op_data->buffer, sizeof(char), op_data->buffer_size, file);
    if (read_bytes == 0)
    {
        if (ferror(file))
        {
            log_printf(ERROR, "Failed to read from file");
        }
        return 0;
    }

    op_data->buffer[read_bytes] = '\0';
    
    return read_bytes;
}

size_t clean_file(const char * const filename)
{
    FILE* file_out = load_file(filename, "w");
    if (!file_out) return 0;

    fclose(file_out);
    return 1;
}

void flush_input(void)
{
    int ch = 0;
    while ((ch = getchar()) != '\n' && ch != EOF) {  }
}

size_t read_line(char *buf, size_t cap)
{
    if (!buf || cap == 0) return -1;

    buf[0] = '\0';

    if (cap == 1) 
    {
        flush_input();
        return 0;
    }

    char fmt[32] = {0};
    size_t maxlen = cap - 1;
    snprintf(fmt, sizeof(fmt), "%%%zu[^\n]", maxlen);

    int rc = scanf(fmt, buf);
    if (rc == EOF || rc == 0) 
        return 0;

    flush_input();

    size_t len = strlen(buf);
    if (len && buf[len - 1] == '\r') 
    {
        buf[--len] = '\0';
    }

    return len;
}

