/*
** Interactive tester:  ./test [-r] [-n count] [fd ...]
**
**   ./test            reads stdin (fd 0) until get_next_line returns NULL
**   ./test 3          reads fd 3 (open it first:  exec 3< file.txt)
**   ./test_bonus 3 4  reads the fds in turn, one line each (bonus)
**   -n count          stop after `count` lines (per fd)
**   -r                raw: print lines exactly as returned (for diff)
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#ifdef GNL_BONUS
# include "get_next_line_bonus.h"
#else
# include "get_next_line.h"
#endif

#define MAX_FDS 256
#define DIM "\033[2m"
#define CYAN "\033[36m"
#define RED "\033[31m"
#define RESET "\033[0m"

static void	usage(const char *prog)
{
	fprintf(stderr, "usage: %s [-r] [-n count] [fd ...]\n"
		"  no fd   read stdin (fd 0)\n"
		"  -n N    stop after N lines per fd\n"
		"  -r      raw output (line exactly as returned)\n"
		"example:  exec 3< file.txt; %s 3; exec 3<&-\n", prog, prog);
	exit(2);
}

static int	parse_int(const char *s, long *out)
{
	char	*end;

	if (!*s)
		return (0);
	errno = 0;
	*out = strtol(s, &end, 10);
	return (!*end && !errno);
}

/* Shows the line with \n, \t, \r and other control bytes made visible. */
static void	print_visible(const char *line)
{
	const unsigned char	*p;

	for (p = (const unsigned char *)line; *p; p++)
	{
		if (*p == '\n')
			printf(DIM "\\n" RESET);
		else if (*p == '\t')
			printf(DIM "\\t" RESET);
		else if (*p == '\r')
			printf(DIM "\\r" RESET);
		else if (*p < 32 || *p == 127)
			printf(DIM "\\x%02x" RESET, *p);
		else
			putchar(*p);
	}
	putchar('\n');
}

int	main(int argc, char **argv)
{
	int		fds[MAX_FDS];
	long	count[MAX_FDS];
	int		done[MAX_FDS];
	int		nfds;
	int		raw;
	long	max;
	long	v;
	int		active;
	char	*line;
	int		i;

	nfds = 0;
	raw = 0;
	max = -1;
	for (i = 1; i < argc; i++)
	{
		if (!strcmp(argv[i], "-r"))
			raw = 1;
		else if (!strcmp(argv[i], "-n") && i + 1 < argc
			&& parse_int(argv[i + 1], &max) && max >= 0)
			i++;
		else if (parse_int(argv[i], &v) && nfds < MAX_FDS)
			fds[nfds++] = (int)v;
		else
			usage(argv[0]);
	}
	if (nfds == 0)
		fds[nfds++] = 0;
	for (i = 0; i < nfds; i++)
	{
		count[i] = 0;
		done[i] = 0;
		if (!raw && fds[i] >= 0 && fcntl(fds[i], F_GETFD) < 0)
			fprintf(stderr, RED "warning: fd %d is not open (open one first, e.g. exec 3< file)"
				RESET "\n", fds[i]);
	}
	active = nfds;
	while (active > 0)
	{
		for (i = 0; i < nfds; i++)
		{
			if (done[i])
				continue ;
			if (max >= 0 && count[i] >= max)
			{
				done[i] = 1;
				active--;
				continue ;
			}
			line = get_next_line(fds[i]);
			if (!line)
			{
				if (!raw)
					printf(CYAN "fd %d" RESET " | " RED "NULL" RESET
						" (after %ld line%s)\n", fds[i], count[i],
						count[i] == 1 ? "" : "s");
				done[i] = 1;
				active--;
				continue ;
			}
			count[i]++;
			if (raw)
				fputs(line, stdout);
			else
			{
				printf(CYAN "fd %d" RESET " | " DIM "%4ld" RESET " | ",
					fds[i], count[i]);
				print_visible(line);
			}
			fflush(stdout);
			free(line);
		}
	}
	return (0);
}
