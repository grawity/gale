/* client.h -- simplified, high-level interface to gale */

#ifndef CLIENT_H
#define CLIENT_H

/* Client structure.  Don't write any of these fields yourself. */

struct gale_client {
	int socket;                   /* The file descriptor (for select). */
	struct gale_link *link;       /* The link object (see link.h) */
	char *server,*subscr;         /* Name of server and sub list */
};

/* Open a connection to the server (defined by GALE_SERVER).  "spec" is the
   subscription list to use; NULL if you don't want to subscribe. */
struct gale_client *gale_open(const char *spec);

/* Close a connection opened by gale_open. */
void gale_close(struct gale_client *);

/* If you get an error, use this to reattempt connection to the server.  It
   will retry with progressive delay until it succeeds. */
void gale_retry(struct gale_client *);

/* Return nonzero if an error has occurred (and you should call gale_retry), 0
   otherwise.  If this returns nonzero, you can't do either of the following 
   two operations on the connection. */
int gale_error(struct gale_client *);

/* Transmit any queued messages on the link.  Returns 0 if successful. */
int gale_send(struct gale_client *);

/* Wait for the next message on the link.  Returns 0 if successful.  (Extract
   the actual message from the gale_link -- see link.h.) */
int gale_next(struct gale_client *);

#endif
