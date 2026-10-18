#ifndef PARSE_TIME_STR_H
#define PARSE_TIME_STR_H
#include <stdbool.h>
#include <time.h>

bool parse_time_str(const char *str, struct timespec *out, bool *is_relative);
#endif
