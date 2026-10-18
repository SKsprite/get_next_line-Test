#include "gnl_utils.h"
#include <errno.h>
#include <stdarg.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

void	*__real_malloc(size_t size);
void	__real_free(void *p);
ssize_t	__real_read(int fd, void *buf, size_t n);
void	*__wrap_malloc(size_t size);
void	__wrap_free(void *p);
ssize_t	__wrap_read(int fd, void *buf, size_t n);

#define MAX_TRACKED 65536
#define TRACE_MAX 2000
#define TRACE_W 168

static int		g_in_gnl = 0;
static void		*g_live[MAX_TRACKED];
static size_t	g_live_size[MAX_TRACKED];
static long		g_live_seq[MAX_TRACKED];
static long		g_live_call[MAX_TRACKED];
static size_t	g_nlive = 0;
static long		g_nmalloc = 0;
static long		g_malloc_fail_at = 0;
static int		g_malloc_failed = 0;
static long		g_nread = 0;
static long		g_read_fail_at = 0;
static int		g_read_failed = 0;
static size_t	g_bytes_read = 0;
static int		g_bad_read_size = 0;
static long		g_gnl_calls = 0;
static char		g_fail_info[200];
static char		g_trace[TRACE_MAX][TRACE_W];
static long		g_trace_n = 0;
static int		g_trace_frozen = 0;

/*
** Records a trace event from inside the wrappers. Tracking is switched off
** while it formats, so a malloc made by vsnprintf is neither counted nor
** failed.
*/
#define TRACE(...) do { \
	if (verbosity() >= 1 && !g_trace_frozen) \
	{ \
		int s__ = g_in_gnl; \
		g_in_gnl = 0; \
		trace(__VA_ARGS__); \
		g_in_gnl = s__; \
	} \
} while (0)

void	trace(const char *fmt, ...)
{
	va_list	ap;

	if (verbosity() < 1 || g_trace_frozen)
		return ;
	va_start(ap, fmt);
	vsnprintf(g_trace[g_trace_n % TRACE_MAX], TRACE_W, fmt, ap);
	va_end(ap);
	g_trace_n++;
}

void	trace_clear(void)
{
	if (!g_trace_frozen)
		g_trace_n = 0;
}

void	trace_reset(void)
{
	g_trace_frozen = 0;
	g_trace_n = 0;
}

void	trace_freeze(void)
{
	g_trace_frozen = 1;
}

/* Only write(): this also runs from the crash / timeout signal handler. */
void	trace_dump(const char *why)
{
	char	hdr[256];
	long	have;
	long	count;
	int		n;

	if (verbosity() < 1 || g_trace_n == 0)
		return ;
	have = g_trace_n < TRACE_MAX ? g_trace_n : TRACE_MAX;
	count = verbosity() >= 2 ? have : (have < 20 ? have : 20);
	if (!why)
		why = g_trace_frozen ? "up to the first failed check" : "";
	n = snprintf(hdr, sizeof(hdr), "      trace: last %ld of %ld event(s) %s"
		"%s:\n", count, g_trace_n, why,
		count < g_trace_n && verbosity() < 2 ? " (-vv for more)" : "");
	write(STDERR_FILENO, hdr, (size_t)n);
	if (count < g_trace_n && verbosity() >= 2)
	{
		n = snprintf(hdr, sizeof(hdr), "         ... %ld earlier event(s) "
			"dropped\n", g_trace_n - count);
		write(STDERR_FILENO, hdr, (size_t)n);
	}
	for (long k = g_trace_n - count; k < g_trace_n; k++)
	{
		write(STDERR_FILENO, "         ", 9);
		write(STDERR_FILENO, g_trace[k % TRACE_MAX],
			strlen(g_trace[k % TRACE_MAX]));
		write(STDERR_FILENO, "\n", 1);
	}
}

static long	find_live(const void *p)
{
	for (size_t i = g_nlive; p && i-- > 0;)
		if (g_live[i] == p)
			return ((long)i);
	return (-1);
}

static void	set_fail_info(const char *fmt, ...)
{
	va_list	ap;
	int		saved;

	saved = g_in_gnl;
	g_in_gnl = 0;
	va_start(ap, fmt);
	vsnprintf(g_fail_info, sizeof(g_fail_info), fmt, ap);
	va_end(ap);
	g_in_gnl = saved;
}

void	*__wrap_malloc(size_t size)
{
	void	*p;
	long	seq;

	if (!g_in_gnl)
		return (__real_malloc(size));
	seq = ++g_nmalloc;
	if (seq == g_malloc_fail_at)
	{
		g_malloc_failed = 1;
		set_fail_info("malloc #%ld (%zu bytes) returned NULL during "
			"get_next_line call %ld", seq, size, g_gnl_calls);
		TRACE("    malloc(%zu) -> NULL     <== injected failure (malloc #%ld)",
			size, seq);
		return (NULL);
	}
	p = __real_malloc(size);
	if (p && g_nlive < MAX_TRACKED)
	{
		g_live[g_nlive] = p;
		g_live_size[g_nlive] = size;
		g_live_seq[g_nlive] = seq;
		g_live_call[g_nlive++] = g_gnl_calls;
	}
	TRACE("    malloc(%zu) -> block #%ld", size, seq);
	return (p);
}

void	__wrap_free(void *p)
{
	long	i;

	i = find_live(p);
	if (g_in_gnl)
	{
		if (!p)
			TRACE("    free(NULL)");
		else if (i >= 0)
			TRACE("    free(block #%ld, %zu bytes)", g_live_seq[i],
				g_live_size[i]);
		else
			TRACE("    free(%p)  <== not a live block from get_next_line "
				"(double free? pointer moved?)", p);
	}
	if (i >= 0)
	{
		--g_nlive;
		g_live[i] = g_live[g_nlive];
		g_live_size[i] = g_live_size[g_nlive];
		g_live_seq[i] = g_live_seq[g_nlive];
		g_live_call[i] = g_live_call[g_nlive];
	}
	__real_free(p);
}

ssize_t	__wrap_read(int fd, void *buf, size_t n)
{
	ssize_t	r;
	int		saved;

	if (!g_in_gnl)
		return (__real_read(fd, buf, n));
	if (n > (size_t)SSIZE_MAX)
		g_bad_read_size = 1;
	if (++g_nread == g_read_fail_at)
	{
		g_read_failed = 1;
		set_fail_info("read #%ld (fd %d, %zu bytes) returned -1 during "
			"get_next_line call %ld", g_nread, fd, n, g_gnl_calls);
		TRACE("    read(%d, buf, %zu) -> -1     <== injected failure "
			"(read #%ld)", fd, n, g_nread);
		errno = EIO;
		return (-1);
	}
	r = __real_read(fd, buf, n);
	saved = errno;
	if (r > 0)
		g_bytes_read += (size_t)r;
	if (r > 0)
		TRACE("    read(%d, buf, %zu) -> %zd  %s", fd, n, r,
			show_max(buf, (size_t)r, 40));
	else if (r == 0)
		TRACE("    read(%d, buf, %zu) -> 0 (EOF)", fd, n);
	else
		TRACE("    read(%d, buf, %zu) -> -1 (%s)", fd, n, strerror(saved));
	errno = saved;
	return (r);
}

char	*gnl(int fd)
{
	char	*line;

	g_gnl_calls++;
	TRACE("get_next_line(%d)            [call %ld]", fd, g_gnl_calls);
	g_in_gnl = 1;
	line = get_next_line(fd);
	g_in_gnl = 0;
	TRACE("  -> returned %s", line ? show_max(line, strlen(line), 60) : "NULL");
	return (line);
}

const char	*last_failure_info(void)
{
	return (g_fail_info);
}

static int	cmp_live(const void *a, const void *b)
{
	long	ia;
	long	ib;

	ia = *(const long *)a;
	ib = *(const long *)b;
	if (g_live_call[ia] != g_live_call[ib])
		return (g_live_call[ia] < g_live_call[ib] ? -1 : 1);
	return (g_live_seq[ia] < g_live_seq[ib] ? -1 : 1);
}

void	report_live_blocks(void)
{
	long	*idx;
	size_t	max;
	size_t	len;

	if (verbosity() < 1 || g_nlive == 0)
		return ;
	idx = malloc(g_nlive * sizeof(*idx));
	if (!idx)
		return ;
	for (size_t i = 0; i < g_nlive; i++)
		idx[i] = (long)i;
	qsort(idx, g_nlive, sizeof(*idx), cmp_live);
	max = verbosity() >= 2 ? g_nlive : (g_nlive < 10 ? g_nlive : 10);
	report_note("still allocated (oldest first; -vv trace shows each malloc):");
	for (size_t k = 0; k < max; k++)
	{
		/* The content is skipped under valgrind: it may be uninitialised. */
		len = under_valgrind() ? 0 : strnlen(g_live[idx[k]], g_live_size[idx[k]]);
		report_note("  block #%ld, %zu bytes, from get_next_line call %ld%s%s",
			g_live_seq[idx[k]], g_live_size[idx[k]], g_live_call[idx[k]],
			under_valgrind() ? "" : ", holds ",
			under_valgrind() ? "" : show_max(g_live[idx[k]], len, 40));
	}
	if (max < g_nlive)
		report_note("  ... and %zu more (-vv lists them all)", g_nlive - max);
	free(idx);
}

size_t	gnl_live_blocks(void)
{
	return (g_nlive);
}

void	fail_malloc_at(long n)
{
	if (n)
		g_fail_info[0] = '\0';
	g_nmalloc = 0;
	g_malloc_fail_at = n;
	g_malloc_failed = 0;
}

int	malloc_failed(void)
{
	return (g_malloc_failed);
}

void	fail_read_at(long n)
{
	if (n)
		g_fail_info[0] = '\0';
	g_nread = 0;
	g_read_fail_at = n;
	g_read_failed = 0;
}

int	read_failed(void)
{
	return (g_read_failed);
}

int	bad_read_size(void)
{
	return (g_bad_read_size);
}

size_t	bytes_read(void)
{
	return (g_bytes_read);
}

void	reset_counters(void)
{
	fail_malloc_at(0);
	fail_read_at(0);
	g_bytes_read = 0;
}

static void	write_all(int fd, const char *data, size_t len)
{
	ssize_t	w;

	while (len > 0)
	{
		w = write(fd, data, len);
		if (w <= 0)
			return ;
		data += w;
		len -= (size_t)w;
	}
}

int	open_mem(const void *data, size_t len)
{
	FILE	*f;
	int		fd;

	f = tmpfile();
	if (!f)
	{
		perror("tmpfile");
		exit(2);
	}
	fd = dup(fileno(f));
	fclose(f);
	write_all(fd, data, len);
	lseek(fd, 0, SEEK_SET);
	return (fd);
}

int	open_str(const char *s)
{
	return (open_mem(s, strlen(s)));
}

int	open_pipe_chunks(const char **chunks, size_t n, unsigned delay_us)
{
	int		p[2];
	pid_t	pid;

	if (pipe(p) < 0 || (pid = fork()) < 0)
	{
		perror("pipe/fork");
		exit(2);
	}
	if (pid == 0)
	{
		close(p[0]);
		signal(SIGPIPE, SIG_DFL);
		for (size_t i = 0; i < n; i++)
		{
			if (i > 0 && delay_us)
				usleep(delay_us);
			write_all(p[1], chunks[i], strlen(chunks[i]));
		}
		close(p[1]);
		_exit(0);
	}
	close(p[1]);
	return (p[0]);
}

int	open_pipe_mem(const void *data, size_t len)
{
	int		p[2];
	pid_t	pid;

	if (pipe(p) < 0 || (pid = fork()) < 0)
	{
		perror("pipe/fork");
		exit(2);
	}
	if (pid == 0)
	{
		close(p[0]);
		write_all(p[1], data, len);
		close(p[1]);
		_exit(0);
	}
	close(p[1]);
	return (p[0]);
}

int	open_pipe_str(const char *s)
{
	return (open_pipe_mem(s, strlen(s)));
}

char	*repeat_char(char c, size_t n, int newline)
{
	char	*s;

	s = malloc(n + 2);
	if (!s)
	{
		perror("malloc");
		exit(2);
	}
	memset(s, c, n);
	if (newline)
		s[n++] = '\n';
	s[n] = '\0';
	return (s);
}

int	expect_line_at(const char *f, int l, int fd, const char *exp)
{
	char	*got;
	int		ok;

	got = gnl(fd);
	if (!exp)
		ok = (got == NULL);
	else
		ok = (got && strcmp(got, exp) == 0);
	if (!ok)
	{
		report_fail(f, l, "fd %d: expected %s, got %s", fd,
			exp ? show_str(exp) : "NULL", got ? show_str(got) : "NULL");
		explain_diff(exp, got);
	}
	free(got);
	return (ok);
}

/*
** The n lines, then NULL twice. Copies of what came back are kept so -v can
** show the calls up to the first wrong one as a diff.
*/
void	expect_lines_at(const char *f, int l, int fd, const char **lines,
			size_t n)
{
	const char	**exp;
	char		**got;
	char		*line;
	size_t		k;
	int			ok;

	exp = calloc(n + 2, sizeof(*exp));
	got = calloc(n + 2, sizeof(*got));
	if (!exp || !got)
		exit(2);
	memcpy(exp, lines, n * sizeof(*exp));
	for (k = 0; k < n + 2; k++)
	{
		line = gnl(fd);
		got[k] = line ? strdup(line) : NULL;
		ok = exp[k] ? (line && strcmp(line, exp[k]) == 0) : (line == NULL);
		free(line);
		if (!ok)
			break ;
	}
	if (k < n + 2)
	{
		report_fail(f, l, "fd %d: expected %s, got %s", fd,
			exp[k] ? show_str(exp[k]) : "NULL", got[k] ? show_str(got[k]) : "NULL");
		if (k < n)
			report_fail(f, l, "  (that was line %zu of %zu)", k + 1, n);
		else if (k == n)
			report_fail(f, l, "  (after the last line, expected NULL)");
		else
			report_fail(f, l, "  (second call after EOF must also be NULL)");
		diff_lines(exp, (const char **)got, k + 1);
		explain_diff(exp[k], got[k]);
		if (verbosity() >= 1 && k + 1 < n)
			report_note("(%zu more expected line(s) not checked)", n - k - 1);
	}
	for (size_t i = 0; i < n + 2; i++)
		free(got[i]);
	free(got);
	free(exp);
}

size_t	drain(int fd, size_t max)
{
	char	*line;
	size_t	n;

	n = 0;
	while (n < max && (line = gnl(fd)) != NULL)
	{
		free(line);
		n++;
	}
	return (n);
}

void	write_str(int fd, const char *s)
{
	write_all(fd, s, strlen(s));
}

void	make_tmp_file(char path[64], const char *content)
{
	const char	*dir;
	int			fd;

	dir = getenv("TMPDIR");
	snprintf(path, 64, "%s/gnl_test_XXXXXX", dir && strlen(dir) < 40 ? dir : "/tmp");
	fd = mkstemp(path);
	if (fd < 0)
	{
		perror("mkstemp");
		exit(2);
	}
	write_str(fd, content);
	close(fd);
}

void	append_str(const char *path, const char *s)
{
	int	fd;

	fd = open(path, O_WRONLY | O_APPEND);
	if (fd < 0)
	{
		perror(path);
		exit(2);
	}
	write_str(fd, s);
	close(fd);
}

/* Child -> parent record: fd index (1 byte), length, the line's bytes. */
static void	send_line(int out, unsigned char idx, const char *line)
{
	size_t	len;

	len = strlen(line);
	write_all(out, (const char *)&idx, 1);
	write_all(out, (const char *)&len, sizeof(len));
	write_all(out, line, len);
}

static void	child_body(int out, const int *fds, size_t nfds, int n)
{
	char	*line;
	int		done[256];
	size_t	active;

	memset(done, 0, sizeof(done));
	active = nfds;
	for (int round = 0; round < n && active > 0; round++)
	{
		for (size_t i = 0; i < nfds; i++)
		{
			if (done[i])
				continue ;
			line = gnl(fds[i]);
			if (!line)
			{
				done[i] = 1;
				active--;
				continue ;
			}
			send_line(out, (unsigned char)i, line);
			free(line);
		}
	}
	close(out);
	exit(0);
}

static int	read_exact(int fd, void *buf, size_t n)
{
	ssize_t	r;
	char	*p;

	p = buf;
	while (n > 0)
	{
		r = read(fd, p, n);
		if (r <= 0)
			return (0);
		p += r;
		n -= (size_t)r;
	}
	return (1);
}

static void	append_mem(char **dst, size_t *dlen, const char *src, size_t n)
{
	*dst = realloc(*dst, *dlen + n + 1);
	memcpy(*dst + *dlen, src, n);
	*dlen += n;
	(*dst)[*dlen] = '\0';
}

int	child_reads(const int *fds, size_t nfds, int n, char **out)
{
	int				p[2];
	pid_t			pid;
	int				status;
	unsigned char	idx;
	size_t			len;
	size_t			lens[256];
	char			*buf;

	fflush(stdout);
	if (nfds > 256 || pipe(p) < 0 || (pid = fork()) < 0)
		exit(2);
	if (pid == 0)
	{
		close(p[0]);
		child_body(p[1], fds, nfds, n);
	}
	close(p[1]);
	for (size_t i = 0; i < nfds; i++)
	{
		out[i] = calloc(1, 1);
		lens[i] = 0;
	}
	while (read_exact(p[0], &idx, 1) && read_exact(p[0], &len, sizeof(len)))
	{
		buf = malloc(len + 1);
		if (idx >= nfds || !read_exact(p[0], buf, len))
		{
			free(buf);
			break ;
		}
		append_mem(&out[idx], &lens[idx], buf, len);
		free(buf);
	}
	close(p[0]);
	waitpid(pid, &status, 0);
	return (status);
}

const char	*status_str(int status)
{
	static char	buf[128];

	if (WIFSIGNALED(status))
		snprintf(buf, sizeof(buf), "killed by signal %d (%s)",
			WTERMSIG(status), strsignal(WTERMSIG(status)));
	else if (WEXITSTATUS(status) == 42)
		snprintf(buf, sizeof(buf), "valgrind reported errors in the child "
			"(log on stderr)");
	else
		snprintf(buf, sizeof(buf), "exit status %d", WEXITSTATUS(status));
	return (buf);
}

char	*first_lines(const char *s, int n)
{
	const char	*p;

	p = s;
	while (n > 0 && *p)
	{
		if (*p++ == '\n')
			n--;
	}
	return (strndup(s, (size_t)(p - s)));
}
