#include "sy_parser/utils.h"

#include <stdlib.h>

char *my_strdup(const char *s) {
    if (s == NULL) {
        return NULL;
    }

    size_t len = 0;
    while (s[len] != '\0') {
        len++;
    }

    char *copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }

    for (size_t i = 0; i <= len; i++) {
        copy[i] = s[i];
    }

    return copy;
}

unsigned long my_str_hash(const char *str) {
    unsigned long hash = 5381;
    int c;
    while ((c = *str++)) hash = ((hash << 5) + hash) + c; /* hash * 33 + c */
    return hash;
}
