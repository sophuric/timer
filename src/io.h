#ifndef IO_H
#define IO_H
#include <stddef.h>
#include <stdbool.h>
#include <signal.h>

typedef void (*fork_io_child_func)(int parent_stdout_fd, int parent_stderr_fd, void *child_ctx);
typedef int (*fork_io_parent_func)(int stdin_fd, int stdout_fd, int stderr_fd, pid_t pid, void *parent_ctx);

int fork_func(fork_io_child_func child_func, void *child_ctx, fork_io_parent_func parent_func, void *parent_ctx, int *wstatus);
int fork_execve(const char *path, char *const *argv, char *const *envp, fork_io_parent_func parent_func, void *parent_ctx, int *wstatus);
int fork_execvp(const char *file, char *const *argv, fork_io_parent_func parent_func, void *parent_ctx, int *wstatus);
int fork_execv(const char *path, char *const *argv, fork_io_parent_func parent_func, void *parent_ctx, int *wstatus);

bool fork_execvp_read_stdout(const char *file, char *const *argv, void *out, size_t out_buf_size, size_t *read_size, bool output_error);
#endif
