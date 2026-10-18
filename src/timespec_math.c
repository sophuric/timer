#include "timespec_math.h"

#include <stdio.h>

int compare_timespec(struct timespec a, struct timespec b) {
	if (a.tv_sec > b.tv_sec) return 1;
	if (a.tv_sec < b.tv_sec) return -1;
	if (a.tv_nsec > b.tv_nsec) return 1;
	if (a.tv_nsec < b.tv_nsec) return -1;
	return 0;
}

#define MAX_NANOS (999999999)

struct timespec add_timespec(struct timespec a, struct timespec b) {
	struct timespec res;
	res.tv_sec = a.tv_sec + b.tv_sec;
	// timespec.tv_nsec can store at least the range [0,MAX_NANOS], so we need can't use the result to detect if goes over MAX_NANOS because it could overflow
	if (a.tv_nsec > MAX_NANOS - b.tv_nsec) {
		res.tv_nsec = a.tv_nsec + b.tv_nsec - MAX_NANOS - 1;
		++res.tv_sec;
	} else {
		res.tv_nsec = a.tv_nsec + b.tv_nsec;
	}
	return res;
}
struct timespec subtract_timespec(struct timespec a, struct timespec b) {
	// only return positive value
	if (compare_timespec(a, b) < 0) return subtract_timespec(b, a);

	struct timespec res;
	res.tv_sec = a.tv_sec - b.tv_sec;
	if (b.tv_nsec > a.tv_nsec) {
		// carry negative value
		res.tv_nsec = MAX_NANOS - b.tv_nsec + a.tv_nsec + 1;
		--res.tv_sec;
	} else {
		// subtract like normal
		res.tv_nsec = a.tv_nsec - b.tv_nsec;
	}
	return res;
}
