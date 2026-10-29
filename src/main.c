#include <stdio.h>
#include <poll.h>
#include <getopt.h>
#include <time.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <signal.h>

#include "display.h"
#include "parse_time_str.h"
#include "timespec_math.h"

// #include "../gnulib/lib/parse-datetime.h"

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

static void handle_signal(int signal);
struct display_state display = {.fd = STDERR_FILENO};

static bool do_exit = false, do_suspend = false, do_redraw = false, do_toggle_pause = false, do_pause = false;
static int exit_code = 1;
static struct itimerval itimer = {
        .it_interval = {.tv_sec = 0, .tv_usec = 0},
};

#define TIMER_PRECISION_USEC (1000000)

int main(int argc, char *argv[]) {
	bool invalid = false;
	bool keep_going = false, allow_control = false, disallow_control = false, paused = false;
	char *expire_command = NULL;

	// argument handling
	for (int opt; (opt = getopt_long(argc, argv, ":hVkpPdc:", (struct option[]) {
	                                                                  {"help",           no_argument,       0, 'h'},
	                                                                  {"version",        no_argument,       0, 'V'},
	                                                                  {"keep-going",     no_argument,       0, 'k'},
	                                                                  {"allow-pause",    no_argument,       0, 'p'},
	                                                                  {"disallow-pause", no_argument,       0, 'P'},
	                                                                  {"start-paused",   no_argument,       0, 'd'},
	                                                                  {"command",        required_argument, 0, 'c'},
	                                                                  {0,                0,                 0, 0  }
    },
	                                 NULL)) != -1;) {
		switch (opt) {
			case 'h':
				printf("Usage: %s [<option>...] [<target time>]\n", argv[0]);
				printf("\n"
				       "target time: see DATE STRING in date(1), or info '(coreutils) Date input formats'\n"
				       "Counts down if the target time is in the future, counts up if the time is in the past\n"
				       "\n"
				       "-h --help: Shows help text\n"
				       "-k --keep-going: Don't exit after count down expires\n"
				       "-p --allow-control: Allow pausing for allow times (e.g. 17:23) too as well as relative times (e.g. in 42 minutes)\n"
				       "-P --disallow-control: Prevent pausing regardless of how the target time is specified, has priority over -p\n"
				       "-d --start-paused: Start the timer paused\n"
				       "-c --command <command>: Run shell command when countdown hits zero\n"
				       "\n"
				       "Signals:\n"
				       "SIGUSR1: pauses the timer if it is active\n"
				       "SIGUSR2: toggles pause/resume\n"
				       "Not affected by -P\n"
				       "\n"
				       "Keybinds:\n"
				       "space: toggle pause/resume\n"
				       "q: quit\n"
				       "f: toggle stopwatch/countdown\n"
				       "+/k: add 1 second\n"
				       "i: add 10 seconds\n"
				       "m: add 60 seconds\n"
				       "-/j: subtract 1 second\n"
				       "u: subtract 10 seconds\n"
				       "n: subtract 60 seconds\n"
				       "\n");
				return 0;
			default:
				if (invalid) break;
				switch (opt) {
					case 'k':
						keep_going = true;
						break;
					case 'p':
						allow_control = true;
						break;
					case 'P':
						disallow_control = true;
						break;
					case 'd':
						paused = true;
						break;
					case 'c':
						expire_command = optarg;
						break;
					default:
						invalid = true;
				}
				break;
		}
	}

	if ((optind != argc - 1 && optind != argc) || invalid) {
	invalid:
		eprintf("Invalid usage, try --help\n");
		return 2;
	}

	// parse target time
	const char *target_str = optind == argc - 1 ? argv[optind] : NULL;
	struct timespec target_date;
	bool is_relative;
	if (!parse_time_str(target_str, &target_date, &is_relative)) {
		eprintf("Could not parse time\n");
		return 3;
	}

	if (disallow_control)
		allow_control = false;
	else if (is_relative)
		allow_control = true;

	struct timespec date_now;
	if (clock_gettime(CLOCK_REALTIME, &date_now) == -1) {
		eprintf("clock_gettime: %s\n", strerror(errno));
		return 4;
	}

	// handle exit
	for (int sig = 1; sig < NSIG; ++sig) {
		switch (sig) {
			// don't need to handle these signals
			case SIGTRAP:
			case SIGCHLD:
			case SIGURG:
			case SIGKILL:
			case SIGSTOP:
			case SIGCONT:
				break;
			default:
				if (signal(sig, handle_signal) == SIG_ERR && errno != EINVAL) {
					// we might try to listen to signals that don't exist and fail, so ignore EINVAL
					eprintf("signal: %s: %s\n", strsignal(sig), strerror(errno));
					return 9;
				}
		}
	}

	if (!set_display(&display, true)) return 5;

#define ERROR(code, ...)              \
	{                                 \
		set_display(&display, false); \
		eprintf(__VA_ARGS__);         \
		exit_code = code;             \
		goto exit;                    \
	}

	do_redraw = true;

	// remaining/elapsed time when paused
	int diff_cmp = -2, prev_diff_cmp;
	struct timespec diff_time;

	bool no_input = true;

	// main loop
	while (true) {
		bool recalculate_time = false;
		if (do_suspend) {
			if (!set_display(&display, false)) return 5; // return to normal display
			raise(SIGSTOP);                              // suspend process
			if (!set_display(&display, true)) return 5;  // return back to timer display
			do_suspend = false;
			do_redraw = true;
			recalculate_time = true;
			no_input = true;
		} else if (do_exit) {
			goto exit;
		}

		if (!paused) {
			if (clock_gettime(CLOCK_REALTIME, &date_now) == -1)
				ERROR(1, "clock_gettime: %s\n", strerror(errno));
			prev_diff_cmp = diff_cmp;
			diff_cmp = compare_timespec(date_now, target_date);
			diff_time = subtract_timespec(date_now, target_date);
			if (prev_diff_cmp == -2) prev_diff_cmp = diff_cmp;

			// detect when countdown hits zero
			if (diff_cmp >= 0 && prev_diff_cmp < 0) {
				if (expire_command) {
					// run command
					if (!set_display(&display, false)) return 5;
					int system_res = system(expire_command);
					if (system_res == -1)
						ERROR(1, "system: %s: %s\n", expire_command, strerror(errno));
					if (!keep_going) {
						// exit after expiring, setting exit code to the program's exit code
						exit_code = system_res;
						goto exit;
					}
					if (!set_display(&display, true)) return 5;
					recalculate_time = true;
					no_input = true;
				}
				if (!keep_going) {
					// exit unless specified otherwise
					exit_code = 0;
					goto exit;
				}
			}
		}

		// input
		while (true) {
			struct pollfd pollfd = {.fd = STDIN_FILENO, .events = POLLIN, .revents = 0};
			int poll_res = poll(&pollfd, 1, 0);
			if (poll_res == 0) break;
			if (poll_res == -1) {
				// ignore interrupted system call from signal handler
				if (errno == EINTR) break;
				ERROR(1, "poll: %s\n", strerror(errno));
			}

			if (feof(stdin)) break;
			if (ferror(stdin)) ERROR(6, "Error in stdin\n");

			int c = fgetc(stdin);
			if (no_input) continue; // consume any buffered input at the start of the program
			if (c == 'q') {
				// quit
				exit_code = 0;
				goto exit;
			}
			if (allow_control) {
				switch (c) {
					case 'q':
					case ' ':
						do_toggle_pause = true;
						goto no_set;
					case 'f':
						// toggle stopwatch/countdown
						diff_cmp = -diff_cmp;
						break;
					case '+':
					case 'k':
						diff_time = add_timespec(diff_time, TIMESPEC(1, 0));
						break;
					case 'i':
						diff_time = add_timespec(diff_time, TIMESPEC(10, 0));
						break;
					case 'm':
						diff_time = add_timespec(diff_time, TIMESPEC(60, 0));
						break;
					case '-':
					case 'j':
						diff_time = subtract_timespec_zero(diff_time, TIMESPEC(1, 0));
						break;
					case 'u':
						diff_time = subtract_timespec_zero(diff_time, TIMESPEC(10, 0));
						break;
					case 'n':
						diff_time = subtract_timespec_zero(diff_time, TIMESPEC(60, 0));
						break;
					default:
						goto no_set;
				}
				// set new target date
				target_date = diff_cmp < 0 ? add_timespec(date_now, diff_time) : subtract_timespec(date_now, diff_time);
				do_redraw = true;
			no_set:
			}
		}

		if (do_toggle_pause || do_pause) {
			if (paused && do_toggle_pause) {
				// resume
				recalculate_time = true;
				paused = false;
				do_redraw = true;
			} else if (!paused) {
				// pause
				paused = true;
				do_redraw = true;
			}
			do_toggle_pause = false;
			do_pause = false;
		}

		if (recalculate_time) {
			if (clock_gettime(CLOCK_REALTIME, &date_now) == -1)
				ERROR(1, "clock_gettime: %s\n", strerror(errno));
			target_date = diff_cmp < 0 ? add_timespec(date_now, diff_time) : subtract_timespec(date_now, diff_time);
			recalculate_time = false;
		}

		if (do_redraw) {
			// abort itimer
			itimer.it_value.tv_sec = 0;
			itimer.it_value.tv_usec = 0;
			if (setitimer(ITIMER_REAL, &itimer, NULL) != 0)
				ERROR(1, "setitimer: %s\n", strerror(errno));

			struct tm *timeinfo;
			timeinfo = localtime(&date_now.tv_sec);
			printf("%s - diff: %03" PRIdLEAST64 ".%09" PRIdLEAST32 " - counting %s\r\n", paused ? "paused" : "active", (int_least64_t) diff_time.tv_sec, (int_least32_t) diff_time.tv_nsec, diff_cmp > 0 ? "up" : "down");
			do_redraw = false;

			if (!paused) {
				itimer.it_value.tv_usec = (diff_time.tv_nsec / 1000) % TIMER_PRECISION_USEC;                    // the time until the next multiple of TIMER_PRECISION_USEC
				if (diff_cmp > 0) itimer.it_value.tv_usec = TIMER_PRECISION_USEC - 1 - itimer.it_value.tv_usec; // negate depending if we're counting down or up
				if (itimer.it_value.tv_usec <= 0) itimer.it_value.tv_usec = 1;                                  // just in case

				// redraw after TIMER_PRECISION_USEC has elapsed
				if (setitimer(ITIMER_REAL, &itimer, NULL) != 0)
					ERROR(1, "setitimer: %s\n", strerror(errno));
			}

			do_redraw = false;
		}

		no_input = false;
	}

	exit_code = 0;
exit:
	if (!set_display(&display, false) && exit_code == 0) return 5;
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
		case SIGUSR1:
			do_pause = true;
			break;
		case SIGUSR2:
			do_toggle_pause = true;
			break;
		default:
			// every other signal exits the program
			do_exit = true;
			exit_code = signal == SIGINT ? 0 : 1;
	}
}
