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

int pflag = 1,kflag = 0,gflag = 1;
const char *Pflag = NULL;

void *gale_malloc(int size) { return malloc(size); }
void gale_free(void *ptr) { free(ptr); }

void exitfunc(void) {
	if (dotfile) unlink(dir_file(dot_gale,dotfile));
}

void return_receipt(const char *cat,const char *from,
                    const char *sign,const char *encrypt) 
{
	struct gale_message *msg;

	msg = new_message();
	msg->category = gale_strdup(cat);
	msg->data = gale_malloc(128 + strlen(from) + strlen(Pflag));
	sprintf(msg->data,
	        "Receipt-from: %s\r\n"
		"From: %s\r\n"
	        "Time: %lu\r\n"
	        "\r\n",from,Pflag,time(NULL));
	msg->data_size = strlen(msg->data);

	if (sign) {
		char *tmp = sign_message(sign,msg->data,
		                         msg->data + msg->data_size);
		int len = strlen(tmp);
		char *cp = gale_malloc(len + msg->data_size + 13);
		sprintf(cp,"Signature: %s\r\n",tmp);
		memcpy(cp + len + 13,msg->data,msg->data_size);
		gale_free(tmp); gale_free(msg->data);
		msg->data = cp;
		msg->data_size = len + 13 + msg->data_size;
	}

	if (encrypt) {
                char *crypt = gale_malloc(msg->data_size + ENCRYPTION_PADDING);
                char *cend;
                char *hdr = encrypt_message(encrypt,msg->data,msg->data
                                            + msg->data_size,crypt,&cend);
                int len = strlen(hdr) + 14;
                char *tmp = gale_malloc(len + cend - crypt);
                sprintf(tmp,"Encryption: %s\r\n",hdr);
                memcpy(tmp + len,crypt,cend - crypt);
                gale_free(msg->data); gale_free(hdr);
                msg->data = tmp;
                msg->data_size = len + cend - crypt;
	}

	link_put(client->link,msg);
	release_message(msg);
}

void default_gsubrc(void) {
	char *tmp,*tmp2,buf[80];
	char *nl = isatty(1) ? "\r\n" : "\n";
	int count = 0;

	printf("[%s]",getenv("GALE_CATEGORY"));
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
	char *id_encrypted = NULL,*id_signed = NULL;
	int envp_global,envp_alloc,envp_len,first = 1;
	pid_t pid;

	if (gflag) {
		end = msg->category;
		while ((end = strstr(end,"/ping"))) {
			end += 5;
			if (!*end || *end == ':') return;
		}
	}

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
		char *tmp;

		if (first) {
			if (!decrypt && !strcasecmp(key,"Encryption")) {
				decrypt = gale_malloc(end - next
					+ DECRYPTION_PADDING);
				id_encrypted = decrypt_message(data,next,end,
					decrypt,&end);
				if (!id_encrypted) goto error;
				next = decrypt;
				tmp = gale_malloc(strlen(id_encrypted) + 16);
				sprintf(tmp,"GALE_ENCRYPTED=%s",id_encrypted);
				envp[envp_len++] = tmp;
				continue;
			}

			if (!strcasecmp(key,"Signature")) {
				id_signed = verify_signature(data,next,end);
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
			execl(dir_file(dot_gale,"gsubrc"),"gsubrc",NULL);
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
		perror("uname");
		exit(1);
	}

	len = strlen(un.nodename) + strlen(tty) + 7;
	dotfile = gale_malloc(len + 15);
	sprintf(dotfile,"gsub.%s.%s.",un.nodename,tty);

	if (!kflag) {
		pdir = opendir(dir_file(dot_gale,"."));
		if (pdir == NULL) {
			perror("opendir");
			exit(1);
		}
		while ((de = readdir(pdir)))
			if (!strncmp(de->d_name,dotfile,len)) {
				kill(atoi(de->d_name + len),SIGHUP);
				unlink(dir_file(dot_gale,de->d_name));
			}
		closedir(pdir);
	}

	if (!Pflag) {
		char *tmp;
		const char *id;
		gale_id(&id);
		tmp = gale_malloc(strlen(id) + 3);
		sprintf(tmp,"<%s>",id);
		Pflag = tmp;
	}

	if (fork()) exit(0);

	gale_cleanup(exitfunc);
	signal(SIGINT,SIG_IGN);
	signal(SIGQUIT,SIG_IGN);
	signal(SIGTTOU,SIG_IGN);
	signal(SIGPIPE,SIG_IGN);
	sprintf(dotfile,"%s%d",dotfile,(int) getpid());
	if ((fd = creat(dir_file(dot_gale,dotfile),0666)) >= 0)
		close(fd);
}

void usage(void) {
	fprintf(stderr,
	"usage: gsub [-gknpr] [-P str] [-f rcprog] cat@server\n"
	"flags: -n          Do not fork (default unless stdout is a tty)\n"
	"       -k          Do not kill other gsubs on this tty (ditto)\n"
	"       -f rcprog   Use rcprog (default gsubrc, then built-in)\n"
	"       -r          Terminate if connection fails (do not retry)\n"
	"       -g          Do not ignore messages to /ping\n"
	"       -P str      Use \"str\" for return-receipt ID\n"
	"       -p          Suppress return-receipt processing altogether\n");
	exit(1);
}

int main(int argc,char *argv[]) {
	int opt,fflag = 0,rflag = 1;
	struct gale_message *msg;

	gale_init("gsub");
	tty = ttyname(1);

	while (EOF != (opt = getopt(argc,argv,"gnkf:rpP:"))) switch (opt) {
	case 'k': ++kflag; break;
	case 'n': ++fflag; break;
	case 'f': rcprog = optarg; break;
	case 'g': gflag = 0; break;
	case 'r': rflag = 0; break;
	case 'p': pflag = 0; break;
	case 'P': Pflag = optarg; break;
	case '?': usage();
	}

	if (optind != argc - 1) usage();

	gale_keys();

	if (!(client = gale_open(argv[optind],16,262144))) exit(1);
	if (!rflag && gale_error(client)) {
		fprintf(stderr,"error: could not connect to gale server\n");
		exit(1);
	}

	if (!fflag) startup();

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

	fprintf(stderr,"gsub: connection lost\r\n");
	return 0;
}
