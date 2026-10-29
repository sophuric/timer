#include "parse_time_str.h"
#include "timespec_math.h"

#include "io.h"

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)
#define STR_(str) #str
#define STR(str) STR_(str)

static bool call_date(const char *str, struct timespec *out) {
	if (!str) str = "now";
	static char *const path = STR(DATE_BIN_PATH);
	char *const argv[] = {path, "+%s;%N", "--date", (char *) str, NULL};
	int wstatus;

	char buf[257] = {0};
	size_t buf_size;

	if (!fork_execvp_read_stdout(path, argv, buf, sizeof(buf) / sizeof(*buf) - 1, &buf_size)) return false;

	buf[buf_size] = '\0';

	int_least64_t sec;  // time_t is at least i64
	int_least32_t nsec; // struct timespec.tv_nsec is at least i32

	// parse date output
	int res = sscanf(buf, "%" SCNdLEAST64 ";%09" SCNdLEAST32 "\n", &sec, &nsec);
	if (res != 2) {
		eprintf("Failed to match date output\n");
		return false;
	}

	struct timespec ts = {
	        .tv_sec = sec,
	        .tv_nsec = nsec};
	*out = ts;
	return true;
}

bool parse_time_str(const char *str, struct timespec *out, bool *is_relative) {
	struct timespec ts1, ts2;
	if (!call_date(str, &ts1)) return false;
	usleep(100000); // wait 0.1 sec
	if (!call_date(str, &ts2)) return false;

	if (is_relative) {
		// find the difference between the parsed date 0.1 seconds ago, and the parsed date now
		struct timespec diff = subtract_timespec_zero(ts2, ts1);

		*is_relative = false;

		// if the difference is between between 0 and 10 seconds exclusive, then it is a relative time
		// the time would have changed if it is a relative time
		// we check for <10s, since if the date where to change between the 0.1s, we would get a 24hr difference
		if (compare_timespec(diff, TIMESPEC(0, 0)) > 0 && compare_timespec(diff, TIMESPEC(10, 0)) < 0) {
			*is_relative = true;
		}
	}

	*out = ts2;

	return true;
}
