#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include "common.h"
#include "random.h"
#include "file.h"

#define HASH_SIZE 16

void _ga_random(struct gale_data buf) {
	static int init = 0;
	static struct {
		int fd;
		struct gale_text filename;
		struct stat st;
		byte stuff[HASH_SIZE];
		struct timeval tv;
		struct timezone tz;
		pid_t pid;
		MD5_CTX md5;
	} r;

	if (!init) {
		MD5Init(&r.md5);
		r.pid = getpid();
		r.filename = dir_file(gale_global->dot_auth,G_("random"));

		r.fd = open("/dev/urandom",O_RDONLY);
		if (r.fd < 0) {
			r.fd = _ga_read_file(r.filename);
			if (r.fd >= 0 
			&&  (fstat(r.fd,&r.st) || (r.st.st_mode & 077))) {
				gale_alert(GALE_WARNING,
				           "insecure \".gale/auth/random\"",0);
				close(r.fd);
				r.fd = -1;
			}
		}

		if (r.fd < 0 || 
		    read(r.fd,r.stuff,HASH_SIZE) != HASH_SIZE)
			gale_alert(GALE_WARNING,
		                   "good random numbers unavailable",0);
		if (r.fd >= 0) close(r.fd);
	}

	gettimeofday(&r.tv,&r.tz);
	MD5Update(&r.md5,(byte *) &r,sizeof(r));
	MD5Final(r.stuff,&r.md5);

	do {
		if (buf.l > HASH_SIZE) {
			memcpy(buf.p,r.stuff,HASH_SIZE);
			buf.p += HASH_SIZE; buf.l -= HASH_SIZE;
		} else {
			memcpy(buf.p,r.stuff,buf.l);
			buf.p += buf.l; buf.l -= buf.l;
		}
		gettimeofday(&r.tv,&r.tz);
		MD5Update(&r.md5,(byte *) &r,sizeof(r));
		MD5Final(r.stuff,&r.md5);
	} while (buf.l > 0);

	if (!init) {
		struct gale_data data;
		data.p = r.stuff;
		data.l = HASH_SIZE;
		init = 1;
		_ga_save_file(null_text,r.filename,0600,data,NULL);
	}
}

R_RANDOM_STRUCT *_ga_rrand(void) {
	static R_RANDOM_STRUCT random;
	static int init = 0;
	unsigned int needed;

	if (!init) {
		R_RandomInit(&random);

		while (R_GetRandomBytesNeeded(&needed,&random),needed) {
			byte buf[128];
			struct gale_data data;
			data.p = buf;
			data.l = sizeof(buf);
			_ga_random(data);
			R_RandomUpdate(&random,data.p,data.l);
		}

		init = 1;
	}

	return &random;
}
