#include "gale/all.h"

#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/wait.h>

pid_t gale_exec(const char *prog,char * const *argv,int *in,int *out,
                void (*def)(char * const *)) {
	int inp[2] = { -1, -1 },outp[2] = { -1, -1 };
	pid_t pid;

	if ((in && pipe(inp)) || (out && pipe(outp))) {
		gale_alert(GALE_WARNING,"pipe",errno);
		goto error;
	}

	pid = fork();
	if (pid < 0) {
		gale_alert(GALE_WARNING,"fork",errno);
		goto error;
	}

	if (pid == 0) {
		if (in) {
			dup2(inp[0],0);
			close(inp[0]);
			close(inp[1]);
		}
		if (out) {
			dup2(outp[1],1);
			close(outp[0]);
			close(outp[1]);
		}
		execvp(prog,argv);
		if (def)
			def(argv);
		else
			gale_alert(GALE_WARNING,prog,errno);
		_exit(0);
	}

	if (in) {
		*in = inp[1];
		close(inp[0]);
	}
	if (out) {
		*out = outp[0];
		close(outp[1]);
	}

	return pid;

error:
	if (inp[0] >= 0) close(inp[0]);
	if (inp[1] >= 0) close(inp[1]);
	if (outp[0] >= 0) close(outp[0]);
	if (outp[1] >= 0) close(outp[1]);
	if (in) *in = -1;
	if (out) *out = -1;
	return -1;
}

int gale_wait(pid_t pid) {
	int status;
	waitpid(pid,&status,0);
	if (WIFEXITED(status)) return WEXITSTATUS(status);
	assert(WIFSIGNALED(status));
	return -WTERMSIG(status);
}
