#include "gnl_utils.h"
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>

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
		skip_case("open file limit is below 1024 (ulimit -n)");
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
		trace_clear();
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

/* ------------------------------------------------- malloc failures */

#define RR_FDS 3

static int	same_or_null(const char *a, const char *b)
{
	if (!a || !b)
		return (a == b);
	return (strcmp(a, b) == 0);
}

static void	malloc_failures_round_robin(void)
{
	/*
	** 3 fds read in turn while malloc 1, 2, 3, ... fails. Only the fd whose
	** call saw the failure may lose lines; the other fds must carry on with
	** exactly the right lines, and nothing may leak.
	*/
	const char	*text[RR_FDS] = {"a1\na2 longer\na3\n", "b1\n\nb3 no nl",
		"c1 is the longest line here\nc2\n"};
	const char	*lines[RR_FDS][4] = {{"a1\n", "a2 longer\n", "a3\n", NULL},
		{"b1\n", "\n", "b3 no nl", NULL},
		{"c1 is the longest line here\n", "c2\n", NULL, NULL}};
	int			fds[RR_FDS];
	int			next[RR_FDS];
	int			tainted[RR_FDS];
	int			triggered;
	char		*got;

	for (long n = 1; n < 500; n++)
	{
		trace_clear();
		for (int i = 0; i < RR_FDS; i++)
		{
			fds[i] = open_str(text[i]);
			next[i] = 0;
			tainted[i] = 0;
		}
		fail_malloc_at(n);
		triggered = 0;
		for (int round = 0; round < 4 && !g_case_failures; round++)
		{
			for (int i = 0; i < RR_FDS; i++)
			{
				got = gnl(fds[i]);
				if (malloc_failed() && !triggered)
				{
					triggered = 1;
					tainted[i] = 1;
				}
				if (!tainted[i])
					CHECK((!got && !lines[i][next[i]]) || (got && lines[i][next[i]]
						&& strcmp(got, lines[i][next[i]]) == 0), "malloc %ld "
						"failed on another fd: fd %c expected %s, got %s", n,
						'a' + i, lines[i][next[i]] ? show_str(lines[i][next[i]])
						: "NULL", show_str(got));
				if (!tainted[i] && !same_or_null(got, lines[i][next[i]]))
				{
					if (*last_failure_info())
						report_note("injected: %s", last_failure_info());
					explain_diff(lines[i][next[i]], got);
				}
				if (got && next[i] < 3)
					next[i]++;
				free(got);
			}
		}
		fail_malloc_at(0);
		for (int i = 0; i < RR_FDS; i++)
		{
			drain(fds[i], 100);
			close(fds[i]);
		}
		CHECK_NO_LEAK("after a malloc failure with 3 fds open");
		if (!triggered || g_case_failures)
			break ;
	}
}

/* ------------------------------------------- stopping early, exiting */

static void	stop_early_on_two_fds(void)
{
	/*
	** Read a few lines of a and b, stop, close both. A third fd that is
	** still open must be unaffected, and calls on the closed fds must only
	** give their own buffered lines, then NULL, with nothing leaked.
	*/
	const char	*la[] = {"a1\n", "a2\n", "a3\n", "a4"};
	const char	*lb[] = {"b1\n", "b2\n", "b3\n", "b4"};
	int			a;
	int			b;
	int			c;
	int			next;
	char		*got;

	a = open_str("a1\na2\na3\na4");
	b = open_str("b1\nb2\nb3\nb4");
	c = open_str("c1\nc2\nc3\n");
	expect_line(a, "a1\n");
	expect_line(b, "b1\n");
	expect_line(c, "c1\n");
	expect_line(b, "b2\n");
	close(a);
	close(b);
	expect_line(c, "c2\n");
	next = 1;
	while ((got = gnl(a)) != NULL && next < 4)
	{
		CHECK(strcmp(got, la[next]) == 0, "closed fd a: got %s, expected %s "
			"or NULL", show_str(got), show_str(la[next]));
		free(got);
		next++;
	}
	free(got);
	next = 2;
	while ((got = gnl(b)) != NULL && next < 4)
	{
		CHECK(strcmp(got, lb[next]) == 0, "closed fd b: got %s, expected %s "
			"or NULL", show_str(got), show_str(lb[next]));
		free(got);
		next++;
	}
	free(got);
	EXPECT_LINES(c, "c3\n");
	close(c);
	CHECK_NO_LEAK("after stopping early on 2 fds");
}

static void	child_reads_two_fds_and_exits(void)
{
	/* A program reads x lines from each of 2 fds, in turn, and exits. */
	const char	*ta = "a1\na2 longer\na3\na4\n";
	const char	*tb = "b1\n\nb3 no newline";
	int			fds[2];
	char		*out[2];
	char		*exp;
	int			status;

	for (int x = 1; x <= 4; x++)
	{
		fds[0] = open_str(ta);
		fds[1] = open_str(tb);
		status = child_reads(fds, 2, x, out);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
			"program reading %d line(s) per fd: %s", x, status_str(status));
		exp = first_lines(ta, x);
		CHECK(strcmp(out[0], exp) == 0, "fd a, %d line(s): printed %s, "
			"expected %s", x, show_str(out[0]), show_str(exp));
		free(exp);
		exp = first_lines(tb, x);
		CHECK(strcmp(out[1], exp) == 0, "fd b, %d line(s): printed %s, "
			"expected %s", x, show_str(out[1]), show_str(exp));
		free(exp);
		free(out[0]);
		free(out[1]);
		close(fds[0]);
		close(fds[1]);
	}
}

static char	*make_lines(char tag, int n)
{
	char	*s;
	size_t	len;

	s = malloc((size_t)n * 48 + 1);
	len = 0;
	for (int i = 0; i < n; i++)
		len += (size_t)sprintf(s + len, "%c%d%.*s\n", tag, i, (i * 7) % 19,
			"--------------------");
	s[len] = '\0';
	return (s);
}

static void	several_programs_two_fds(void)
{
	/*
	** Two fds shared by programs run one after the other; each reads 2
	** lines from each fd and exits. Program k must print, for each fd, the
	** lines that start where that fd's offset was.
	*/
	char	*text[2];
	int		fds[2];
	off_t	off[2];
	char	*out[2];
	char	*exp;
	int		status;

	text[0] = make_lines('a', 25);
	text[1] = make_lines('b', 40);
	fds[0] = open_str(text[0]);
	fds[1] = open_str(text[1]);
	for (int run = 1; run <= 8 && !g_case_failures; run++)
	{
		off[0] = lseek(fds[0], 0, SEEK_CUR);
		off[1] = lseek(fds[1], 0, SEEK_CUR);
		status = child_reads(fds, 2, 2, out);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "program %d: %s",
			run, status_str(status));
		for (int i = 0; i < 2; i++)
		{
			exp = first_lines(text[i] + off[i], 2);
			CHECK(strcmp(out[i], exp) == 0, "program %d, fd %c (offset %ld): "
				"printed %s, expected %s", run, 'a' + i, (long)off[i],
				show_str(out[i]), show_str(exp));
			free(exp);
			free(out[i]);
		}
	}
	close(fds[0]);
	close(fds[1]);
	free(text[0]);
	free(text[1]);
}

/* ----------------------------------------- writing while fds are open */

static void	two_pipes_written_between_calls(void)
{
	/* The test holds both write ends and writes between calls. */
	int	p[2];
	int	q[2];

	if (pipe(p) < 0 || pipe(q) < 0)
		exit(2);
	write_str(p[1], "p1\np2\n");
	write_str(q[1], "q");
	write_str(q[1], "1\n");
	expect_line(p[0], "p1\n");
	expect_line(q[0], "q1\n");
	write_str(q[1], "q2\nq3");
	expect_line(p[0], "p2\n");
	write_str(p[1], "p3\n");
	write_str(q[1], " end\n");
	expect_line(q[0], "q2\n");
	expect_line(p[0], "p3\n");
	expect_line(q[0], "q3 end\n");
	write_str(p[1], "p4");
	close(p[1]);
	close(q[1]);
	EXPECT_LINES(p[0], "p4");
	expect_line(q[0], NULL);
	close(p[0]);
	close(q[0]);
	CHECK_NO_LEAK("after EOF on both pipes");
}

static void	two_files_appended_while_open(void)
{
	/* (extra) Appends to 2 files while both are read, and after EOF. */
	char	pa[64];
	char	pb[64];
	int		a;
	int		b;

	make_tmp_file(pa, "a1\n");
	make_tmp_file(pb, "b1\nb2\n");
	a = open(pa, O_RDONLY);
	b = open(pb, O_RDONLY);
	expect_line(a, "a1\n");
	expect_line(b, "b1\n");
	append_str(pa, "a2\n");
	expect_line(a, "a2\n");
	expect_line(a, NULL);
	append_str(pb, "b3\n");
	expect_line(b, "b2\n");
	append_str(pa, "a3 after EOF\n");
	expect_line(b, "b3\n");
	expect_line(a, "a3 after EOF\n");
	expect_line(b, NULL);
	append_str(pb, "b4 after EOF");
	EXPECT_LINES(b, "b4 after EOF");
	expect_line(a, NULL);
	close(a);
	close(b);
	unlink(pa);
	unlink(pb);
	CHECK_NO_LEAK("after EOF on both files");
}

static void	same_path_opened_twice(void)
{
	/*
	** The same file opened twice: 2 fds, 2 independent offsets, so each
	** must return every line of the file.
	*/
	char	path[64];
	int		a;
	int		b;

	make_tmp_file(path, "same 1\nsame 2\nsame 3");
	a = open(path, O_RDONLY);
	b = open(path, O_RDONLY);
	expect_line(a, "same 1\n");
	expect_line(a, "same 2\n");
	expect_line(b, "same 1\n");
	expect_line(a, "same 3");
	expect_line(b, "same 2\n");
	expect_line(a, NULL);
	expect_line(b, "same 3");
	expect_line(b, NULL);
	close(a);
	close(b);
	unlink(path);
	CHECK_NO_LEAK("after EOF on both fds");
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
	run_case("every malloc fails in turn, 3 fds round robin",
		malloc_failures_round_robin);
	run_case("read x lines of 2 fds, stop, close: third fd unaffected",
		stop_early_on_two_fds);
	run_case("a program reads x lines from 2 fds and exits",
		child_reads_two_fds_and_exits);
	run_case("2 fds read by 8 programs in a row", several_programs_two_fds);
	run_case("2 pipes written between calls (fds stay open)",
		two_pipes_written_between_calls);
	run_case("(extra) 2 files appended while open and after EOF",
		two_files_appended_while_open);
	run_case("same file opened twice: 2 independent fds",
		same_path_opened_twice);
	return (finish("bonus"));
}
