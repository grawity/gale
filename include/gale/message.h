/* message.h -- management of message (puff) objects. */

#ifndef MESSAGE_H
#define MESSAGE_H

/* The message object is reference counted; this is done mostly for the 
   server, but might prove useful elsewhere. */

struct gale_message {
	char *category;        /* Owned pointer.  NUL-terminated category. */
	int data_size;         /* Size of message data, in characters. */
	char *data;            /* Owned pointer to the data (not NUL-ended!) */
	int ref;               /* Reference count. */
};

/* Create a new message with a single reference count and fields empty. */
struct gale_message *new_message(void);

/* Manage reference counts on a message.  release_message() will delete the
   message (and free the category and data) if the reference count reaches 0 */
void addref_message(struct gale_message *);
void release_message(struct gale_message *);

#endif
