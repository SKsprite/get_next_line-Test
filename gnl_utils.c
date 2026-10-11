#include "gnl_utils.h"
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <sys/types.h>

void	*__real_malloc(size_t size);
void	__real_free(void *p);
ssize_t	__real_read(int fd, void *buf, size_t n);
void	*__wrap_malloc(size_t size);
void	__wrap_free(void *p);
ssize_t	__wrap_read(int fd, void *buf, size_t n);

#define MAX_TRACKED 65536

static int		g_in_gnl = 0;
static void		*g_live[MAX_TRACKED];
static size_t	g_nlive = 0;
static long		g_nmalloc = 0;
static long		g_malloc_fail_at = 0;
static int		g_malloc_failed = 0;
static long		g_nread = 0;
static long		g_read_fail_at = 0;
static int		g_read_failed = 0;
static size_t	g_bytes_read = 0;
static int		g_bad_read_size = 0;

void	*__wrap_malloc(size_t size)
{
	void	*p;

	if (!g_in_gnl)
		return (__real_malloc(size));
	if (++g_nmalloc == g_malloc_fail_at)
	{
		g_malloc_failed = 1;
		return (NULL);
	}
	p = __real_malloc(size);
	if (p && g_nlive < MAX_TRACKED)
		g_live[g_nlive++] = p;
	return (p);
}

void	__wrap_free(void *p)
{
	for (size_t i = g_nlive; p && i-- > 0;)
	{
		if (g_live[i] == p)
		{
			g_live[i] = g_live[--g_nlive];
			break ;
		}
	}
	__real_free(p);
}

ssize_t	__wrap_read(int fd, void *buf, size_t n)
{
	ssize_t	r;

	if (!g_in_gnl)
		return (__real_read(fd, buf, n));
	if (n > (size_t)SSIZE_MAX)
		g_bad_read_size = 1;
	if (++g_nread == g_read_fail_at)
	{
		g_read_failed = 1;
		errno = EIO;
		return (-1);
	}
	r = __real_read(fd, buf, n);
	if (r > 0)
		g_bytes_read += (size_t)r;
	return (r);
}

char	*gnl(int fd)
{
	char	*line;

	g_in_gnl = 1;
	line = get_next_line(fd);
	g_in_gnl = 0;
	return (line);
}

size_t	gnl_live_blocks(void)
{
	return (g_nlive);
}

void	fail_malloc_at(long n)
{
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
		report_fail(f, l, "fd %d: expected %s, got %s", fd,
			exp ? show_str(exp) : "NULL", got ? show_str(got) : "NULL");
	free(got);
	return (ok);
}

void	expect_lines_at(const char *f, int l, int fd, const char **lines,
			size_t n)
{
	for (size_t i = 0; i < n; i++)
	{
		if (!expect_line_at(f, l, fd, lines[i]))
		{
			report_fail(f, l, "  (that was line %zu of %zu)", i + 1, n);
			return ;
		}
	}
	if (expect_line_at(f, l, fd, NULL))
	{
		if (!expect_line_at(f, l, fd, NULL))
			report_fail(f, l, "  (second call after EOF must also be NULL)");
	}
	else
		report_fail(f, l, "  (after the last line, expected NULL)");
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
