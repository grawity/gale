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

int pflag = 1,kflag = 0;

void *gale_malloc(int size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

void exitfunc(void) {
	if (dotfile) unlink(dir_file(dot_gale,dotfile));
}

void return_receipt(const char *cat,const char *rcpt,
                    const char *sign,const char *encrypt) 
{
	struct gale_message *msg;
	const char *from = getenv("GALE_FROM");
	const char *reply = getenv("GALE_REPLY_TO");
	int len = strlen(rcpt) + strlen(from) + strlen(reply);

	msg = new_message();
	msg->category = gale_strdup(cat);
	msg->data = gale_malloc(128 + len);
	sprintf(msg->data,
	        "Receipt-from: %s\r\n"
		"From: %s\r\n"
		"Reply-To: %s\r\n"
	        "Time: %lu\r\n"
	        "\r\n",rcpt,from,reply,time(NULL));
	msg->data_size = strlen(msg->data);

	if (sign) sign_message(sign,msg);
	if (encrypt) encrypt_message(encrypt,msg);

	link_put(client->link,msg);
	release_message(msg);
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
	char *id_encrypted = NULL,*id_signed = NULL;
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
		if (first) {
			if (!decrypt && !strcasecmp(key,"Encryption")) {
				decrypt = gale_malloc(end - next
					+ DECRYPTION_PADDING);
				id_encrypted = decrypt_data(data,next,end,
					decrypt,&end);
				if (!id_encrypted) goto error;
				next = decrypt;
				tmp = gale_malloc(strlen(id_encrypted) + 16);
				sprintf(tmp,"GALE_ENCRYPTED=%s",id_encrypted);
				envp[envp_len++] = tmp;
				continue;
			}

			if (!strcasecmp(key,"Signature")) {
				id_signed = verify_data(data,next,end);
				if (id_signed) {
					tmp = gale_malloc(strlen(id_signed)+13);
					sprintf(tmp,"GALE_SIGNED=%s",id_signed);
					envp[envp_len++] = tmp;
				}
			}

			first = 0;
		}

		if (!strcasecmp(key,"Receipt-to")) {
			const char *colon = data;
			while (colon && *colon && !strncmp(colon,"receipt/",8))
				colon = strchr(colon + 1,':');
			if (colon && *colon) {
				gale_alert(GALE_WARNING,
				           "invalid receipt header",0);
				continue;
			}
			if (pflag)
				return_receipt(data,msg->category,
				               id_encrypted,id_signed);
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

	envp[envp_len] = NULL;

	if (pipe(pfd)) {
		gale_alert(GALE_WARNING,"pipe",errno);
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
			gale_alert(GALE_WARNING,rcprog,errno);
		} else {
			execl(dir_file(dot_gale,"gsubrc"),"gsubrc",NULL);
			if (errno != ENOENT)
				gale_alert(GALE_WARNING,"gsubrc",errno);
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
	if (id_signed) gale_free(id_signed);
}

void startup(void) {
	char *dir;
	struct utsname un;
	DIR *pdir;
	struct dirent *de;
	int len,fd;

	if (!tty) return;
	if ((dir = strrchr(tty,'/'))) tty = dir + 1;
	if (uname(&un)) {
		gale_alert(GALE_WARNING,"uname",errno);
		exit(1);
	}

	len = strlen(un.nodename) + strlen(tty) + 7;
	dotfile = gale_malloc(len + 15);
	sprintf(dotfile,"gsub.%s.%s.",un.nodename,tty);

	if (!kflag) {
		pdir = opendir(dir_file(dot_gale,"."));
		if (pdir == NULL) {
			gale_alert(GALE_WARNING,"opendir",errno);
			exit(1);
		}
		while ((de = readdir(pdir)))
			if (!strncmp(de->d_name,dotfile,len)) {
				kill(atoi(de->d_name + len),SIGHUP);
				unlink(dir_file(dot_gale,de->d_name));
			}
		closedir(pdir);
	}

	gale_cleanup(exitfunc);
	signal(SIGPIPE,SIG_IGN);

	gale_daemon(1);

	sprintf(dotfile,"%s%d",dotfile,(int) getpid());
	if ((fd = creat(dir_file(dot_gale,dotfile),0666)) >= 0)
		close(fd);
}

void usage(void) {
	fprintf(stderr,
	"%s\n"
	"usage: gsub [-gkKnpr] [-P str] [-f rcprog] cat@server\n"
	"flags: -n          Do not fork (default unless stdout is a tty)\n"
	"       -k          Do not kill other gsubs on this tty (ditto)\n"
	"       -K          Kill other gsubs on the tty, then terminate\n"
	"       -f rcprog   Use rcprog (default gsubrc, then built-in)\n"
	"       -r          Terminate if connection fails (do not retry)\n"
	"       -p          Suppress return-receipt processing altogether\n"
	,GALE_BANNER);
	exit(1);
}

int main(int argc,char *argv[]) {
	int opt,fflag = 0,rflag = 1,Kflag = 0;
	struct gale_message *msg;

	gale_init("gsub");
	tty = ttyname(1);

	while (EOF != (opt = getopt(argc,argv,"hgnkKf:rp:"))) switch (opt) {
	case 'k': ++kflag; break;
	case 'K': ++Kflag; break;
	case 'n': ++fflag; break;
	case 'f': rcprog = optarg; break;
	case 'r': rflag = 0; break;
	case 'p': pflag = 0; break;
	case 'h':
	case '?': usage();
	}

	if (Kflag) {
		if (optind != argc) usage();
	} else {
		char *serv;

		if (optind < argc - 1) usage();

		serv = (optind == argc - 1) ? argv[optind] : NULL;
		if (!serv) {
			char *subs = getenv("GALE_SUBS");
			serv = gale_malloc(strlen(subs) + 2);
			sprintf(serv,"%s@",subs);
		}

		gale_keys();

		if (!(client = gale_open(serv,16,262144))) exit(1);
		if (!rflag && gale_error(client))
			gale_alert(GALE_ERROR,"could not connect to server",0);
	}

	if (!fflag) startup();

	if (!Kflag)
	{
		do {
			while (!gale_send(client) && !gale_next(client)) {
				if (tty && !isatty(1)) return 0;
				if ((msg = link_get(client->link))) {
					present_message(msg);
					release_message(msg);
				}
			}
			if (rflag) gale_retry(client);
		} while (rflag);

		gale_alert(GALE_ERROR,"connection lost",0);
	}

	return 0;
}
