#include "oop.h"
#include <stdlib.h>

void *(*oop_malloc)(size_t) = malloc;
void (*oop_free)(void *) = free;
void *(*oop_realloc)(void *,size_t) = realloc;

int _oop_continue; /* this has to go somewhere */
