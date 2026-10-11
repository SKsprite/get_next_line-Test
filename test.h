#ifndef TEST_H
# define TEST_H

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <ctype.h>
# include <unistd.h>

/*
** Seconds a single test case may run before it is killed (infinite loop or
** get_next_line blocking on read()). Override with TEST_TIMEOUT.
*/
# define DEFAULT_TIMEOUT 5

extern int	g_case_failures;

/* Printable, escaped view of n bytes (for failure messages). */
const char	*show(const void *buf, size_t n);
const char	*show_str(const char *s);

void	run_case(const char *name, void (*fn)(void));
int		finish(const char *title);
void	report_fail(const char *file, int line, const char *fmt, ...)
		__attribute__((format(printf, 3, 4)));

# define CHECK(cond, ...) do { \
	if (!(cond)) \
		report_fail(__FILE__, __LINE__, __VA_ARGS__); \
} while (0)

# define CHECK_INT(got, exp) do { \
	long long g__ = (long long)(got); \
	long long e__ = (long long)(exp); \
	CHECK(g__ == e__, "%s: expected %lld, got %lld", #got, e__, g__); \
} while (0)

#endif
