#include "gale/client.h"
#include "gale/globals.h"

struct gale_error_queue {
	oop_source *oop;
	gale_call_message *call;
	void *data;
	struct gale_text buffer;
	struct gale_location *target;
	int is_active;
};

static oop_call_time on_kick;

static void activate(struct gale_error_queue *queue) {
	const int is_active = 
		  (NULL != queue->call 
		&& NULL != queue->target 
		&&    0 != queue->buffer.l);
	if (queue->is_active && !is_active)
		queue->oop->cancel_time(queue->oop,OOP_TIME_NOW,on_kick,queue);
	else if (!queue->is_active && is_active)
		queue->oop->on_time(queue->oop,OOP_TIME_NOW,on_kick,queue);
	queue->is_active = is_active;
}

static void *on_kick(oop_source *oop,struct timeval when,void *x) {
	struct gale_error_queue * const queue = (struct gale_error_queue *) x;
	struct gale_fragment frag;
	struct gale_message *msg;

	gale_create(msg);
	msg->from = NULL;
	gale_create_array(msg->to,2);
	msg->to[0] = queue->target;
	msg->to[1] = NULL;
	msg->data = gale_group_empty();

	frag.type = frag_text;
	frag.name = G_("message/body");
	frag.value.text = queue->buffer;
	gale_group_add(&msg->data,frag);

	gale_add_id(&msg->data,G_("daemon"));

	frag.name = G_("message/sender");
	frag.type = frag_text;
        frag.value.text = gale_text_concat(6,
		gale_var(G_("HOST")),G_(" "),
		gale_text_from(NULL,gale_global->error_prefix,-1),
		G_(" ("),gale_var(G_("LOGNAME")),G_(")"));
	gale_group_add(&msg->data,frag);

	queue->buffer = null_text;
	activate(queue);
	return queue->call(msg,queue->data);
}

static void *on_location(struct gale_text n,struct gale_location *loc,void *x) {
	struct gale_error_queue * const queue = (struct gale_error_queue *) x;
	if (NULL == loc) {
		const struct gale_text normal = gale_text_concat(2,
			G_("_gale.server@"),
			gale_var(G_("GALE_DOMAIN")));
		if (gale_text_compare(n,normal)) {
			gale_find_exact_location(queue->oop,
				normal,on_location,queue);
			return OOP_CONTINUE;
		}

		/* Shouldn't happen; oh well... */
		return OOP_CONTINUE;
	}

	queue->target = loc;
	activate(queue);
	return OOP_CONTINUE;
}

/** Create an error queue object.
 *  \param oop Liboop event source to use.
 *  \return New error queue object, used to report errors as messages. 
 *  \sa gale_on_queue(), gale_queue_error() */
struct gale_error_queue *gale_make_queue(oop_source *oop) {
	struct gale_text name = gale_var(G_("GALE_ERRORS"));
	struct gale_error_queue *queue;
	gale_create(queue);
	queue->oop = oop;
	queue->call = NULL;
	queue->buffer = null_text;
	queue->target = NULL;
	queue->is_active = 0;

	if (0 == name.l) name = G_("_gale.server");
	gale_find_location(oop,name,on_location,queue);
	return queue;
}

/** Set a handler to be called when an error message object is generated.
 *  \param queue Error queue object to monitor.
 *  \param func The function to call when a new message is generated.
 *  \param user A user-defined parameter.
 *  \sa gale_make_queue(), gale_queue_error() */
void gale_on_queue(
	struct gale_error_queue *queue,
	gale_call_message *func,void *user) 
{
	queue->call = func;
	queue->data = user;
	activate(queue);
}

/** Add an error report to an error queue object.
 *  \param severity The severity of the error.
 *  \param msg The error message.
 *  \param queue The error queue object.
 *  \return OOP_CONTINUE.
 *  \sa gale_make_queue(), gale_on_queue() */
void *gale_queue_error(int severity,struct gale_text msg,void *queue) {
	struct gale_error_queue * const q = (struct gale_error_queue *) queue;
	q->buffer = gale_text_concat(3,q->buffer,msg,G_("\n"));
	activate(q);
	return OOP_CONTINUE;
}
