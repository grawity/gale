#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pwd.h>

#include "gale/all.h"

void *gale_malloc(int size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

struct gale_message *msg;
int aflag = 0,alloc = 0;
int have_from = 0,have_to = 0,have_time = 0,have_type = 0;
const char *pflag = NULL;
struct gale_id *recipient = NULL;

void reserve(int len) {
	char *tmp;
	while (msg->data_size + len >= alloc) {
		alloc = alloc ? alloc * 2 : 4096;
		tmp = gale_malloc(alloc);
		if (msg->data) {
			memcpy(tmp,msg->data,msg->data_size);
			gale_free(msg->data);
		}
		msg->data = tmp;
	}
}

void headers(void) {
	if (!have_type) {
		reserve(40);
		sprintf(msg->data + msg->data_size,
			"Content-type: text/plain\r\n");
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!aflag && !have_from && user_id->comment) {
		reserve(20 + strlen(user_id->comment));
		sprintf(msg->data + msg->data_size,
		        "From: %s\r\n",user_id->comment);
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!have_to && recipient && recipient->comment) {
		reserve(20 + strlen(recipient->comment));
		sprintf(msg->data + msg->data_size,
		        "To: %s\r\n",recipient->comment);
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!have_time) {
		reserve(20);
		sprintf(msg->data + msg->data_size,"Time: %lu\r\n",time(NULL));
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (pflag) {
		reserve(20 + strlen(pflag));
		sprintf(msg->data + msg->data_size,
		        "Receipt-To: %s\r\n",pflag);
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	reserve(2);
	msg->data[msg->data_size++] = '\r';
	msg->data[msg->data_size++] = '\n';
}

void usage(void) {
	struct gale_id *id = lookup_id("username@domain");
	fprintf(stderr,
		"%s\n"
		"usage: gsend [-uU] [-e id] [-p cat] cat\n"
		"flags: -e id       Encrypt message with <id>'s public key\n"
		"       -a          Do not sign message (anonymous)\n"
		"       -r          Do not retry server connection\n"
		"       -p cat      Request a return receipt\n"
		"       -u          Expect user-supplied headers\n"
		"       -U          Ditto, and don't supply default headers\n"
		"With -e, cat defaults to \"%s\".\n"
		,GALE_BANNER,id_category(id,"user",""));
	exit(1);
}

int main(int argc,char *argv[]) {
	struct gale_client *client;
	int arg,uflag = 0,rflag = 0;
	int ttyin = isatty(0),newline = 1;
	char *cp,*tmp,*eflag = NULL;

	gale_init("gsend",argc,argv);

	while ((arg = getopt(argc,argv,"hae:t:p:ruU")) != EOF) 
	switch (arg) {
	case 'a': aflag = 1; break;
	case 'e': eflag = optarg; break;
	case 'p': pflag = optarg; break;
	case 'r': rflag = 1; break;
	case 'u': uflag = 1; break;
	case 'U': uflag = 2; break;
	case 'h':
	case '?': usage();
	}

	msg = new_message();

	if (eflag) {
		gale_keys();
		recipient = lookup_id(eflag);
	}

	if (optind == argc && eflag)
		msg->category = id_category(recipient,"user","");
	else if (optind != argc - 1)
		usage();
	else
		msg->category = gale_strdup(argv[optind]);

	if (ttyin && getpwnam(msg->category))
		gale_alert(GALE_WARNING,"Category name matches username!  "
		                        "Did you forget the \"-e\" flag?",0);

	client = gale_open(NULL);

	while (gale_error(client)) {
		if (rflag) gale_alert(GALE_ERROR,"could not contact server",0);
		gale_retry(client);
	}

	if (ttyin) {
		printf("Message for %s in category \"%s\":\n",
			recipient ? recipient->name : "*everyone*",
			msg->category);
		printf("(End your message with EOF or a solitary dot.)\n");
	}

	if (uflag == 0) headers();

	for(;;) {
		reserve(80);
		cp = msg->data + msg->data_size;
		*cp = '\0'; /* just to make sure */
		fgets(cp,alloc - msg->data_size - 1,stdin);
		tmp = cp + strlen(cp);
		if (tmp == cp) break;
		if (ttyin && newline && !strcmp(cp,".\n")) break;
		if (uflag == 1) {
			if (!strncasecmp(cp,"From:",5)) 
				have_from = 1;
			else if (!strncasecmp(cp,"Content-type:",13)) 
				have_type = 1;
			else if (!strncasecmp(cp,"Time:",5)) 
				have_time = 1;
			else if (!strcmp(cp,"\n")) {
				headers();
				uflag = 0;
				continue;
			}
		}

		msg->data_size = tmp - msg->data;
		if ((newline = (tmp[-1] == '\n'))) {
			tmp[-1] = '\r';
			*tmp++ = '\n';
			*tmp = '\0';
			++(msg->data_size);
		}
	}

	if (!aflag) {
		struct gale_message *new = sign_message(user_id,msg);
		release_message(msg);
		msg = new;
	}
	if (recipient) {
		struct gale_message *new = encrypt_message(recipient,msg);
		release_message(msg);
		msg = new;
	}

	link_put(client->link,msg);
	while (1) {
		if (!gale_send(client)) break;
		if (rflag) gale_alert(GALE_ERROR,"transmission failed",0);
		gale_retry(client);
		if (!link_queue(client->link))
			link_put(client->link,msg);
	}
	release_message(msg);

	if (ttyin)
		printf("Message sent.\n");

	return 0;
}
