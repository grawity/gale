#ifndef GALE_SERVER_H
#define GALE_SERVER_H

extern int gale_debug;

void gale_dprintf(int level,const char *fmt,...);
void gale_daemon(void);
void gale_die(char *,int err);
void gale_warn(char *,int err);

#endif
