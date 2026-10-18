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

/* VERBOSE from the environment: 0, 1 (-v) or 2 (-vv). */
int		verbosity(void);

/*
** Printable, escaped view of n bytes (for failure messages), cut after 60
** bytes (300 with -v, 4000 with -vv). show_max picks the cut itself.
*/
const char	*show(const void *buf, size_t n);
const char	*show_str(const char *s);
const char	*show_max(const void *buf, size_t n, size_t max);

void	run_case(const char *name, void (*fn)(void));
int		finish(const char *title);
void	report_fail(const char *file, int line, const char *fmt, ...)
		__attribute__((format(printf, 3, 4)));
/* An extra line under the last failure (not counted as a failure). */
void	report_note(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/*
** -v / -vv only (no output otherwise):
** explain_diff: expected vs got, aligned, with a ^ under the first
**   difference and what is missing / extra / different.
** diff_lines: the lines returned by n calls against the expected ones
**   (NULL = get_next_line returned NULL), with - / + markers.
*/
void	explain_diff(const char *exp, const char *got);
void	diff_lines(const char **exp, const char **got, size_t n);

/*
** Trace of what get_next_line did (calls, malloc, free, read), recorded at
** -v / -vv and printed at the end of a failing case, or on a crash or
** timeout: the last 20 events with -v, up to 2000 with -vv. Recording stops
** at the first failed check (trace_freeze). trace_clear starts over, e.g.
** at the start of each iteration of a loop; trace_reset also unfreezes.
*/
void	trace(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void	trace_clear(void);
void	trace_reset(void);
void	trace_freeze(void);
void	trace_dump(const char *why);

/* 1 when the test runs under valgrind (it preloads its vgpreload_*.so). */
int		under_valgrind(void);

/*
** Ends the current case as [SKIP] (not counted as passed or failed) and
** prints why. Only call it from inside a case run by run_case().
*/
void	skip_case(const char *why) __attribute__((noreturn));

# define CHECK(cond, ...) do { \
	if (!(cond)) \
		report_fail(__FILE__, __LINE__, __VA_ARGS__); \
} while (0)

# define CHECK_INT(got, exp) do { \
	long long g__ = (long long)(got); \
	long long e__ = (long long)(exp); \
	CHECK(g__ == e__, "%s: expected %lld, got %lld", #got, e__, g__); \
} while (0)

# define CHECK_STR(got, exp) do { \
	const char *g__ = (got); \
	const char *e__ = (exp); \
	if (!(g__ && e__ && strcmp(g__, e__) == 0)) \
	{ \
		report_fail(__FILE__, __LINE__, "%s: expected %s, got %s", #got, \
			e__ ? show_str(e__) : "NULL", g__ ? show_str(g__) : "NULL"); \
		explain_diff(e__, g__); \
	} \
} while (0)

#endif
