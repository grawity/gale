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

char *dotfile,*rcprog = NULL;
struct gale_client *client;
char *tty;

void *gale_malloc(int size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

void exitfunc(void) {
	if (dotfile) unlink(dotfile);
}

void hupfunc(int sig) {
	exitfunc();
	exit(0);
}

void default_gsubrc(void) {
	char *tmp,*tmp2,buf[80];
	char *nl = isatty(1) ? "\r\n" : "\n";

	printf("[%s]",getenv("CATEGORY"));
	if ((tmp = getenv("GALE_TIME"))) {
		time_t when = atoi(tmp);
		strftime(buf,sizeof(buf)," %m/%d %H:%M",localtime(&when));
		fputs(buf,stdout);
	}
	if ((tmp = getenv("GALE_FROM"))) printf(" from %s",tmp);
	fputs(nl,stdout);
	tmp = getenv("SIGNED"); tmp2 = getenv("ENCRYPTED");
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
	}
	fputs(nl,stdout);
}

void send_message(char *body,char *end,int fd) {
	char *tmp;

	while (body != end) {
		tmp = memchr(body,'\r',end - body);
		if (!tmp) tmp = end;
		while (body != tmp) {
			int r = write(fd,body,tmp - body);
			if (r <= 0) {
				perror("write");
				return;
			}
			body += r;
		}
		if (tmp != end) {
			if (write(fd,"\n",1) != 1) {
				perror("write");
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
	char *next,**envp = NULL,*key,*data,*end,*decrypt = NULL;
	int envp_global,envp_alloc,envp_len,first = 1;
	pid_t pid;

	envp_global = 0;
	for (envp = environ; *envp; ++envp) ++envp_global;
	envp_alloc = envp_global + 10;
	envp = gale_malloc(envp_alloc * sizeof(*envp));
	memcpy(envp,environ,envp_global * sizeof(*envp));
	envp_len = envp_global;

	next = gale_malloc(strlen(msg->category) + 10);
	sprintf(next,"CATEGORY=%s",msg->category);
	envp[envp_len++] = next;

	next = msg->data;
	end = msg->data + msg->data_size;
	while (parse_header(&next,&key,&data,end)) {
		char *id,*tmp;

		if (first) {
			if (!decrypt && !strcasecmp(key,"Encryption")) {
				decrypt = gale_malloc(end - next
					+ DECRYPTION_PADDING);
				id = decrypt_message(data,next,end,
					decrypt,&end);
				if (!id) goto error;
				next = decrypt;
				tmp = gale_malloc(strlen(id) + 11);
				sprintf(tmp,"ENCRYPTED=%s",id);
				gale_free(id);
				envp[envp_len++] = tmp;
				continue;
			}

			if (!strcasecmp(key,"Signature")) {
				id = verify_signature(data,next,end);
				if (id) {
					tmp = gale_malloc(strlen(id)+8);
					sprintf(tmp,"SIGNED=%s",id);
					gale_free(id);
					envp[envp_len++] = tmp;
				}
			}

			first = 0;
		}

		for (tmp = key; *tmp; ++tmp)
			*tmp = isalnum(*tmp) ? toupper(*tmp) : '_';
		tmp = gale_malloc(strlen(key) + strlen(data) + 7);
		sprintf(tmp,"GALE_%s=%s",key,data);

		if (envp_len == envp_alloc - 1) {
			char **tmp = envp;
			envp_alloc *= 2;
			envp = gale_malloc(envp_alloc * sizeof(*envp));
			memcpy(envp,tmp,envp_len * sizeof(*envp));
			gale_free(tmp);
		}

		envp[envp_len++] = tmp;
	}

	envp[envp_len] = NULL;

	if (pipe(pfd)) {
		perror("pipe");
		goto error;
	}

	pid = fork();
	if (!pid) {
		signal(SIGPIPE,SIG_DFL);
		dotfile = NULL;
		environ = envp;
		close(client->socket);
		close(pfd[1]);
		dup2(pfd[0],0);
		if (pfd[0] != 0) close(pfd[0]);
		if (rcprog) {
			execl(rcprog,rcprog,NULL);
			perror(rcprog);
		} else {
			execl("gsubrc","gsubrc",NULL);
			if (errno != ENOENT) perror("gsubrc");
		}
		default_gsubrc();
		exit(0);
	}

	if (pid < 0) perror("fork");

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
}

void startup(int kflag) {
	char *dir;
	struct utsname un;
	DIR *pdir;
	struct dirent *de;
	int len,fd;

	if (!tty) return;
	if ((dir = strrchr(tty,'/'))) tty = dir + 1;
	if (uname(&un)) {
		perror("uname");
		exit(1);
	}
	gale_chdir();

	len = strlen(un.nodename) + strlen(tty) + 7;
	dotfile = gale_malloc(len + 15);
	sprintf(dotfile,"gsub.%s.%s.",un.nodename,tty);

	if (!kflag) {
		pdir = opendir(".");
		if (pdir == NULL) {
			perror("opendir");
			exit(1);
		}
		while ((de = readdir(pdir)))
			if (!strncmp(de->d_name,dotfile,len)) {
				kill(atoi(de->d_name + len),SIGHUP);
				unlink(de->d_name);
			}
		closedir(pdir);
	}

	if (fork()) exit(0);

	signal(SIGHUP,hupfunc);
	signal(SIGINT,SIG_IGN);
	signal(SIGQUIT,SIG_IGN);
	signal(SIGTTOU,SIG_IGN);
	signal(SIGPIPE,SIG_IGN);
	signal(SIGTERM,hupfunc);
	atexit(exitfunc);
	sprintf(dotfile,"%s%d",dotfile,(int) getpid());
	if ((fd = creat(dotfile,0666)) >= 0)
		close(fd);
}

void usage(void) {
	fprintf(stderr,
	"usage: gsub [-n] [-f rcprog] cat[:cat...][@[host][:[port]]]\n"
	"flags: -n          Do not fork (default unless stdout is a tty)\n"
	"       -k          Do not kill other gsubs on this tty (ditto)\n"
	"       -f rcprog   Use rcprog (default gsubrc, then built-in)\n"
	"       -r          Terminate if connection fails (do not retry)\n");
	exit(1);
}

int main(int argc,char *argv[]) {
	int opt,fflag = 0,kflag = 0,rflag = 1;
	struct gale_message *msg;

	tty = ttyname(1);

	while (EOF != (opt = getopt(argc,argv,"nkf:r"))) switch (opt) {
	case 'k': ++kflag; break;
	case 'n': ++fflag; break;
	case 'f': rcprog = optarg; break;
	case 'r': rflag = 0; break;
	case '?': usage();
	}

	if (optind != argc - 1) usage();

	gale_keys();

	if (!(client = gale_open(argv[optind],0,0))) exit(1);
	if (!rflag && gale_error(client)) {
		fprintf(stderr,"error: could not connect to gale server\n");
		exit(1);
	}

	if (!fflag) startup(kflag);

	do {
		while (!gale_next(client)) {
			if (tty && !isatty(1)) return 0;
			if ((msg = link_get(client->link))) {
				present_message(msg);
				release_message(msg);
			}
		}
		if (rflag) gale_retry(client);
	} while (rflag);

	fprintf(stderr,"gsub: connection lost\r\n");
	return 0;
}
