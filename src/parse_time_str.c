#include "parse_time_str.h"

#include <stdio.h>
#include <errno.h>
#include <string.h>

#define eprintf(...) fprintf(stderr, __VA_ARGS__)

bool parse_time_str(const char *str, struct timespec *out, bool *is_relative) {
	struct timespec date_now;
	if (clock_gettime(CLOCK_REALTIME, &date_now) == -1) {
		eprintf("clock_gettime: %s\n", strerror(errno));
		return false;
	}

	if (is_relative) {
		*is_relative = false;
	}

	*out = date_now;
	return true;
}
