#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/utsname.h>

#include "gale/all.h"

extern char **environ;

char *rcprog = "gsubrc";
struct gale_client *client;
char *tty,*agent;

#ifndef NDEBUG
char **restart_argv;
#endif

int pflag = 1;

void *gale_malloc(int size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

struct gale_message *slip(const char *cat,const char *sign,const char *enc) {
	struct gale_message *msg;
	const char *from = getenv("GALE_FROM");
	const char *reply = getenv("GALE_REPLY_TO");
	int len = strlen(from) + strlen(reply) + strlen(agent);
	static int sequence = 0;

	msg = new_message();
	msg->category = gale_strdup(cat);
	msg->data = gale_malloc(128 + len);
	sprintf(msg->data,
		"From: %s\r\n"
		"Reply-To: %s\r\n"
	        "Time: %lu\r\n"
		"Agent: %s\r\n"
		"Sequence: %d\r\n"
	        "\r\n",from,reply,time(NULL),agent,sequence++);
	msg->data_size = strlen(msg->data);

	if (sign) {
		struct gale_message *new = sign_message(sign,msg);
		release_message(msg);
		msg = new;
	}
	if (enc) {
		struct gale_message *new = encrypt_message(enc,msg);
		release_message(msg);
		msg = new;
	}

	return msg;
}

void default_gsubrc(void) {
	char *tmp,*tmp2,buf[80],*cat = getenv("GALE_CATEGORY");
	char *nl = isatty(1) ? "\r\n" : "\n";
	int count = 0;

	tmp = cat;
	while ((tmp = strstr(tmp,"/ping"))) {
		tmp += 5;
		if (!*tmp || *tmp == ':') return;
	}

	printf("[%s]",cat);
	if ((tmp = getenv("HEADER_TIME"))) {
		time_t when = atoi(tmp);
		strftime(buf,sizeof(buf)," %m/%d %H:%M",localtime(&when));
		fputs(buf,stdout);
	}
	if ((tmp = getenv("HEADER_FROM"))) printf(" from %s",tmp);
	if ((tmp = getenv("HEADER_RECEIPT_TO"))) printf(" [rcpt]");
	fputs(nl,stdout);
	tmp = getenv("GALE_SIGNED"); tmp2 = getenv("GALE_ENCRYPTED");
	if (tmp || tmp2) {
		printf("Cryptography: ");
		if (tmp) printf("signed by <%s>",tmp);
		if (tmp && tmp2) printf(", ");
		if (tmp2) printf("encrypted for <%s>",tmp2);
		fputs(nl,stdout);
	}
	fputs(nl,stdout);
	while (fgets(buf,sizeof(buf),stdin)) {
		int len = strlen(buf);
		if (len && buf[len-1] == '\n') {
			buf[len-1] = '\0';
			fputs(buf,stdout);
			fputs(nl,stdout);
		} else
			fputs(buf,stdout);
		++count;
	}
	if (count) fputs(nl,stdout);
	fflush(stdout);
}

void send_message(char *body,char *end,int fd) {
	char *tmp;

	while (body != end) {
		tmp = memchr(body,'\r',end - body);
		if (!tmp) tmp = end;
		while (body != tmp) {
			int r = write(fd,body,tmp - body);
			if (r <= 0) {
				gale_alert(GALE_WARNING,"write",errno);
				return;
			}
			body += r;
		}
		if (tmp != end) {
			if (write(fd,"\n",1) != 1) {
				gale_alert(GALE_WARNING,"write",errno);
				return;
			}
			++tmp;
			if (tmp != end && *tmp == '\n') ++tmp;
		}
		body = tmp;
	}
}

void present_message(struct gale_message *msg) {
	int pfd[2];
	char *next,**envp = NULL,*key,*data,*end,*tmp,*decrypt = NULL;
	char *id_encrypted = NULL,*id_sign = NULL;
	int envp_global,envp_alloc,envp_len,first = 1;
	pid_t pid;

	envp_global = 0;
	for (envp = environ; *envp; ++envp) ++envp_global;
	envp_alloc = envp_global + 10;
	envp = gale_malloc(envp_alloc * sizeof(*envp));
	memcpy(envp,environ,envp_global * sizeof(*envp));
	envp_len = envp_global;

	next = gale_malloc(strlen(msg->category) + 15);
	sprintf(next,"GALE_CATEGORY=%s",msg->category);
	envp[envp_len++] = next;

	next = msg->data;
	end = msg->data + msg->data_size;
	while (parse_header(&next,&key,&data,end)) {
		if (first && !decrypt && !strcasecmp(key,"Encryption")) {
			decrypt = gale_malloc(end - next + DECRYPTION_PADDING);
			id_encrypted = decrypt_data(data,next,end,decrypt,&end);
			if (!id_encrypted) goto error;
			next = decrypt;
			tmp = gale_malloc(strlen(id_encrypted) + 16);
			sprintf(tmp,"GALE_ENCRYPTED=%s",id_encrypted);
			envp[envp_len++] = tmp;
			continue;
		}

		if (first && !strcasecmp(key,"Signature")) {
			id_sign = verify_data(data,next,end);
			if (id_sign) {
				tmp = gale_malloc(strlen(id_sign)+13);
				sprintf(tmp,"GALE_SIGNED=%s",id_sign);
				envp[envp_len++] = tmp;
			}
		}

		first = 0;

		if (!strcasecmp(key,"Receipt-To")) {
			const char *colon = data;
			while (colon && *colon && !strncmp(colon,"receipt/",8))
				colon = strchr(colon + 1,':');
			if (colon && *colon) {
				gale_alert(GALE_WARNING,
				           "invalid receipt header",0);
				continue;
			}
			if (pflag) {
				struct gale_message *rcpt;
				const char *sign = id_encrypted;
				if (!sign) sign = getenv("GALE_ID");
				rcpt = slip(data,sign,id_sign);
				link_put(client->link,rcpt);
				release_message(rcpt);
			}
		}

		for (tmp = key; *tmp; ++tmp)
			*tmp = isalnum(*tmp) ? toupper(*tmp) : '_';
		tmp = gale_malloc(strlen(key) + strlen(data) + 9);
		sprintf(tmp,"HEADER_%s=%s",key,data);

		if (envp_len == envp_alloc - 1) {
			char **tmp = envp;
			envp_alloc *= 2;
			envp = gale_malloc(envp_alloc * sizeof(*envp));
			memcpy(envp,tmp,envp_len * sizeof(*envp));
			gale_free(tmp);
		}

		envp[envp_len++] = tmp;
	}

#ifndef NDEBUG
	if (!strcmp(msg->category,"debug/restart") &&
	    id_sign && !strcmp(id_sign,"egnor@ofb.net")) {
		gale_alert(GALE_NOTICE,"Restarting from debug/restart.",0);
		execvp(restart_argv[0],restart_argv);
		gale_alert(GALE_WARNING,restart_argv[0],errno);
	}
#endif

	envp[envp_len] = NULL;

	if (pipe(pfd)) {
		gale_alert(GALE_WARNING,"pipe",errno);
		goto error;
	}

	pid = fork();
	if (!pid) {
		const char *rc;
		signal(SIGPIPE,SIG_DFL);
		environ = envp;
		close(client->socket);
		close(pfd[1]);
		dup2(pfd[0],0);
		if (pfd[0] != 0) close(pfd[0]);
		rc = dir_search(rcprog,1,dot_gale,sys_dir,NULL);
		if (rc) {
			execl(rc,rcprog,NULL);
			gale_alert(GALE_WARNING,rc,errno);
		}
		default_gsubrc();
		exit(0);
	}

	if (pid < 0) gale_alert(GALE_WARNING,"fork",errno);

	close(pfd[0]);
	send_message(next,end,pfd[1]);
	close(pfd[1]);

	if (pid > 0) waitpid(pid,NULL,0);

error:
	if (envp) {
		while (envp_global != envp_len) gale_free(envp[envp_global++]);
		gale_free(envp);
	}
	if (decrypt) gale_free(decrypt);
	if (id_encrypted) gale_free(id_encrypted);
	if (id_sign) gale_free(id_sign);
}

void notify(void) {
	struct gale_message *msg;
	const char *id = getenv("GALE_ID");
	char *tmp;

	tmp = gale_idtocat("notice",id,"login");
	msg = slip(tmp,id,NULL);
	link_put(client->link,msg);
	release_message(msg);
	gale_free(tmp);

	tmp = gale_idtocat("notice",id,"logout");
	msg = slip(tmp,id,NULL);
	link_will(client->link,msg);
	release_message(msg);
	gale_free(tmp);
}

void set_agent(void) {
	char *user = getenv("LOGNAME");
	const char *host = getenv("HOST");
	int len;

	len = strlen(GALE_VERSION) + strlen(user) + strlen(host);
	len += (tty ? strlen(tty) : 0) + 30;
	agent = gale_malloc(len);
	sprintf(agent,"gsub/%s %s@%s %s %d",
	        GALE_VERSION,user,host,tty ? tty : "none",(int) getpid());
}

void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gsub [-gkKnpr] [-P str] [-f rcprog] cat\n"
	"flags: -n          Do not fork (default if stdout redirected)\n"
	"       -k          Do not kill other gsub processes\n"
	"       -K          Kill other gsub processes and terminate\n"
	"       -r          Do not retry server connection\n"
	"       -f rcprog   Use rcprog (default gsubrc, then built-in)\n"
	"       -p          Suppress return-receipt processing altogether\n"
	"       -y          Disable login/logout notification\n"
	,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	int opt,do_retry = 1,do_notify = 1,do_fork = 0,do_kill = 0;
	char *serv;

	gale_init("gsub");
	if ((tty = ttyname(1))) {
		char *tmp = strrchr(tty,'/');
		if (tmp) tty = tmp + 1;
		do_fork = do_kill = 1;
	}

	while (EOF != (opt = getopt(argc,argv,"hgnkKf:rp:y"))) switch (opt) {
	case 'n': do_fork = 0; break;
	case 'k': do_kill = 0; break;
	case 'K': if (tty) gale_kill(tty,1); return 0;
	case 'f': rcprog = optarg; break;
	case 'r': do_retry = 0; break;
	case 'p': pflag = 0; break;
	case 'y': do_notify = 0; break;
	case 'h':
	case '?': usage();
	}

	if (optind < argc - 1) usage();

	if (optind == argc - 1)
		serv = argv[optind];
	else
		serv = getenv("GALE_SUBS");

	if (serv == NULL) 
		gale_alert(GALE_ERROR,"No subscriptions specified.",0);

	gale_keys();

#ifndef NDEBUG
	{
		char *tmp = gale_malloc(strlen(serv) + 8);
		restart_argv = argv;
		sprintf(tmp,"%s:debug/",serv);
		serv = tmp;
	}
#endif

	client = gale_open(serv,16,262144);
	if (!do_retry && gale_error(client))
		gale_alert(GALE_ERROR,"Could not connect to server.",0);

	if (do_fork) {
		gale_daemon(1);
		gale_kill(tty,do_kill);
	}

	signal(SIGPIPE,SIG_IGN);
	set_agent();

	if (do_notify) notify();
	do {
		while (!gale_send(client) && !gale_next(client)) {
			struct gale_message *msg;
			if (tty && !isatty(1)) return 0;
			if ((msg = link_get(client->link))) {
				present_message(msg);
				release_message(msg);
			}
		}
		if (do_retry) {
			gale_retry(client);
			if (do_notify) notify();
		}
	} while (do_retry);

	gale_alert(GALE_ERROR,"connection lost",0);
	return 0;
}
