#include <string.h>
#include "gale/header.h"

int parse_header(char **next,char **key,char **data,char *end) {
	char *tmp;

	if (*next == end) return 0;
	if (**next == '\r') {
		++*next;
		if (*next != end && **next == '\n') ++*next;
		return 0;
	}

	*key = *next;
	while (*next != end && **next != ':' && **next != '\r') ++*next;

	if (*next == end || **next != ':') {
		*next = *key;
		return 0;
	}

	tmp = *next;
	*tmp = '\0';
	++*next;
	while (*next != end && **next == ' ') ++*next;
	*data = *next;
	while (*next != end && **next != '\r') ++*next;
	if (*next != end) {
		**next = '\0';
		++*next;
		if (*next != end && **next == '\n') ++*next;
		return 1;
	}

	*tmp = ':';
	*next = *key;
	return 0;
}
