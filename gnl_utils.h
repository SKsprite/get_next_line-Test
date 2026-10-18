#ifndef GNL_UTILS_H
# define GNL_UTILS_H

# include "test.h"
# ifdef GNL_BONUS
#  include "get_next_line_bonus.h"
# else
#  include "get_next_line.h"
# endif

/*
** Every test binary is linked with -Wl,--wrap=malloc,--wrap=free,--wrap=read
** so that, while get_next_line runs (through gnl() below):
**   - every block it allocates is tracked until something frees it,
**   - allocation number N can be made to fail  (fail_malloc_at),
**   - read() call number N can be made to fail (fail_read_at),
**   - the number of bytes read() returned is counted.
** Only calls made from inside gnl() are counted / failed.
*/
char	*gnl(int fd);

size_t	gnl_live_blocks(void);
void	fail_malloc_at(long n);
int		malloc_failed(void);
void	fail_read_at(long n);
int		read_failed(void);
size_t	bytes_read(void);
/* True if read() was ever called with a negative size (cast to size_t). */
int		bad_read_size(void);
void	reset_counters(void);

/* Regular files (tmpfile, already deleted) holding exactly this content. */
int		open_mem(const void *data, size_t len);
int		open_str(const char *s);

/* Read end of a pipe; a child process writes the content then closes. */
int		open_pipe_mem(const void *data, size_t len);
int		open_pipe_str(const char *s);
/* Same, but the chunks arrive one by one, delay_us apart. */
int		open_pipe_chunks(const char **chunks, size_t n, unsigned delay_us);

/*
** A real file with a path (so other fds can be opened on it later). Remove
** it with unlink(path) at the end of the case.
*/
void	make_tmp_file(char path[64], const char *content);
/* Appends s to the file at path through a separate O_APPEND fd. */
void	append_str(const char *path, const char *s);
/* Writes all of s to fd (e.g. the write end of a pipe). */
void	write_str(int fd, const char *s);

/*
** Forks a child process that calls get_next_line on fds[0], fds[1], ... in
** turn, until it has n lines from each fd (or NULL), frees every line and
** exits, leaving whatever is in its static stash behind. This is a program
** that stops reading early. The fds are shared with the child, so their
** offset moves. out[i] gets everything the child read from fds[i]
** (malloc'd, free it). Returns the child's wait status.
*/
int		child_reads(const int *fds, size_t nfds, int n, char **out);
/* Describes a wait status that is not a clean exit(0). */
const char	*status_str(int status);
/* malloc'd copy of the first n lines of s (fewer if s has fewer). */
char	*first_lines(const char *s, int n);

/* malloc'd string: n copies of c (no '\0' inside), optional trailing '\n'. */
char	*repeat_char(char c, size_t n, int newline);

/*
** expect_line: next line must be exactly `exp` (NULL = must return NULL).
** expect_lines: the lines in order, then NULL twice (NULL must stay NULL).
** Both free what get_next_line returned.
*/
# define expect_line(fd, exp) expect_line_at(__FILE__, __LINE__, fd, exp)
# define expect_lines(fd, arr, n) expect_lines_at(__FILE__, __LINE__, fd, arr, n)
int		expect_line_at(const char *f, int l, int fd, const char *exp);
void	expect_lines_at(const char *f, int l, int fd, const char **lines,
			size_t n);

# define EXPECT_LINES(fd, ...) do { \
	const char *l__[] = {__VA_ARGS__}; \
	expect_lines(fd, l__, sizeof(l__) / sizeof(*l__)); \
} while (0)

/* Calls get_next_line until it returns NULL (at most `max` times). */
size_t	drain(int fd, size_t max);

/*
** -v / -vv: lists the blocks get_next_line still owns (size, the call that
** allocated them, their content).
*/
void	report_live_blocks(void);
/*
** "malloc #3 (44 bytes) returned NULL during get_next_line call 2": the
** last failure injected by fail_malloc_at / fail_read_at ("" if none).
*/
const char	*last_failure_info(void);

/* Fails the case if get_next_line still owns heap blocks. */
# define CHECK_NO_LEAK(when) do { \
	size_t n__ = gnl_live_blocks(); \
	CHECK(n__ == 0, "%s: %zu block(s) allocated by get_next_line were never" \
		" freed (static stash not freed at EOF / on error?)", when, n__); \
	if (n__) \
		report_live_blocks(); \
} while (0)

#endif
