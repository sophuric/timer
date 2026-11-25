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
#include <sys/ioctl.h>

#include "display.h"
#include "draw_text.h"
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

bool time_to_str(struct timespec time, char *out, size_t out_len);

static char *default_font = "univers";
static char *default_date_format = "%c";

int main(int argc, char *argv[]) {
#define ERROR(code, ...)      \
	{                         \
		eprintf(__VA_ARGS__); \
		return code;          \
	}
	bool invalid = false;
	bool keep_going = false, allow_control = false, disallow_control = false, paused = false;
	char *expire_command = NULL;
	char *font = default_font;
	char *date_format = default_date_format;
	bool set_title = true;

	// argument handling
	for (int opt; (opt = getopt_long(argc, argv, ":hVkpPdc:f:FTD:", (struct option[]) {
	                                                                        {"help",           no_argument,       0, 'h'},
	                                                                        {"version",        no_argument,       0, 'V'},
	                                                                        {"keep-going",     no_argument,       0, 'k'},
	                                                                        {"allow-pause",    no_argument,       0, 'p'},
	                                                                        {"disallow-pause", no_argument,       0, 'P'},
	                                                                        {"start-paused",   no_argument,       0, 'd'},
	                                                                        {"command",        required_argument, 0, 'c'},
	                                                                        {"font",           required_argument, 0, 'f'},
	                                                                        {"no-font",        no_argument,       0, 'F'},
	                                                                        {"no-set-title",   no_argument,       0, 'T'},
	                                                                        {"date-format",    required_argument, 0, 'D'},
	                                                                        {0,                0,                 0, 0  }
    },
	                                 NULL)) != -1;) {
		switch (opt) {
			case 'h':
				printf("Usage: %s [<option>...] [<target time>]\n"
				       "target time: see DATE STRING in date(1), or info '(coreutils) Date input formats'\n"
				       "Counts down if the target time is in the future, counts up if the time is in the past\n"
				       "\n"
				       "-h --help: Shows help text\n"
				       "-k --keep-going: Don't exit after count down expires, and start counting up\n"
				       "-p --allow-control: Allow pausing and changing time\n"
				       "-P --disallow-control: Prevent pausing and changing time, has priority over -p\n"
				       "-d --start-paused: Start the timer paused\n"
				       "-c --command <command>: Run shell command when countdown hits zero\n"
				       "-f --font <font>: Use this figlet font, default: '%s'\n"
				       "-F --no-font: Don't use figlet to display the timer\n"
				       "-T --no-set-title: Don't set the title to the timer\n"
				       "-D --date-format: Date format for the target date shown below the timer, see strftime(3)\n"
				       "\n"
				       "If neither -p or -P is specified, pausing and changing time will only be allowed for\n"
				       "relative times (e.g. in 42 minutes) and not absolute times (e.g. 17:23)\n"
				       "\n"
				       "If target time is omitted, the program starts counting up from now\n"
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
				       "\n",
				       argv[0],
				       default_font);
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
					case 'f':
						font = optarg;
						break;
					case 'F':
						font = NULL;
						break;
					case 'T':
						set_title = false;
						break;
					case 'D':
						date_format = optarg;
						break;
					default:
						invalid = true;
				}
				break;
		}
	}

	if ((optind != argc - 1 && optind != argc) || invalid)
	invalid:
		ERROR(2, "Invalid usage, try --help\n");

	static const char date_format_prefix_char = '.';
	size_t date_format_len = strlen(date_format);
	if (date_format_len > 512) ERROR(1, "Date format too long\n");
	char date_format_prefixed[date_format_len + 8];
	date_format_prefixed[0] = date_format_prefix_char; // prefix with random character
	memcpy(&date_format_prefixed[1], date_format, date_format_len);
	date_format_prefixed[date_format_len + 1] = '\0';

	struct timespec target_date;
	bool is_relative;

	// parse target date
	const char *target_str = optind == argc - 1 ? argv[optind] : NULL;
	if (target_str != NULL) {
		if (!parse_time_str(target_str, &target_date, &is_relative))
			ERROR(3, "Could not parse date/time\n");
	}

	struct timespec date_now;
	if (clock_gettime(CLOCK_REALTIME, &date_now) == -1)
		ERROR(4, "clock_gettime: %s\n", strerror(errno));

	if (target_str == NULL) {
		target_date = date_now;
		is_relative = true;
	}

	if (disallow_control)
		allow_control = false;
	else if (is_relative)
		allow_control = true;

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
					ERROR(9, "signal: %s: %s\n", strsignal(sig), strerror(errno));
				}
		}
	}

	if (!set_display(&display, true)) return 5;

#undef ERROR
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
	bool recalculate_diff = paused;

	do_suspend = false;

	// main loop
	while (true) {
		bool recalculate_target_date = false;
		if (do_suspend) {
			if (!set_display(&display, false)) return 5; // return to normal display
			raise(SIGSTOP);                              // suspend process
			if (!set_display(&display, true)) return 5;  // return back to timer display
			do_suspend = false;
			do_redraw = true;
			recalculate_target_date = true;
			no_input = true;
		} else if (do_exit) {
			goto exit;
		}

		if (!paused || recalculate_diff) {
			// calculate diff from current time
			if (clock_gettime(CLOCK_REALTIME, &date_now) == -1)
				ERROR(1, "clock_gettime: %s\n", strerror(errno));
			prev_diff_cmp = diff_cmp;
			diff_cmp = compare_timespec(date_now, target_date); // -1 if counting down, +1 if counting up
			diff_time = subtract_timespec(date_now, target_date);
			if (prev_diff_cmp == -2) prev_diff_cmp = diff_cmp; // set to current if previous is not set properly yet (initially -2)
			recalculate_diff = false;
		}

		// detect when countdown hits zero
		if (!paused && diff_cmp >= 0 && prev_diff_cmp < 0) {
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
				recalculate_target_date = true;
				no_input = true;
			}
			if (!keep_going) {
				// exit unless specified otherwise
				exit_code = 0;
				goto exit;
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
			no_set:;
			}
		}

		if (do_toggle_pause || do_pause) {
			if (paused && do_toggle_pause) {
				// resume
				recalculate_target_date = true;
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

		if (recalculate_target_date) {
			// calculate target time from current diff
			if (clock_gettime(CLOCK_REALTIME, &date_now) == -1)
				ERROR(1, "clock_gettime: %s\n", strerror(errno));
			target_date = diff_cmp < 0 ? add_timespec(date_now, diff_time) : subtract_timespec(date_now, diff_time);
			recalculate_target_date = false;
		}

		if (do_redraw) {
			// abort itimer
			itimer.it_value.tv_sec = 0;
			itimer.it_value.tv_usec = 0;
			if (setitimer(ITIMER_REAL, &itimer, NULL) != 0)
				ERROR(1, "setitimer: %s\n", strerror(errno));

#define LENGTH_OF(x) (sizeof(x) / sizeof(*(x)))
			char *target_date_str = NULL;
			char target_date_str_[512];
			if (!paused) {
				struct tm *timeinfo, timeinfo_;
				timeinfo = localtime_r(&target_date.tv_sec, &timeinfo_);
				if (!timeinfo) ERROR(1, "localtime: %s\n", strerror(errno));

				errno = 0;
				if (strftime(target_date_str_, LENGTH_OF(target_date_str_), date_format_prefixed, timeinfo) <= 0)
					ERROR(1, "strftime: %s\n", errno ? strerror(errno) : "Exceeds max bytes or blank date format");

				target_date_str = target_date_str_;
				if (*target_date_str == date_format_prefix_char) ++target_date_str;
			} else
				target_date_str = "Paused";

			// get string of time difference
			char diff_time_str[128];
			if (!time_to_str(diff_time, diff_time_str, LENGTH_OF(diff_time_str))) ERROR(1, "");

			// render text with figlet
			char *render_text = NULL;
			char figlet_text[8192];
			if (font) {
				if (!render_figlet_text(diff_time_str, font, figlet_text, LENGTH_OF(figlet_text))) ERROR(1, "Failed to call figlet. Invalid font?\n");
				render_text = figlet_text;
			} else
				render_text = diff_time_str;

			struct draw_text_blocks_settings block_settings = {
			        .global_offset_x = 0,
			        .global_offset_y = 0,
			        .global_mul_x = 0.5,
			        .global_mul_y = 0.5,
			        .stack_horizontal = false,
			        .each_offset = 1,
			        .each_align = 0.5};

			const char *status_text = diff_cmp > 0 ? "Counting up" : "Counting down";

			struct winsize terminal_size;
			if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal_size) != 0) ERROR(1, "ioctl: TIOCGWINSZ: %s\n", strerror(errno));

			clear_screen(stdout);

			size_t w = terminal_size.ws_col, h = terminal_size.ws_row;
			if (!draw_text_blocks(stdout, DRAW_TEXTS(render_text, status_text, target_date_str), w, h, block_settings)) {
				block_settings.each_offset = 0;
				if (!draw_text_blocks(stdout, DRAW_TEXTS(diff_time_str, status_text, target_date_str), w, h, block_settings)) {
					if (!draw_text_blocks(stdout, DRAW_TEXTS(diff_time_str, status_text), w, h, block_settings)) {
						draw_text_blocks(stdout, DRAW_TEXTS(diff_time_str), w, h, block_settings);
					}
				}
			}

			if (set_title) set_terminal_title(stdout, diff_time_str);

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

		usleep(1000); // 1 ms, to reduce CPU usage
	}

	exit_code = 0;
exit:
	if (!set_display(&display, false) && exit_code == 0) return 5;
	return exit_code;
}

#undef ERROR
#define ERROR(...)                    \
	{                                 \
		set_display(&display, false); \
		eprintf(__VA_ARGS__);         \
		return false;                 \
	}

bool time_to_str(struct timespec time, char *out, size_t out_len) {
	if (time.tv_nsec == 0 && time.tv_sec > 0) --time.tv_sec; // if this were to somehow happen

#define APPEND(...)                                                         \
	{                                                                       \
		int res = snprintf(&out[out_i], out_len - out_i, __VA_ARGS__);      \
		if (res < 0) ERROR("snprintf: %s\n", strerror(errno));              \
		if (res >= out_len - out_i) ERROR("snprintf: hit end of buffer\n"); \
		out_i += res;                                                       \
	}
	size_t out_i = 0;

#define UNIT(VALUE, DIGITS, PREFIX, SUFFIX, IS_LAST)                     \
	{                                                                    \
		if (out_i > 0) APPEND(" ");                                      \
		int_least64_t t = time.tv_sec / (VALUE);                         \
		time.tv_sec -= t * (VALUE);                                      \
		if ((IS_LAST) || out_i > 0 || t > 0)                             \
			APPEND("%s%0*" PRIdLEAST64 "%s", PREFIX, DIGITS, t, SUFFIX); \
	}

	UNIT(86400, 0, "", "d", 0);
	UNIT(3600, 2, "", "h", 0);
	UNIT(60, 2, "", "m", 0);
	UNIT(1, 2, "", "s", 1);

	return true;
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
