#include "display.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>

#define dsprintf(...) dprintf(state->fd, __VA_ARGS__)

#define DEBUG_DISPLAY

bool set_display(struct display_state *state, bool enabled) {
	if (enabled == state->enabled) return true;

	// save current termios
	if (enabled)
		if (tcgetattr(state->fd, &state->prev_termios) != 0) {
			dsprintf("tcgetattr: %s\n", strerror(errno));
			return false;
		}

#ifndef DEBUG_DISPLAY
	if (!enabled) // disable
		dsprintf(
		        "\x1b[H"      // move to start
		        "\x1b[2J"     // clear
		        "\x1b[?25h"   // show cursor
		        "\x1b[?7h"    // re-enable newline at end of line
		        "\x1b[?1049l" // restore buffer
		);
#endif

	struct termios new = state->prev_termios;
	if (enabled) {
		cfmakeraw(&new);
		new.c_lflag |= ISIG; // allow sending signals
	}

	// set new termios
	if (tcsetattr(state->fd, TCSANOW, &new) != 0) {
		dsprintf("tcsetattr: %s\n", strerror(errno));
		return false;
	}

#ifndef DEBUG_DISPLAY
	if (enabled) // enable
		dsprintf(
		        "\x1b[?1049h" // move to separate buffer
		        "\x1b[?7l"    // disable newline at end of line
		        "\x1b[?25l"   // hide cursor
		);
#endif

	state->enabled = enabled;

	return true;
}
