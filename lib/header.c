#include <stdio.h>
#include <string.h>
#include "gale/header.h"
#include "gale/server.h"

static int skip_newline(char **next,char *end) {
	if (*next == end) return 1;
	if (**next != '\r') return 0;
	++*next;
	if (*next != end && **next == '\n') ++*next;
	return 1;
}

static int complain(void) {
	gale_warn("gale: invalid header parsed\r\n",0);
	return 1;
}

int parse_header(char **next,char **key,char **data,char *end) {
	char *tmp;

	do {
		if (skip_newline(next,end)) return 0;
		*key = *next;
		while (*next != end && **next != ':' && **next != '\r') 
			++*next;
	} while (skip_newline(next,end) && complain());

	tmp = (*next)++;
	while (*next != end && **next == ' ') ++*next;
	*data = *next;
	while (*next != end && **next != '\r') ++*next;

	if (*next == end) {
		complain();
		return 0;
	}

	*(*next)++ = *tmp = '\0';
	if (*next != end && **next == '\n') ++*next;
	return 1;
}
