#include "gale/misc.h"
#include "gale/globals.h"

#include <assert.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>

struct process {
	pid_t pid;
	void *(*call)(int,void *);
	void *user;
};

static void *on_signal(oop_source *oop,int sig,void *p) {
	struct process * const proc = (struct process *) p;
	int status;
	const pid_t pid = waitpid(proc->pid,&status,WNOHANG | WUNTRACED);
	assert(SIGCHLD == sig);

	if (pid < 0) gale_alert(GALE_WARNING,G_("waitpid"),errno);
	if (pid <= 0) return OOP_CONTINUE;

	assert(pid == proc->pid);

	oop->cancel_signal(oop,SIGCHLD,on_signal,p);
	return proc->call ? proc->call(status,proc->user) : OOP_CONTINUE;
}

static void *on_error(oop_source *oop,struct timeval now,void *p) {
	struct process * const proc = (struct process *) p;
	return proc->call(-1,proc->user);
}

/** Run a subprogram.
 *  Create a subprocess and execute the specified program.  If \a in or
 *  \a out is not NULL, a pipe will be established to the the process'
 *  standard input or standard output, respectively.  If the program cannot
 *  be found or executed and a \a fallback function is supplied, it will
 *  be called instead with the argument list supplied.
 *  \aram oop The liboop event source to use.
 *  \param prog The name of the program to execute (will search $PATH).
 *  \param count The length of the \a args array.
 *  \param args The arguments to use (including argv[0]).
 *  \param in If non-NULL, receives a pipe file descriptor open for writing.
 *  \param out If non-NULL, receives a pipe file descriptor open for reading.
 *  \param fallback If non-NULL, function to call if the program can't be found.
 *  \param done If non-NULL, function to call when the program terminates.
 *  \param user User-defined parameter to pass to \a default and \a done.
 *  \todo Change the argument types to gale_text. */
void gale_exec(oop_source *oop,struct gale_text prog,
	int count,const struct gale_text *args,
	int *in,int *out,
	int (*fallback)(int count,const struct gale_text *args,void *user),
	void *(*done)(int status,void *user),
	void *user)
{
	int inp[2] = { -1, -1 },outp[2] = { -1, -1 };
	struct process *proc;
	gale_create(proc);
	oop->on_signal(oop,SIGCHLD,on_signal,proc);
	proc->call = done;
	proc->user = user;
	proc->pid = 0;

	if ((NULL != in && pipe(inp)) || (NULL != out && pipe(outp))) {
		gale_alert(GALE_WARNING,G_("pipe"),errno);
		goto error;
	}

	proc->pid = fork();
	if (proc->pid < 0) {
		gale_alert(GALE_WARNING,G_("fork"),errno);
		goto error;
	}

	if (proc->pid == 0) {
		char **argv;
		int i;

		gale_create_array(argv,1 + count);
		for (i = 0; i < count; ++i) argv[i] = gale_text_to(
			gale_global->enc_cmdline,args[i]);
		argv[count] = NULL;

		if (NULL != in) {
			dup2(inp[0],0);
			close(inp[0]);
			close(inp[1]);
		}
		if (NULL != out) {
			dup2(outp[1],1);
			close(outp[0]);
			close(outp[1]);
		}

		execvp(gale_text_to(gale_global->enc_filesys,prog),argv);
		if (NULL != fallback)
			_exit(fallback(count,args,user));
		gale_alert(GALE_WARNING,prog,errno);
		_exit(-1);
	}

	if (NULL != in) {
		*in = inp[1];
		close(inp[0]);
	}
	if (NULL != out) {
		*out = outp[0];
		close(outp[1]);
	}

	return;

error:
	if (inp[0] >= 0) close(inp[0]);
	if (inp[1] >= 0) close(inp[1]);
	if (outp[0] >= 0) close(outp[0]);
	if (outp[1] >= 0) close(outp[1]);
	if (in) *in = -1;
	if (out) *out = -1;

	oop->cancel_signal(oop,SIGCHLD,on_signal,proc);
	if (NULL != proc->call) oop->on_time(oop,OOP_TIME_NOW,on_error,proc);
	return;
}
