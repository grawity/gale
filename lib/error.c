#include "gale/misc.h"
#include "gale/compat.h"
#include "gale/globals.h"

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>

struct gale_errors {
	oop_source *source;
	gale_call_error *call;
	void *data;
};

struct error_message {
	gale_error severity;
	struct gale_text text;
};

static void output(struct error_message *message) {
	gale_print(stderr,1,G_("! "));
	gale_print(stderr,0,message->text);
	gale_print(stderr,0,G_("\n"));
	fflush(stderr);
}

static void *on_error(oop_source *source,struct timeval when,void *data) {
	struct error_message *message = (struct error_message *) data;
	if (NULL != gale_global->error->call)
		return gale_global->error->call(
			message->severity,message->text,
			gale_global->error->data);
	output(message);
	return OOP_CONTINUE;
}

void gale_on_error(oop_source *source,gale_call_error *call,void *data) {
	struct gale_errors *error = gale_malloc(sizeof(*error));
	error->source = source;
	error->call = call;
	error->data = data;
	gale_global->error = error;
}

void gale_alert(int sev,const char *msg,int err) {
	struct error_message *message;
	char *tmp;
	time_t when;
	int len = strlen(msg) + 256;
	if (NULL != gale_global->error_prefix) 
		len += strlen(gale_global->error_prefix);
	tmp = gale_malloc(len);

	time(&when);
	strftime(tmp,40,"%Y-%m-%d %H:%M:%S ",localtime(&when));

	if (gale_global->error_prefix) {
		strcat(tmp,gale_global->error_prefix);
		strcat(tmp," ");
	}

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

	message = gale_malloc(sizeof(*message));
	message->severity = sev;
	if (NULL == gale_global)
		message->text = gale_text_from(NULL,tmp,-1);
	else
		message->text = gale_text_from(gale_global->enc_console,tmp,-1);

	if (NULL == gale_global || NULL == gale_global->error)
		output(message);
	else {
		message->text = gale_text_from(gale_global->enc_console,tmp,-1);
		gale_global->error->source->on_time(
			gale_global->error->source,
			OOP_TIME_NOW,
			on_error,message);
	}

	if (sev == GALE_ERROR) exit(1);
}
