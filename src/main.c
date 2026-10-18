#include <stdio.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <signal.h>

#include "display.h"
#include "parse_time_str.h"
#include "timespec_math.h"

// #include "../gnulib/lib/parse-datetime.h"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

static void handle_signal(int signal);
struct display_state display = {.fd = STDERR_FILENO};

static bool do_exit = false, do_suspend = false, do_redraw = false;
static int exit_code = 1;
static struct itimerval itimer = {
        .it_interval = {.tv_sec = 0, .tv_usec = 0},
};

#define TIMER_PRECISION_USEC (1000000)

int main(int argc, char *argv[]) {
	bool invalid = false;
	bool keep_going = false, allow_pause = false, disallow_pause = false;

	// argument handling
	for (int opt; (opt = getopt_long(argc, argv, ":hVk", (struct option[]) {
	                                                             {"help",           no_argument, 0, 'h'},
	                                                             {"version",        no_argument, 0, 'V'},
	                                                             {"keep-going",     no_argument, 0, 'k'},
	                                                             {"allow-pause",    no_argument, 0, 'p'},
	                                                             {"disallow-pause", no_argument, 0, 'P'},
	                                                             {0,                0,           0, 0  }
    },
	                                 NULL)) != -1;) {
		switch (opt) {
			case 'h':
				printf("Usage: %s [OPTION] <target time>...\n", argv[0]);
				printf("target time: see DATE STRING in date(1), or info '(coreutils) Date input formats'\n"
				       "Counts down if the target time is in the future, counts up if the time is in the past\n"
				       "-h --help: Shows help text\n"
				       "-k --keep-going: Don't exit after count down expires\n"
				       "-p --allow-pause: Allow pausing for allow times (e.g. 17:23) too as well as relative times (e.g. in 42 minutes)\n"
				       "-P --disallow-pause: Prevent pausing regardless of how the target time is specified, has priority over -p");
				return 0;
			default:
				if (invalid) break;
				switch (opt) {
					case 'k':
						keep_going = true;
						break;
					case 'p':
						allow_pause = true;
						break;
					case 'P':
						disallow_pause = true;
						break;
					default:
						invalid = true;
						break;
				}
				break;
		}
	}

	if (optind != argc - 1 || invalid) {
	invalid:
		eprintf("Invalid usage, try --help\n");
		return 2;
	}

	// parse target time
	const char *target_str = argv[optind];
	struct timespec target_date;
	bool is_relative;
	if (!parse_time_str(target_str, &target_date, &is_relative)) {
		eprintf("Could not parse time\n");
		return 3;
	}

	if (disallow_pause)
		allow_pause = false;
	else if (is_relative)
		allow_pause = true;

	struct timespec date_now;
	if (clock_gettime(CLOCK_REALTIME, &date_now) == -1) {
		eprintf("clock_gettime: %s\n", strerror(errno));
		return 4;
	}

	// don't quit after passing the target time, if the target time is in the past
	if (compare_timespec(date_now, target_date) >= 0) keep_going = true;

	// remaining/elapsed time when paused
	bool paused = false;
	int diff_cmp;
	struct timespec diff_time;

	// handle exit
	for (int sig = 1; sig < NSIG; ++sig) {
		switch (sig) {
			case SIGTRAP:
			case SIGCHLD:
			case SIGURG:
			case SIGKILL:
			case SIGSTOP:
			case SIGCONT:
				break;
			default:
				signal(sig, handle_signal);
		}
	}

	if (!set_display(&display, true)) return 1;

#define ERROR(code, ...)              \
	{                                 \
		set_display(&display, false); \
		eprintf(__VA_ARGS__);         \
		exit_code = code;             \
		goto exit;                    \
	}

	do_redraw = true;

	// main loop
	while (true) {
		if (do_suspend) {
			if (!set_display(&display, false)) return 1; // return to normal display
			raise(SIGSTOP);                              // suspend process
			if (!set_display(&display, true)) return 1;  // return back to timer display
			do_suspend = false;
			do_redraw = true;
		} else if (do_exit) {
			goto exit;
		}

		if (!paused) {
			if (clock_gettime(CLOCK_REALTIME, &date_now) == -1) {
				ERROR(1, "clock_gettime: %s\n", strerror(errno));
			}
			diff_cmp = compare_timespec(date_now, target_date);
			diff_time = subtract_timespec(date_now, target_date);
		}

		/*
		if (!paused) {
			// pause
			paused = true;
		} else {
			// resume
			target_date = diff_cmp < 0 ? add_timespec(date_now, diff_time) : subtract_timespec(date_now, diff_time);
			paused = false;
		}
		*/

		if (do_redraw) {
			// abort itimer
			itimer.it_value.tv_sec = 0;
			itimer.it_value.tv_usec = 0;
			if (setitimer(ITIMER_REAL, &itimer, NULL) != 0) {
				ERROR(1, "setitimer: %s\n", strerror(errno));
			}

			struct tm *timeinfo;
			timeinfo = localtime(&date_now.tv_sec);
			printf("Paused: %s - diff: %03i.%09i\n", paused ? "true" : "false", diff_time.tv_sec, diff_time.tv_nsec);
			do_redraw = false;

			// if (!paused) {
			itimer.it_value.tv_usec = (diff_time.tv_nsec / 1000) % TIMER_PRECISION_USEC;                // the time until the next multiple of TIMER_PRECISION_USEC
			if (diff_cmp > 0) itimer.it_value.tv_usec = TIMER_PRECISION_USEC - itimer.it_value.tv_usec; // negate depending if we're counting down or up
			if (itimer.it_value.tv_usec <= 0) itimer.it_value.tv_usec = 1;                              // just in case

			// redraw after TIMER_PRECISION_USEC has elapsed
			if (setitimer(ITIMER_REAL, &itimer, NULL) != 0) {
				ERROR(1, "setitimer: %s\n", strerror(errno));
			}
			// }

			do_redraw = false;
		}
	}

	exit_code = 0;
exit:
	set_display(&display, false);
	return exit_code;
}

static void handle_signal(int signal) {
	switch (signal) {
		case SIGWINCH: // from resizing window
		case SIGALRM:  // from setitimer
			do_redraw = true;
			break;
		case SIGTSTP: // ctrl+Z suspend process
		case SIGTTIN:
		case SIGTTOU:
			do_suspend = true;
			break;
		default:
			do_exit = true;
			exit_code = signal == SIGINT ? 0 : 1;
	}
}
