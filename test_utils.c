#include "test.h"
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

#define GREEN "\033[32m"
#define RED "\033[31m"
#define YELLOW "\033[33m"
#define RESET "\033[0m"

/* Failures printed per case before the rest are summarised. */
#define MAX_REPORTED 10

int			g_case_failures = 0;
static int	g_passed = 0;
static int	g_failed = 0;

const char	*show(const void *buf, size_t n)
{
	static char		out[8][512];
	static int		slot = 0;
	const unsigned char	*p;
	char			*o;
	size_t			i;
	size_t			len;

	if (!buf)
		return ("(null)");
	p = buf;
	o = out[slot++ % 8];
	len = 0;
	o[len++] = '"';
	for (i = 0; i < n && i < 60; i++)
	{
		if (p[i] == '\\' || p[i] == '"')
			len += sprintf(o + len, "\\%c", p[i]);
		else if (isprint(p[i]))
			o[len++] = (char)p[i];
		else
			len += sprintf(o + len, "\\x%02x", p[i]);
	}
	o[len++] = '"';
	if (i < n)
		len += sprintf(o + len, "... (%zu bytes)", n);
	o[len] = '\0';
	return (o);
}

const char	*show_str(const char *s)
{
	if (!s)
		return ("NULL");
	return (show(s, strlen(s)));
}

static int	case_timeout(void)
{
	const char	*env;
	int			t;

	env = getenv("TEST_TIMEOUT");
	if (env && (t = atoi(env)) > 0)
		return (t);
	return (DEFAULT_TIMEOUT);
}

void	report_fail(const char *file, int line, const char *fmt, ...)
{
	va_list	ap;

	g_case_failures++;
	if (g_case_failures > MAX_REPORTED)
	{
		if (g_case_failures == MAX_REPORTED + 1)
			fprintf(stdout, "      " RED "-> " RESET
				"... more failures in this case suppressed\n");
		return ;
	}
	fprintf(stdout, "      " RED "-> " RESET "%s:%d: ", file, line);
	va_start(ap, fmt);
	vfprintf(stdout, fmt, ap);
	va_end(ap);
	fprintf(stdout, "\n");
	fflush(stdout);
}

static const char	*signal_name(int sig)
{
	if (sig == SIGSEGV)
		return ("SEGFAULT");
	if (sig == SIGBUS)
		return ("BUS ERROR");
	if (sig == SIGABRT)
		return ("ABORT (bad free / heap corruption?)");
	if (sig == SIGALRM)
		return ("TIMEOUT (infinite loop?)");
	if (sig == SIGFPE)
		return ("FLOATING POINT EXCEPTION");
	return (strsignal(sig));
}

/* Reads everything the child wrote so it can be printed under the KO line. */
static char	*slurp(int fd)
{
	char	*buf;
	char	*tmp;
	size_t	len;
	size_t	cap;
	ssize_t	n;

	len = 0;
	cap = 4096;
	buf = malloc(cap + 1);
	if (!buf)
		return (NULL);
	while ((n = read(fd, buf + len, cap - len)) > 0)
	{
		len += (size_t)n;
		if (len == cap)
		{
			cap *= 2;
			tmp = realloc(buf, cap + 1);
			if (!tmp)
				break ;
			buf = tmp;
		}
	}
	buf[len] = '\0';
	return (buf);
}

/*
** Each case runs in its own child process so a crash or infinite loop
** only fails that case instead of killing the whole run.
*/
void	run_case(const char *name, void (*fn)(void))
{
	pid_t	pid;
	int		status;
	int		fds[2];
	char	*details;

	fflush(stdout);
	if (pipe(fds) < 0 || (pid = fork()) < 0)
	{
		perror("pipe/fork");
		exit(2);
	}
	if (pid == 0)
	{
		close(fds[0]);
		dup2(fds[1], STDOUT_FILENO);
		close(fds[1]);
		alarm((unsigned int)case_timeout());
		g_case_failures = 0;
		fn();
		fflush(stdout);
		_exit(g_case_failures ? 1 : 0);
	}
	close(fds[1]);
	details = slurp(fds[0]);
	close(fds[0]);
	waitpid(pid, &status, 0);
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
	{
		g_passed++;
		printf("  " GREEN "[OK]" RESET " %s\n", name);
	}
	else
	{
		g_failed++;
		if (WIFSIGNALED(status))
			printf("  " RED "[KO]" RESET " %s " YELLOW "-> %s" RESET "\n",
				name, signal_name(WTERMSIG(status)));
		else if (WEXITSTATUS(status) == 42)
			printf("  " RED "[KO]" RESET " %s " YELLOW
				"-> valgrind reported memory errors (see above)" RESET "\n",
				name);
		else
			printf("  " RED "[KO]" RESET " %s\n", name);
	}
	if (details)
		fputs(details, stdout);
	free(details);
	fflush(stdout);
}

int	finish(const char *title)
{
	printf("%s%s: %d/%d passed" RESET "\n\n",
		g_failed ? RED : GREEN, title, g_passed, g_passed + g_failed);
	return (g_failed != 0);
}
