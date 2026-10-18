#ifndef DISPLAY_H
#define DISPLAY_H
#include <stdbool.h>
#include <termios.h>
#include <unistd.h>

struct display_state {
	int fd;
	struct termios prev_termios;
	bool enabled;
};

bool set_display(struct display_state *state, bool enabled);
#endif
