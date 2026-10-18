#ifndef TIMESPEC_MATH_H
#define TIMESPEC_MATH_H
#include <time.h>
int compare_timespec(struct timespec a, struct timespec b);
struct timespec add_timespec(struct timespec a, struct timespec b);
struct timespec subtract_timespec(struct timespec a, struct timespec b);
#endif
