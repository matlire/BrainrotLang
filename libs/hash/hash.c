#include "hash.h"

size_t sdbm(const char * str) 
{
    size_t hash = 0;
    int c = 0;

    while ((c = *str++) != '\0') 
    {
        hash = c + (hash << 6) + (hash << 16) - hash;
    }

    return hash;
}

size_t sdbm_n(const char* str, size_t len)
{
    size_t hash = 0;
    for (size_t i = 0; i < len; ++i)
    {
        hash = (unsigned char)str[i] + (hash << 6) + (hash << 16) - hash;
    }
    return hash;
}
