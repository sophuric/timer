#include "io.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

#define READ 0
#define WRITE 1

#define CLOSE_(fd)                                                             \
	{                                                                          \
		if (fd != 0) {                                                         \
			if (close(fd) != 0) ERROR("close: %i: %s\n", fd, strerror(errno)); \
		}                                                                      \
	}
#define CLOSE(fd)       \
	{                   \
		if (fd != 0) {  \
			CLOSE_(fd); \
			fd = 0;     \
		}               \
	}

int fork_func(fork_io_child_func child_func, void *child_ctx, fork_io_parent_func parent_func, void *parent_ctx, int *wstatus) {
	int stdin_pipe[2] = {0, 0}, stdout_pipe[2] = {0, 0}, stderr_pipe[2] = {0, 0};
	int res = -1;

#define ERROR(...)            \
	{                         \
		eprintf(__VA_ARGS__); \
		goto cleanup;         \
	}

	pid_t parent_pid = getpid();

	if (pipe(stdin_pipe) != 0) ERROR("pipe: %s\n", strerror(errno));
	if (pipe(stdout_pipe) != 0) ERROR("pipe: %s\n", strerror(errno));
	if (pipe(stderr_pipe) != 0) ERROR("pipe: %s\n", strerror(errno));

	pid_t pid = fork();
	switch (pid) {
		case -1:
			ERROR("fork: %s\n", strerror(errno));
#undef ERROR

		case 0:
#define ERROR(...) \
	{              \
		abort();   \
	}
			// child process

			// close unused pipe ends
			CLOSE(stdin_pipe[WRITE]);
			CLOSE(stdout_pipe[READ]);
			CLOSE(stderr_pipe[READ]);

			// duplicate stdout and stderr
			int parent_stdout = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0); // CLOEXEC = close if exec succeeds
			if (parent_stdout == -1) ERROR("fcntl: %s\n", strerror(errno));

			int parent_stderr = fcntl(STDERR_FILENO, F_DUPFD_CLOEXEC, 0);
			if (parent_stderr == -1) ERROR("fcntl: %s\n", strerror(errno));

#undef ERROR
#define ERROR(...)                           \
	{                                        \
		dprintf(parent_stderr, __VA_ARGS__); \
		abort();                             \
	}

			// redirect child process fds to pipe
			if (dup2(stdin_pipe[READ], STDIN_FILENO) == -1) ERROR("dup2: %s\n", strerror(errno));
			if (dup2(stdout_pipe[WRITE], STDOUT_FILENO) == -1) ERROR("dup2: %s\n", strerror(errno));
			if (dup2(stderr_pipe[WRITE], STDERR_FILENO) == -1) ERROR("dup2: %s\n", strerror(errno));
			CLOSE(stdin_pipe[READ]);
			CLOSE(stdout_pipe[WRITE]);
			CLOSE(stderr_pipe[WRITE]);

			if (child_func)
				child_func(parent_stdout, parent_stderr, child_ctx);

			CLOSE(parent_stdout);
			CLOSE(parent_stderr);

			abort();
#undef ERROR

		default:
			// parent process
#define ERROR(...)            \
	{                         \
		eprintf(__VA_ARGS__); \
		kill(pid, SIGKILL);   \
		goto cleanup;         \
	}

			// close unused pipe ends
			CLOSE(stdin_pipe[READ]);
			CLOSE(stdout_pipe[WRITE]);
			CLOSE(stderr_pipe[WRITE]);

			if (parent_func) {
				int res = parent_func(stdin_pipe[WRITE], stdout_pipe[READ], stderr_pipe[READ], pid, parent_ctx);
				if (res != 0) {
					kill(pid, SIGKILL);
					goto cleanup;
				}
			}

			// wait for child to die
			if (waitpid(pid, wstatus, 0) == -1) ERROR("waitpid: %i: %s\n", pid, strerror(errno));
			pid = 0;

#undef ERROR
#define ERROR(...)            \
	{                         \
		eprintf(__VA_ARGS__); \
		goto cleanup;         \
	}
			CLOSE(stdin_pipe[WRITE]);
			CLOSE(stdout_pipe[READ]);
			CLOSE(stderr_pipe[READ]);

			return 0;
#undef ERROR
	}

cleanup:
	// close all pipes
#define CLOSE_PIPE(pipe)                          \
	{                                             \
		if (pipe[READ] != 0) close(pipe[READ]);   \
		if (pipe[WRITE] != 0) close(pipe[WRITE]); \
	}
	CLOSE_PIPE(stdin_pipe);
	CLOSE_PIPE(stdout_pipe);
	CLOSE_PIPE(stderr_pipe);
	if (pid > 0) kill(pid, SIGKILL);
	return res;
}

#define ecprintf(...) dprintf(parent_stderr_fd, __VA_ARGS__)

struct execv_data {
	const char *file;
	char *const *argv;
	char *const *envp;
};

static void execve_func(int parent_stdout_fd, int parent_stderr_fd, void *ctx_) {
	struct execv_data *ctx = ctx_;
	execve(ctx->file, ctx->argv, ctx->envp);
	ecprintf("execve: %s: %s\n", ctx->file, strerror(errno));
}
static void execvp_func(int parent_stdout_fd, int parent_stderr_fd, void *ctx_) {
	struct execv_data *ctx = ctx_;
	execvp(ctx->file, ctx->argv);
	ecprintf("execvp: %s: %s\n", ctx->file, strerror(errno));
}
static void execv_func(int parent_stdout_fd, int parent_stderr_fd, void *ctx_) {
	struct execv_data *ctx = ctx_;
	execv(ctx->file, ctx->argv);
	ecprintf("execv: %s: %s\n", ctx->file, strerror(errno));
}

int fork_execve(const char *path, char *const *argv, char *const *envp, fork_io_parent_func parent_func, void *parent_ctx, int *wstatus) {
	struct execv_data data = {.file = path, .argv = argv, .envp = envp};
	return fork_func(execve_func, &data, parent_func, parent_ctx, wstatus);
}

int fork_execvp(const char *file, char *const *argv, fork_io_parent_func parent_func, void *parent_ctx, int *wstatus) {
	struct execv_data data = {.file = file, .argv = argv};
	return fork_func(execvp_func, &data, parent_func, parent_ctx, wstatus);
}

int fork_execv(const char *path, char *const *argv, fork_io_parent_func parent_func, void *parent_ctx, int *wstatus) {
	struct execv_data data = {.file = path, .argv = argv};
	return fork_func(execv_func, &data, parent_func, parent_ctx, wstatus);
}

struct read_stdout_ctx {
	char *buf;
	size_t left;
};

static int read_stdout(int stdin_fd, int stdout_fd, int stderr_fd, pid_t pid, void *parent_ctx) {
#define ERROR(...)            \
	{                         \
		eprintf(__VA_ARGS__); \
		return 1;             \
	}
	struct read_stdout_ctx *ctx = parent_ctx;

	while (true) {
		int res, tmp;
		if (ctx->left == 0)
			res = read(stdout_fd, &tmp, 1);
		else
			res = read(stdout_fd, ctx->buf, ctx->left);

		if (res < 0) {
			ERROR("read: %s\n", strerror(errno));
		} else if (res > (ctx->left == 0 ? 1 : ctx->left)) {
			ERROR("read: invalid return value\n");
		} else if (res == 0) {
			// eof
			return 0;
		} else {
			if (ctx->left == 0) ERROR("Too much data\n");
			ctx->buf += res;
			ctx->left -= res;
		}
	}
#undef ERROR
}

#define ERROR(...)            \
	{                         \
		eprintf(__VA_ARGS__); \
		return false;         \
	}
bool fork_execvp_read_stdout(const char *file, char *const *argv, void *out, size_t out_buf_size, size_t *read_size, bool output_error) {
	struct read_stdout_ctx ctx = {.buf = out, .left = out_buf_size};

	int wstatus;
	int res = fork_execvp(file, argv, read_stdout, &ctx, &wstatus);

	if (output_error) {
		if (res != 0) ERROR("Failed to start process\n");
		if (WIFSIGNALED(wstatus)) ERROR("Process was killed with signal: %s\n", strsignal(WTERMSIG(wstatus)));
		if (!WIFEXITED(wstatus)) ERROR("Process is in unknown state\n");
		if (WEXITSTATUS(wstatus)) ERROR("Process exited with status: %i\n", WEXITSTATUS(wstatus));
	}

	if (read_size) *read_size = out_buf_size - ctx.left;

	return true;
}
#undef ERROR
