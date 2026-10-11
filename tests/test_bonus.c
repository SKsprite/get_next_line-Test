#include "gnl_utils.h"
#include <fcntl.h>
#include <sys/resource.h>

static void	three_fds_round_robin(void)
{
	/* The subject's example: fd 3, 4, 5, 3, 4, 5, ... */
	int	a;
	int	b;
	int	c;

	a = open_str("a1\na2\na3\n");
	b = open_str("b1\nb2\nb3\n");
	c = open_str("c1\nc2\nc3\n");
	for (int i = 1; i <= 3; i++)
	{
		char	ea[8];
		char	eb[8];
		char	ec[8];

		sprintf(ea, "a%d\n", i);
		sprintf(eb, "b%d\n", i);
		sprintf(ec, "c%d\n", i);
		expect_line(a, ea);
		expect_line(b, eb);
		expect_line(c, ec);
	}
	expect_line(a, NULL);
	expect_line(b, NULL);
	expect_line(c, NULL);
	close(a);
	close(b);
	close(c);
	CHECK_NO_LEAK("after every fd hit EOF");
}

static void	different_lengths(void)
{
	/* One fd runs out early; the others must keep going. */
	int	a;
	int	b;
	int	c;

	a = open_str("only one line");
	b = open_str("b1\nb2\nb3\nb4\n");
	c = open_str("c1\n\nc3");
	expect_line(a, "only one line");
	expect_line(b, "b1\n");
	expect_line(c, "c1\n");
	expect_line(a, NULL);
	expect_line(b, "b2\n");
	expect_line(c, "\n");
	expect_line(a, NULL);
	expect_line(b, "b3\n");
	expect_line(c, "c3");
	expect_line(b, "b4\n");
	expect_line(c, NULL);
	expect_line(b, NULL);
	close(a);
	close(b);
	close(c);
	CHECK_NO_LEAK("after every fd hit EOF");
}

static void	leftovers_kept_per_fd(void)
{
	/*
	** Long lines with several lines per read() chunk: each fd's leftover
	** buffer must stay with that fd.
	*/
	char	*la;
	char	*lb;
	char	*content_a;
	char	*content_b;
	int		a;
	int		b;

	la = repeat_char('A', 5000, 1);
	lb = repeat_char('B', 3000, 1);
	content_a = malloc(strlen(la) * 2 + 16);
	content_b = malloc(strlen(lb) + 32);
	sprintf(content_a, "x\ny\n%sz\n%s", la, la);
	sprintf(content_b, "%s1\n2\n3", lb);
	a = open_str(content_a);
	b = open_str(content_b);
	expect_line(a, "x\n");
	expect_line(b, lb);
	expect_line(a, "y\n");
	expect_line(b, "1\n");
	expect_line(a, la);
	expect_line(b, "2\n");
	expect_line(a, "z\n");
	expect_line(b, "3");
	expect_line(a, la);
	expect_line(b, NULL);
	expect_line(a, NULL);
	close(a);
	close(b);
	free(la);
	free(lb);
	free(content_a);
	free(content_b);
	CHECK_NO_LEAK("after every fd hit EOF");
}

static void	same_content_two_fds(void)
{
	int	a;
	int	b;

	a = open_str("same\ncontent\nhere");
	b = open_str("same\ncontent\nhere");
	expect_line(a, "same\n");
	expect_line(a, "content\n");
	expect_line(b, "same\n");
	expect_line(a, "here");
	expect_line(b, "content\n");
	expect_line(a, NULL);
	expect_line(b, "here");
	expect_line(b, NULL);
	close(a);
	close(b);
	CHECK_NO_LEAK("after every fd hit EOF");
}

static void	file_pipe_and_stdin(void)
{
	int	file;
	int	p;
	int	in;

	file = open_str("file 1\nfile 2\n");
	p = open_pipe_str("pipe 1\npipe 2\n");
	in = open_pipe_str("stdin 1\nstdin 2");
	dup2(in, STDIN_FILENO);
	close(in);
	expect_line(STDIN_FILENO, "stdin 1\n");
	expect_line(file, "file 1\n");
	expect_line(p, "pipe 1\n");
	expect_line(STDIN_FILENO, "stdin 2");
	expect_line(file, "file 2\n");
	expect_line(p, "pipe 2\n");
	expect_line(STDIN_FILENO, NULL);
	expect_line(file, NULL);
	expect_line(p, NULL);
	close(file);
	close(p);
	CHECK_NO_LEAK("after every fd hit EOF");
}

static void	invalid_fd_between(void)
{
	/* A bad fd in between must not wipe the state of the good ones. */
	int	a;
	int	b;
	int	dead;

	a = open_str("a1\na2\na3\n");
	b = open_str("b1\nb2\n");
	dead = open_str("dead\n");
	close(dead);
	expect_line(a, "a1\n");
	expect_line(-1, NULL);
	expect_line(b, "b1\n");
	expect_line(dead, NULL);
	expect_line(a, "a2\n");
	expect_line(4242, NULL);
	expect_line(b, "b2\n");
	expect_line(a, "a3\n");
	expect_line(a, NULL);
	expect_line(b, NULL);
	close(a);
	close(b);
	CHECK_NO_LEAK("after every fd hit EOF");
}

static void	error_on_one_fd(void)
{
	/* read() failing on one fd must not affect the others. */
	int		a;
	int		b;
	int		dir;

	a = open_str("a1\na2\n");
	b = open_str("b1\nb2 the end");
	dir = open(".", O_RDONLY);
	expect_line(a, "a1\n");
	expect_line(b, "b1\n");
	expect_line(dir, NULL);
	expect_line(a, "a2\n");
	fail_read_at(1);
	expect_line(b, NULL);
	fail_read_at(0);
	expect_line(a, NULL);
	close(a);
	close(b);
	close(dir);
	CHECK_NO_LEAK("after errors on one fd and EOF on the other");
}

static void	closing_one_fd_mid_read(void)
{
	int	a;
	int	b;
	int	c;

	a = open_str("a1\na2\na3\n");
	b = open_str("b1\nb2\nb3\n");
	expect_line(a, "a1\n");
	expect_line(b, "b1\n");
	close(a);
	drain(a, 100);
	c = open_str("c1\nc2\n");
	if (c != a)
	{
		dup2(c, a);
		close(c);
	}
	expect_line(b, "b2\n");
	expect_line(a, "c1\n");
	expect_line(b, "b3\n");
	expect_line(a, "c2\n");
	expect_line(a, NULL);
	expect_line(b, NULL);
	close(a);
	close(b);
	CHECK_NO_LEAK("after every fd hit EOF");
}

#define MANY 100

static void	many_fds(void)
{
	int		fds[MANY];
	char	buf[64];

	for (int i = 0; i < MANY; i++)
	{
		sprintf(buf, "fd%d line1\nfd%d line2\nfd%d line3", i, i, i);
		fds[i] = open_str(buf);
	}
	for (int line = 1; line <= 4 && g_case_failures == 0; line++)
	{
		for (int i = 0; i < MANY; i++)
		{
			if (line == 4)
				expect_line(fds[i], NULL);
			else
			{
				sprintf(buf, line == 3 ? "fd%d line%d" : "fd%d line%d\n",
					i, line);
				expect_line(fds[i], buf);
			}
		}
	}
	for (int i = 0; i < MANY; i++)
		close(fds[i]);
	CHECK_NO_LEAK("after every fd hit EOF");
}

static void	high_fd_number(void)
{
	/* fds are not just 3..20: a stash[FOPEN_MAX] (16) table is too small. */
	struct rlimit	rl;
	int				hi;
	int				fd;
	int				other;

	if (getrlimit(RLIMIT_NOFILE, &rl) < 0 || rl.rlim_cur < 1024)
	{
		printf("      (skipped: open file limit is below 1024)\n");
		return ;
	}
	hi = 1023;
	fd = open_str("high 1\nhigh 2\n");
	dup2(fd, hi);
	close(fd);
	other = open_str("low 1\nlow 2\n");
	expect_line(hi, "high 1\n");
	expect_line(other, "low 1\n");
	expect_line(hi, "high 2\n");
	expect_line(other, "low 2\n");
	expect_line(hi, NULL);
	expect_line(other, NULL);
	close(hi);
	close(other);
	expect_line(1024, NULL);
	expect_line(100000, NULL);
	expect_line(2147483647, NULL);
	CHECK_NO_LEAK("after every fd hit EOF");
}

static void	interleaved_malloc_failures(void)
{
	/* A malloc failure on one fd must not corrupt or leak another fd. */
	int		a;
	int		b;
	int		triggered;
	char	*line;

	for (long n = 1; n < 200; n++)
	{
		a = open_str("a1\na2\na3\n");
		b = open_str("b1\nb2\nb3\n");
		expect_line(a, "a1\n");
		fail_malloc_at(n);
		line = gnl(b);
		free(line);
		line = gnl(b);
		free(line);
		triggered = malloc_failed();
		fail_malloc_at(0);
		expect_line(a, "a2\n");
		expect_line(a, "a3\n");
		expect_line(a, NULL);
		drain(b, 100);
		close(a);
		close(b);
		CHECK_NO_LEAK("after a malloc failure on the other fd");
		if (!triggered || g_case_failures)
			break ;
	}
}

static void	pipes_slow_interleaved(void)
{
	const char	*c1[] = {"slow", " one\n", "slow two\n"};
	const char	*c2[] = {"other ", "one\nother", " two"};
	int			a;
	int			b;

	a = open_pipe_chunks(c1, 3, 20000);
	b = open_pipe_chunks(c2, 3, 30000);
	expect_line(a, "slow one\n");
	expect_line(b, "other one\n");
	expect_line(a, "slow two\n");
	expect_line(b, "other two");
	expect_line(a, NULL);
	expect_line(b, NULL);
	close(a);
	close(b);
	CHECK_NO_LEAK("after every fd hit EOF");
}

int	main(void)
{
	printf("bonus tests: multiple fds (BUFFER_SIZE=%ld)\n", (long)BUFFER_SIZE);
	run_case("3 fds round robin (subject example)", three_fds_round_robin);
	run_case("fds of different lengths", different_lengths);
	run_case("leftover buffer stays with its fd", leftovers_kept_per_fd);
	run_case("two fds with the same content", same_content_two_fds);
	run_case("file + pipe + stdin interleaved", file_pipe_and_stdin);
	run_case("invalid fds in between don't reset others", invalid_fd_between);
	run_case("read() error on one fd doesn't affect others", error_on_one_fd);
	run_case("closing one fd mid-read, number reused", closing_one_fd_mid_read);
	run_case("100 fds open at once", many_fds);
	run_case("fd 1023, and fds >= 1024 / INT_MAX -> NULL", high_fd_number);
	run_case("malloc failure on one fd doesn't hurt another",
		interleaved_malloc_failures);
	run_case("two slow pipes interleaved", pipes_slow_interleaved);
	return (finish("bonus"));
}
