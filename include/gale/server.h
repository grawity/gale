#ifndef GALE_SERVER_H
#define GALE_SERVER_H

extern int gale_debug;

void gale_dprintf(int level,const char *fmt,...);
void gale_daemon(int keep_tty);
void gale_cleanup(void (*)(void));

#endif
