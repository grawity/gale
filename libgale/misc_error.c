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
	enum gale_error severity;
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

/** Set a different error handler.
 *  The function \a func will be called when an error is reported.
 *  \param oop The liboop source used for dispatch.
 *  \param func The function to call when there's an error.
 *  \param user The user-defined parameter to pass the function. */
void gale_on_error(oop_source *oop,gale_call_error *func,void *user) {
	gale_create(gale_global->error);
	gale_global->error->source = oop;
	gale_global->error->call = func;
	gale_global->error->data = user;
}

/** Report an error; terminate if \a severity is GALE_ERROR.
 *  \param severity The severity of the error (from ::gale_error).
 *  \param msg The error message to report.
 *  \param err If nonzero, a system errno value to look up. */
void gale_alert(int severity,struct gale_text msg,int err) {
	struct error_message *message;
	struct gale_text stamp,prefix,label;

	stamp = gale_time_format(gale_time_now());
	prefix = null_text;
	if (gale_global && gale_global->error_prefix)
		prefix = gale_text_concat(2,G_(" "),
			 gale_text_from(NULL,gale_global->error_prefix,-1));

	switch (severity) {
	case GALE_NOTICE: label = G_(" notice"); break;
	case GALE_WARNING: label = G_(" warning"); break;
	case GALE_ERROR: label = G_(" error"); break;
	}

	gale_create(message);
	message->severity = severity;
	if (0 != err) 
		message->text = gale_text_concat(7,
			stamp,prefix,label,G_(" ("),msg,G_("): "),
			gale_text_from(gale_global->enc_sys,strerror(err),-1));
	else
		message->text = gale_text_concat(5,
			stamp,prefix,label,G_(": "),msg);

	if (NULL == gale_global || NULL == gale_global->error)
		output(message);
	else
		gale_global->error->source->on_time(
			gale_global->error->source,
			OOP_TIME_NOW,
			on_error,message);

	if (GALE_ERROR == severity) exit(1);
}
