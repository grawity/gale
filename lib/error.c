#include "gale/misc.h"
#include "gale/compat.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

gale_error_f *gale_error_handler = gale_error_stderr;
const char *gale_error_prefix = NULL;

void gale_error_stderr(int sev,char *msg) {
	char buf[40];
	time_t when;
	time(&when);
	strftime(buf,40," %Y-%m-%d %H:%M:%S ",localtime(&when));
	gale_print(stderr,1,G_("!"));
	gale_print(stderr,0,gale_text_from_local(buf,-1));
	gale_print(stderr,0,gale_text_from_local(msg,-1));
	gale_print(stderr,0,G_("\n"));
	fflush(stderr);
}

void gale_error_syslog(int sev,char *msg) {
	switch (sev) {
	case GALE_NOTICE: sev = LOG_NOTICE; break;
	case GALE_WARNING: sev = LOG_WARNING; break;
	case GALE_ERROR: sev = LOG_ERR; break;
	default: sev = LOG_ERR;
	}
	syslog(sev,"%s",msg);
}

void gale_alert(int sev,const char *msg,int err) {
	char *tmp;
	int len = strlen(msg) + 256;
	if (gale_error_prefix) len += strlen(gale_error_prefix);
	tmp = gale_malloc(len);

	if (gale_error_prefix) {
		strcpy(tmp,gale_error_prefix);
		strcat(tmp," ");
	} else
		strcpy(tmp,"");

	switch (sev) {
	case GALE_NOTICE: strcat(tmp,"notice"); break;
	case GALE_WARNING: strcat(tmp,"warning"); break;
	case GALE_ERROR: strcat(tmp,"error"); break;
	}

	if (err) {
		strcat(tmp," (");
		strcat(tmp,msg);
		strcat(tmp,"): ");
		strcat(tmp,strerror(err));
	} else {
		strcat(tmp,": ");
		strcat(tmp,msg);
	}

	gale_error_handler(sev,tmp);
	gale_free(tmp);

	if (sev == GALE_ERROR) exit(1);
}
