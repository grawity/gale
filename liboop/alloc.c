#include "oop.h"
#include <stdlib.h>

void *(*oop_malloc)(size_t) = malloc;
void (*oop_free)(void *) = free;
