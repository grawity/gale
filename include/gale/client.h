#ifndef CLIENT_H
#define CLIENT_H

struct gale_client {
	int socket;
	struct gale_link *link;
	char *server,*subscr;
};

struct gale_client *gale_open(const char *spec);
void gale_close(struct gale_client *);
void gale_retry(struct gale_client *);
int gale_error(struct gale_client *);
int gale_send(struct gale_client *);
int gale_next(struct gale_client *);

#endif
