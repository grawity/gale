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
int alloc = 0;
int have_from = 0,have_time = 0,have_type = 0;

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
	struct passwd *pwd;
	const char *id;
	gale_id(&id);
	if (!have_type) {
		reserve(40);
		sprintf(msg->data + msg->data_size,
			"Content-type: text/plain\r\n");
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	if (!have_from && (pwd = getpwuid(getuid()))) {
		char *name;
		name = strtok(pwd->pw_gecos,",");
		if (!*name) name = pwd->pw_name;
		if (*name) {
			reserve(20 + strlen(name));
			sprintf(msg->data + msg->data_size,
			        "From: %s <%s>\r\n",name,id);
			msg->data_size += strlen(msg->data + msg->data_size);
		}
	}
	if (!have_time) {
		reserve(20);
		sprintf(msg->data + msg->data_size,"Time: %lu\r\n",time(NULL));
		msg->data_size += strlen(msg->data + msg->data_size);
	}
	reserve(2);
	msg->data[msg->data_size++] = '\r';
	msg->data[msg->data_size++] = '\n';
}

void usage(void) {
	fprintf(stderr,
		"usage: gsend [-suU] [-e id] cat[:cat]*[@[host][:[port]]]\n"
		"flags: -s       Sign message with your private key\n"
		"       -e id    Encrypt message with <id>'s public key\n"
		"       -r       Do not retry server connection\n"
		"       -u       Expect user-supplied headers\n"
		"       -U       Ditto, and don't supply default headers\n"
		);
	exit(1);
}

int main(int argc,char *argv[]) {
	struct gale_client *client;
	int len,arg,uflag = 0,sflag = 0,rflag = 0;
	int ttyin = isatty(0),newline = 1;
	char *cp,*tmp,*server,*eflag = NULL;

	gale_init("gsend");

	while ((arg = getopt(argc,argv,"se:ruU")) != EOF) 
	switch (arg) {
	case 's': sflag++; break;
	case 'e': eflag = optarg; break;
	case 'r': rflag = 1; break;
	case 'u': uflag = 1; break;
	case 'U': uflag = 2; break;
	case '?': usage();
	}

	if (optind != argc - 1) usage();

	if ((server = strrchr(argv[optind],'@')))
		*server = '%';
	else
		server = argv[optind] + strlen(argv[optind]);

	if (sflag || eflag) gale_keys();

	client = gale_open(server,1,262144);
	if (!client) exit(1);

	while (gale_error(client)) {
		if (rflag) {
			fprintf(stderr,"error: could not contact server\n");
			exit(1);
		}
		gale_retry(client);
	}

	msg = new_message();
	msg->category = gale_strndup(argv[optind],server - argv[optind]);

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
			if (!strncmp(cp,"From: ",6)) 
				have_from = 1;
			else if (!strncmp(cp,"Content-type: ",14)) 
				have_type = 1;
			else if (!strncmp(cp,"Time: ",6)) 
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

	if (sflag) {
		tmp = sign_message(msg->data,msg->data + msg->data_size);
		cp = gale_malloc((len = strlen(tmp)) + msg->data_size + 13);
		sprintf(cp,"Signature: %s\r\n",tmp);
		memcpy(cp + len + 13,msg->data,msg->data_size);
		gale_free(tmp); gale_free(msg->data);
		msg->data = cp;
		msg->data_size = len + 13 + msg->data_size;
	}

	if (eflag) {
		char *crypt = gale_malloc(msg->data_size + ENCRYPTION_PADDING);
		char *cend;
		char *hdr = encrypt_message(eflag,msg->data,msg->data 
		                            + msg->data_size,crypt,&cend);
		int len = strlen(hdr) + 14;
		tmp = gale_malloc(len + cend - crypt);
		sprintf(tmp,"Encryption: %s\r\n",hdr);
		memcpy(tmp + len,crypt,cend - crypt);
		gale_free(msg->data); gale_free(hdr);
		msg->data = tmp;
		msg->data_size = len + cend - crypt;
	}

	link_put(client->link,msg);
	while (1) {
		if (!gale_send(client)) break;
		if (rflag) {
			fprintf(stderr,"error: transmission failed\n");
			exit(1);
		}
		gale_retry(client);
		if (!link_queue(client->link))
			link_put(client->link,msg);
	}
	release_message(msg);

	if (ttyin)
		printf("Message sent.\n");

	return 0;
}
