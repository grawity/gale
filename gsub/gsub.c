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

#include <curses.h>
#include <term.h>

#include "gale/all.h"

extern char **environ;

char *rcprog = "gsubrc";
struct gale_client *client;
char *tty,*agent;

int do_ping = 1;
int do_termcap = 0;

void *gale_malloc(size_t size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

struct gale_message *slip(const char *cat,struct gale_id *sign) {
	struct gale_message *msg;
	int len = strlen(agent);
	static int sequence = 0;

	if (user_id->comment) len += strlen(user_id->comment);

	msg = new_message();
	msg->category = gale_strdup(cat);
	msg->data = gale_malloc(128 + len);
	if (user_id->comment)
		sprintf(msg->data,
			"From: %s\r\n"
			"Time: %lu\r\n"
			"Agent: %s\r\n"
			"Sequence: %d\r\n"
			"\r\n",user_id->comment,time(NULL),agent,sequence++);
	else
		sprintf(msg->data,
			"Time: %lu\r\n"
			"Agent: %s\r\n"
			"Sequence: %d\r\n"
			"\r\n",time(NULL),agent,sequence++);

	msg->data_size = strlen(msg->data);

	if (sign) {
		struct gale_message *new = sign_message(sign,msg);
		release_message(msg);
		msg = new;
	}

	return msg;
}

void tmode(char id[2]) {
	char *cap;
	if (do_termcap && (cap = tgetstr(id,NULL))) 
		tputs(cap,1,
#if defined(hpux) || (defined(__sun) && defined(__SVR4))
			(int(*)(char))
#endif
			putchar);
}

void print_id(const char *id,const char *dfl) {
	putchar(' ');
	putchar(id ? '<' : '*');
	tmode("md");
	fputs(id ? id : dfl,stdout);
	tmode("me");
	putchar(id ? '>' : '*');
}

void default_gsubrc(void) {
	char *tmp,buf[80],*cat = getenv("GALE_CATEGORY");
	char *nl = tty ? "\r\n" : "\n";
	int count = 0;

	tmp = cat;
	while ((tmp = strstr(tmp,"/ping"))) {
		tmp += 5;
		if (!*tmp || *tmp == ':') return;
	}

	putchar('[');
	tmode("md");
	fputs(cat,stdout);
	tmode("me");
	putchar(']');
	if ((tmp = getenv("HEADER_TIME"))) {
		time_t when = atoi(tmp);
		strftime(buf,sizeof(buf)," %m/%d %H:%M",localtime(&when));
		fputs(buf,stdout);
	}
	if (getenv("HEADER_RECEIPT_TO")) 
		printf(" [rcpt]");
	fputs(nl,stdout);

	{
		char *from_comment = getenv("HEADER_FROM");
		char *from_id = getenv("GALE_SIGNED");
		char *to_comment = getenv("HEADER_TO");
		char *to_id = getenv("GALE_ENCRYPTED");

		fputs("From",stdout);
		if (from_comment || from_id) {
			print_id(from_id,"unverified");
			if (from_comment) printf(" (%s)",from_comment);
		} else
			print_id(NULL,"anonymous");

		fputs(" to",stdout);
		print_id(to_id,"everyone");
		if (to_comment) printf(" (%s)",to_comment);

		putchar(':');
		fputs(nl,stdout);
	}

	fputs(nl,stdout);
	while (fgets(buf,sizeof(buf),stdin)) {
		char *ptr,*end = buf + strlen(buf);
		for (ptr = buf; ptr < end; ++ptr) {
			if (isprint(*ptr) || *ptr == '\t')
				putchar(*ptr);
			else if (*ptr == '\n')
				fputs(nl,stdout);
			else {
				tmode("mr");
				if (*ptr < 32)
					printf("^%c",*ptr + 64);
				else
					printf("[0x%X]",*ptr);
				tmode("me");
			}
		}
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
	struct gale_id *id_encrypted = NULL,*id_sign = NULL;
	struct gale_message *rcpt = NULL;
	int envp_global,envp_alloc,envp_len,first = 1,status;
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
			tmp = gale_malloc(strlen(id_encrypted->name) + 16);
			sprintf(tmp,"GALE_ENCRYPTED=%s",id_encrypted->name);
			envp[envp_len++] = tmp;
			continue;
		}

		if (first && !strcasecmp(key,"Signature")) {
			id_sign = verify_data(data,next,end);
			if (id_sign) {
				tmp = gale_malloc(strlen(id_sign->name)+13);
				sprintf(tmp,"GALE_SIGNED=%s",id_sign->name);
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
			if (do_ping) {
				struct gale_id *sign = id_encrypted;
				if (!sign) sign = user_id;
				if (rcpt) release_message(rcpt);
				rcpt = slip(data,sign);
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
	    id_sign && !strcmp(id_sign->name,"egnor@ofb.net")) {
		gale_alert(GALE_NOTICE,"Restarting from debug/restart.",0);
		gale_restart();
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
		environ = envp;
		close(client->socket);
		close(pfd[1]);
		dup2(pfd[0],0);
		if (pfd[0] != 0) close(pfd[0]);
		rc = dir_search(rcprog,1,dot_gale,sys_dir,NULL);
		if (rc) {
			execl(rc,rcprog,NULL);
			gale_alert(GALE_WARNING,rc,errno);
			exit(1);
		}
		default_gsubrc();
		exit(0);
	}

	if (pid < 0) gale_alert(GALE_WARNING,"fork",errno);

	close(pfd[0]);
	send_message(next,end,pfd[1]);
	close(pfd[1]);

	status = -1;
	if (pid > 0) {
		waitpid(pid,&status,0);
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			status = 0;
		else
			status = -1;
	}

	if (rcpt && !status) {
		link_put(client->link,rcpt);
		release_message(rcpt);
	}

error:
	if (envp) {
		while (envp_global != envp_len) gale_free(envp[envp_global++]);
		gale_free(envp);
	}
	if (decrypt) gale_free(decrypt);
	if (id_encrypted) free_id(id_encrypted);
	if (id_sign) free_id(id_sign);
}

void notify(void) {
	struct gale_message *msg;
	char *tmp;

	tmp = id_category(user_id,"notice","login");
	msg = slip(tmp,user_id);
	gale_free(tmp);
	link_put(client->link,msg);
	release_message(msg);

	tmp = id_category(user_id,"notice","logout");
	msg = slip(tmp,user_id);
	gale_free(tmp);
	link_will(client->link,msg);
	release_message(msg);
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

int main(int argc,char **argv) {
	int opt,do_retry = 1,do_notify = 1,do_fork = 0,do_kill = 0;
	char *serv;

	gale_init("gsub",argc,argv);
	if ((tty = ttyname(1))) {
		char *term = getenv("TERM");
		char *tmp = strrchr(tty,'/');
		if (tmp) tty = tmp + 1;
		do_fork = do_kill = 1;
		if (term && 1 == tgetent(NULL,term)) do_termcap = 1;
	}

	while (EOF != (opt = getopt(argc,argv,"hgnkKf:rpy"))) switch (opt) {
	case 'n': do_fork = 0; break;
	case 'k': do_kill = 0; break;
	case 'K': if (tty) gale_kill(tty,1); return 0;
	case 'f': rcprog = optarg; break;
	case 'r': do_retry = 0; break;
	case 'p': do_ping = 0; break;
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
		sprintf(tmp,"%s:debug/",serv);
		serv = tmp;
	}
#endif

	client = gale_open(serv);
	if (!do_retry && gale_error(client))
		gale_alert(GALE_ERROR,"Could not connect to server.",0);

	if (do_fork) {
		gale_daemon(1);
		gale_kill(tty,do_kill);
	}

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
