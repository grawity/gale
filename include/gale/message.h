#ifndef MESSAGE_H
#define MESSAGE_H

struct gale_message {
	char *category;
	int data_size;
	char *data;
	int ref;
};

struct gale_message *new_message(void);
void addref_message(struct gale_message *);
void release_message(struct gale_message *);

#endif
