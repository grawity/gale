#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "gale/all.h"

void *gale_malloc(int size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

struct gale_message *msg;
int alloc = 0;
int have_from = 0,have_time = 0,have_type = 0,have_replyto = 0;
const char *pflag = NULL;

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
	if (!have_from) {
		char *tmp = getenv("GALE_FROM");
		reserve(20 + strlen(tmp));
		sprintf(msg->data + msg->data_size,"From: %s\r\n",tmp);
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!have_time) {
		reserve(20);
		sprintf(msg->data + msg->data_size,"Time: %lu\r\n",time(NULL));
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!have_replyto) {
		char *tmp = getenv("GALE_REPLY_TO");
		reserve(20 + strlen(tmp));
		sprintf(msg->data + msg->data_size,"Reply-To: %s\r\n",tmp);
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
	fprintf(stderr,
		"%s\n"
		"usage: gsend [-suU] [-et id] [-p cat] cat\n"
		"flags: -s          Sign message with your private key\n"
		"       -e id       Encrypt message with <id>'s public key\n"
		"       -t id       Default category without encryption\n"
		"       -r          Do not retry server connection\n"
		"       -p cat      Request a return receipt\n"
		"       -u          Expect user-supplied headers\n"
		"       -U          Ditto, and don't supply default headers\n"
		"With -e or -t, cat defaults to \"user/domain/username\".\n"
		,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct gale_client *client;
	int arg,uflag = 0,sflag = 0,rflag = 0;
	int ttyin = isatty(0),newline = 1;
	char *cp,*tmp,*eflag = NULL,*tflag = NULL;

	gale_init("gsend");

	while ((arg = getopt(argc,argv,"hse:t:p:ruU")) != EOF) 
	switch (arg) {
	case 's': sflag++; break;
	case 'e': eflag = optarg; break;
	case 't': tflag = optarg; break;
	case 'p': pflag = optarg; break;
	case 'r': rflag = 1; break;
	case 'u': uflag = 1; break;
	case 'U': uflag = 2; break;
	case 'h':
	case '?': usage();
	}

	msg = new_message();

	if (optind == argc && eflag)
		msg->category = gale_idtocat("user",eflag,"");
	else if (optind == argc && tflag)
		msg->category = gale_idtocat("user",tflag,"");
	else if (optind != argc - 1)
		usage();
	else
		msg->category = gale_strdup(argv[optind]);

	if (sflag || eflag) gale_keys();

	client = gale_open(NULL,1,262144);

	while (gale_error(client)) {
		if (rflag) gale_alert(GALE_ERROR,"could not contact server",0);
		gale_retry(client);
	}

	if (ttyin)
		printf("Enter your message.  End it with an EOF or a dot.\n");

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
			else if (!strncasecmp(cp,"Reply-To:",9))
				have_replyto = 1;
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

	if (sflag) {
		struct gale_message *new = sign_message(NULL,msg);
		release_message(msg);
		msg = new;
	}
	if (eflag) {
		struct gale_message *new = encrypt_message(eflag,msg);
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
