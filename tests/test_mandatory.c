#include "gnl_utils.h"
#include <fcntl.h>
#include <sys/stat.h>

/*
** Splits `content` into the lines get_next_line must return: each line ends
** right after a '\n', and the last one may have no '\n'.
*/
static char	**split_lines(const char *content, size_t *count)
{
	char		**lines;
	size_t		n;
	size_t		i;
	const char	*start;

	n = 0;
	for (i = 0; content[i]; i++)
		if (content[i] == '\n' || !content[i + 1])
			n++;
	lines = malloc((n + 1) * sizeof(*lines));
	n = 0;
	start = content;
	for (i = 0; content[i]; i++)
	{
		if (content[i] == '\n' || !content[i + 1])
		{
			lines[n] = strndup(start, (size_t)(content + i + 1 - start));
			start = content + i + 1;
			n++;
		}
	}
	lines[n] = NULL;
	*count = n;
	return (lines);
}

static void	free_lines(char **lines)
{
	for (size_t i = 0; lines[i]; i++)
		free(lines[i]);
	free(lines);
}

/* Reads `content` from a regular file AND from a pipe, then checks leaks. */
#define CHECK_CONTENT(content) check_content_at(__FILE__, __LINE__, content)

static void	check_content_at(const char *f, int l, const char *content)
{
	char	**lines;
	size_t	n;
	int		fd;

	lines = split_lines(content, &n);
	fd = open_str(content);
	expect_lines_at(f, l, fd, (const char **)lines, n);
	close(fd);
	CHECK_NO_LEAK("regular file, after EOF");
	if (g_case_failures == 0)
	{
		fd = open_pipe_str(content);
		expect_lines_at(f, l, fd, (const char **)lines, n);
		close(fd);
		CHECK_NO_LEAK("pipe, after EOF");
	}
	free_lines(lines);
}

/* ---------------------------------------------------------------- basics */

static void	empty_file(void)
{
	int	fd;

	fd = open_str("");
	expect_line(fd, NULL);
	expect_line(fd, NULL);
	close(fd);
	CHECK_NO_LEAK("empty file");
	fd = open_pipe_str("");
	expect_line(fd, NULL);
	close(fd);
	CHECK_NO_LEAK("empty pipe");
}

static void	one_line_newline(void)
{
	int	fd;

	fd = open_str("hello world\n");
	EXPECT_LINES(fd, "hello world\n");
	close(fd);
	CHECK_CONTENT("hello world\n");
}

static void	one_line_no_newline(void)
{
	int	fd;

	fd = open_str("hello world");
	EXPECT_LINES(fd, "hello world");
	close(fd);
	CHECK_CONTENT("hello world");
}

static void	single_chars(void)
{
	CHECK_CONTENT("a");
	CHECK_CONTENT("a\n");
	CHECK_CONTENT("\n");
	CHECK_CONTENT("a\nb");
	CHECK_CONTENT("a\nb\n");
}

static void	only_newlines(void)
{
	int	fd;

	fd = open_str("\n\n\n\n");
	EXPECT_LINES(fd, "\n", "\n", "\n", "\n");
	close(fd);
	CHECK_CONTENT("\n\n\n\n");
	CHECK_CONTENT("\n\n\n\nx");
}

static void	several_lines(void)
{
	int	fd;

	fd = open_str("first line\nsecond\nthird line here\n");
	EXPECT_LINES(fd, "first line\n", "second\n", "third line here\n");
	close(fd);
	fd = open_str("first line\nsecond\nlast without newline");
	EXPECT_LINES(fd, "first line\n", "second\n", "last without newline");
	close(fd);
	CHECK_NO_LEAK("after EOF");
}

static void	empty_lines_between(void)
{
	CHECK_CONTENT("a\n\nb\n\n\nc\n");
	CHECK_CONTENT("\n\nstarts with blank lines\n");
	CHECK_CONTENT("ends with blank lines\n\n\n");
}

static void	special_characters(void)
{
	CHECK_CONTENT("tab\there\n  spaces  \n\r\n");
	CHECK_CONTENT("windows\r\nline\r\nendings\r\n");
	CHECK_CONTENT("caf\xc3\xa9 \xe2\x82\xac 42 \xf0\x9f\x98\x80\n\xff\xfe\x80\n");
	CHECK_CONTENT("\x01\x02\x03\x7f\n\x1b[31mred\x1b[0m\n");
}

static void	every_byte_value(void)
{
	char	content[600];
	size_t	n;

	n = 0;
	for (int c = 1; c < 256; c++)
	{
		if (c == '\n')
			continue ;
		content[n++] = (char)c;
		if (c % 64 == 0)
			content[n++] = '\n';
	}
	content[n] = '\0';
	CHECK_CONTENT(content);
}

/* -------------------------------------------------- BUFFER_SIZE boundaries */

static void	lines_around_buffer_size(void)
{
	size_t	bs;
	char	*a;
	char	*b;
	char	*c;
	char	*content;
	int		fd;

	bs = BUFFER_SIZE;
	a = repeat_char('a', bs - 1, 1);
	b = repeat_char('b', bs, 1);
	c = repeat_char('c', bs + 1, 1);
	content = malloc(strlen(a) + strlen(b) + strlen(c) + 1);
	strcpy(content, a);
	strcat(content, b);
	strcat(content, c);
	fd = open_str(content);
	EXPECT_LINES(fd, a, b, c);
	close(fd);
	CHECK_NO_LEAK("after EOF");
	free(a);
	free(b);
	free(c);
	free(content);
}

static void	file_exactly_buffer_size(void)
{
	size_t	bs;
	char	*s;
	int		fd;

	bs = BUFFER_SIZE;
	s = repeat_char('x', bs, 0);
	fd = open_str(s);
	EXPECT_LINES(fd, s);
	close(fd);
	free(s);
	if (bs > 1)
	{
		s = repeat_char('y', bs - 1, 1);
		fd = open_str(s);
		EXPECT_LINES(fd, s);
		close(fd);
		free(s);
	}
	CHECK_NO_LEAK("after EOF");
}

static void	newline_at_chunk_edges(void)
{
	/* '\n' as the first / last byte of a read() chunk. */
	size_t	bs;
	char	*first;
	char	*content;
	int		fd;

	bs = BUFFER_SIZE;
	if (bs > 100000)
		bs = 100000;
	first = repeat_char('e', bs - 1, 1);
	content = malloc(strlen(first) + 32);
	strcpy(content, first);
	strcat(content, "\nnext\n");
	fd = open_str(content);
	EXPECT_LINES(fd, first, "\n", "next\n");
	close(fd);
	CHECK_NO_LEAK("after EOF");
	free(first);
	free(content);
}

/* ------------------------------------------------------------ big inputs */

static void	long_line(void)
{
	char	*s;
	int		fd;

	s = repeat_char('L', 20000, 0);
	fd = open_str(s);
	EXPECT_LINES(fd, s);
	close(fd);
	free(s);
	s = repeat_char('M', 20000, 1);
	fd = open_mem(s, strlen(s));
	EXPECT_LINES(fd, s);
	close(fd);
	free(s);
	CHECK_NO_LEAK("after EOF");
}

static void	long_line_then_short_lines(void)
{
	char	*s;
	char	*content;
	int		fd;

	s = repeat_char('Z', 15000, 1);
	content = malloc(strlen(s) + 64);
	strcpy(content, s);
	strcat(content, "short\n\nend");
	fd = open_str(content);
	EXPECT_LINES(fd, s, "short\n", "\n", "end");
	close(fd);
	free(s);
	free(content);
	CHECK_NO_LEAK("after EOF");
}

static void	many_lines(void)
{
	size_t	cap;
	size_t	len;
	char	*content;
	char	line[256];
	char	*got;
	int		fd;

	cap = 3000 * 120;
	content = malloc(cap);
	len = 0;
	for (int i = 0; i < 3000; i++)
		len += (size_t)sprintf(content + len, "line %d: %.*s\n", i, i % 97,
				"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghij"
				"klmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
	fd = open_mem(content, len);
	for (int i = 0; i < 3000 && g_case_failures == 0; i++)
	{
		sprintf(line, "line %d: %.*s\n", i, i % 97,
			"abcdefghijklmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyzabcdefghij"
			"klmnopqrstuvwxyzabcdefghijklmnopqrstuvwxyz");
		got = gnl(fd);
		CHECK(got && strcmp(got, line) == 0, "line %d: expected %s, got %s",
			i, show_str(line), show_str(got));
		free(got);
	}
	expect_line(fd, NULL);
	close(fd);
	free(content);
	CHECK_NO_LEAK("after EOF");
}

static void	two_files_one_after_another(void)
{
	int	fd;

	fd = open_str("file A line 1\nfile A line 2\n");
	EXPECT_LINES(fd, "file A line 1\n", "file A line 2\n");
	close(fd);
	fd = open_str("file B line 1\nfile B end");
	EXPECT_LINES(fd, "file B line 1\n", "file B end");
	close(fd);
	CHECK_NO_LEAK("after EOF");
}

/* -------------------------------------------------- invalid fds and errors */

static void	invalid_fds(void)
{
	int	fd;

	expect_line(-1, NULL);
	expect_line(-42, NULL);
	fd = open_str("x\n");
	close(fd);
	expect_line(fd, NULL);
	expect_line(4242, NULL);
	CHECK_NO_LEAK("after invalid fds");
}

static void	directory_fd(void)
{
	int	fd;

	fd = open(".", O_RDONLY);
	CHECK(fd >= 0, "could not open the current directory");
	expect_line(fd, NULL);
	expect_line(fd, NULL);
	close(fd);
	CHECK_NO_LEAK("after read() failed with EISDIR");
}

static void	write_only_fd(void)
{
	int	fd;

	fd = open("/dev/null", O_WRONLY);
	expect_line(fd, NULL);
	close(fd);
	CHECK_NO_LEAK("after read() failed with EBADF");
}

static void	valid_file_after_invalid_fd(void)
{
	int	fd;

	expect_line(-1, NULL);
	expect_line(4242, NULL);
	fd = open_str("still works\n");
	EXPECT_LINES(fd, "still works\n");
	close(fd);
	CHECK_NO_LEAK("after EOF");
}

static void	read_error_mid_file(void)
{
	/*
	** The first line comes back normally; the next read() fails. That call
	** must return NULL and free whatever was left over.
	*/
	int		fd;
	char	*line;

	fd = open_str("line one\nline two");
	expect_line(fd, "line one\n");
	fail_read_at(1);
	line = gnl(fd);
	CHECK(read_failed(), "get_next_line never called read() for line two");
	CHECK(line == NULL, "read() returned -1 but got %s (must return NULL)",
		show_str(line));
	free(line);
	fail_read_at(0);
	close(fd);
	CHECK_NO_LEAK("right after the read() error");
}

static void	every_read_fails_in_turn(void)
{
	/* read() call 1, 2, 3, ... fails. Never crash, never leak. */
	const char	*content = "abc\ndefghij\nklmnopqrstuvwxyz\nlast";
	char		*line;
	int			fd;
	int			triggered;

	for (long n = 1; n < 200; n++)
	{
		fd = open_str(content);
		fail_read_at(n);
		for (int calls = 0; calls < 20; calls++)
		{
			line = gnl(fd);
			if (read_failed())
			{
				CHECK(line == NULL, "read() call %ld returned -1 but "
					"get_next_line returned %s", n, show_str(line));
				free(line);
				break ;
			}
			if (!line)
				break ;
			free(line);
		}
		triggered = read_failed();
		fail_read_at(0);
		drain(fd, 100);
		close(fd);
		CHECK_NO_LEAK("after a failed read()");
		if (!triggered || g_case_failures)
			break ;
	}
	/* State must be clean for the next file. */
	fd = open_str("fresh file\n");
	EXPECT_LINES(fd, "fresh file\n");
	close(fd);
}

static void	every_malloc_fails_in_turn(void)
{
	/*
	** malloc 1, 2, 3, ... returns NULL. The call where it happens must
	** return NULL (or still the right line), never crash, never leak.
	*/
	const char	*content = "first line\nsecond line is longer\n\nend";
	const char	*expected[] = {"first line\n", "second line is longer\n",
		"\n", "end"};
	char		*line;
	int			fd;
	int			triggered;

	for (long n = 1; n < 500; n++)
	{
		fd = open_str(content);
		fail_malloc_at(n);
		for (size_t i = 0; i < 4; i++)
		{
			line = gnl(fd);
			if (malloc_failed())
			{
				CHECK(!line || strcmp(line, expected[i]) == 0,
					"malloc %ld failed: expected NULL (or %s), got %s",
					n, show_str(expected[i]), show_str(line));
				free(line);
				break ;
			}
			CHECK(line && strcmp(line, expected[i]) == 0,
				"expected %s, got %s", show_str(expected[i]), show_str(line));
			if (!line)
				break ;
			free(line);
		}
		triggered = malloc_failed();
		fail_malloc_at(0);
		drain(fd, 100);
		close(fd);
		CHECK_NO_LEAK("after a failed malloc()");
		if (!triggered || g_case_failures)
			break ;
	}
	fd = open_str("fresh file\n");
	EXPECT_LINES(fd, "fresh file\n");
	close(fd);
}

static void	reused_fd_number_starts_clean(void)
{
	/*
	** Read one line of A, close A, call get_next_line on the dead fd until
	** it gives up, then open B (the kernel reuses the same fd number).
	** No leftover of A may appear in B.
	*/
	int	fd;
	int	fd2;

	fd = open_str("A1\nA2\nA3 without newline");
	expect_line(fd, "A1\n");
	close(fd);
	drain(fd, 100);
	fd2 = open_str("B1\nB2\n");
	if (fd2 != fd)
	{
		dup2(fd2, fd);
		close(fd2);
	}
	EXPECT_LINES(fd, "B1\n", "B2\n");
	close(fd);
	CHECK_NO_LEAK("after EOF");
}

/* ------------------------------------------------------- stdin and pipes */

static void	stdin_from_file(void)
{
	int	fd;

	fd = open_str("from stdin 1\nfrom stdin 2\nno newline");
	dup2(fd, STDIN_FILENO);
	close(fd);
	EXPECT_LINES(STDIN_FILENO, "from stdin 1\n", "from stdin 2\n",
		"no newline");
	CHECK_NO_LEAK("after EOF");
}

static void	stdin_from_pipe(void)
{
	int	fd;

	fd = open_pipe_str("piped 1\npiped 2\n");
	dup2(fd, STDIN_FILENO);
	close(fd);
	EXPECT_LINES(STDIN_FILENO, "piped 1\n", "piped 2\n");
	CHECK_NO_LEAK("after EOF");
}

static void	pipe_chunks_arrive_slowly(void)
{
	/* read() returns short counts, splitting lines at odd places. */
	const char	*chunks[] = {"hel", "lo\nwor", "ld", "\n", "\n!", "!!"};
	int			fd;

	fd = open_pipe_chunks(chunks, 6, 30000);
	EXPECT_LINES(fd, "hello\n", "world\n", "\n", "!!!");
	close(fd);
	CHECK_NO_LEAK("after EOF");
}

static void	big_pipe(void)
{
	/* More than the 64 KiB a pipe holds, so the writer blocks in between. */
	char	*content;
	size_t	len;
	char	expect[64];
	char	*got;
	int		fd;

	content = malloc(10000 * 32);
	len = 0;
	for (int i = 0; i < 10000; i++)
		len += (size_t)sprintf(content + len, "pipe line number %05d\n", i);
	fd = open_pipe_mem(content, len);
	free(content);
	for (int i = 0; i < 10000 && g_case_failures == 0; i++)
	{
		sprintf(expect, "pipe line number %05d\n", i);
		got = gnl(fd);
		CHECK(got && strcmp(got, expect) == 0, "line %d: expected %s, got %s",
			i, show_str(expect), show_str(got));
		free(got);
	}
	expect_line(fd, NULL);
	close(fd);
	CHECK_NO_LEAK("after EOF");
}

static void	returns_without_waiting_for_eof(void)
{
	/*
	** The writer sends "first\n" and then waits. get_next_line must return
	** that line right away instead of calling read() again (which would
	** block forever -> TIMEOUT). This is how a terminal on stdin behaves.
	*/
	int		data[2];
	int		go[2];
	pid_t	pid;
	char	c;

	if (pipe(data) < 0 || pipe(go) < 0 || (pid = fork()) < 0)
		exit(2);
	if (pid == 0)
	{
		close(data[0]);
		close(go[1]);
		write(data[1], "first\n", 6);
		if (read(go[0], &c, 1) == 1)
			write(data[1], "second\nthird", 12);
		_exit(0);
	}
	close(data[1]);
	close(go[0]);
	expect_line(data[0], "first\n");
	write(go[1], "x", 1);
	close(go[1]);
	EXPECT_LINES(data[0], "second\n", "third");
	close(data[0]);
	CHECK_NO_LEAK("after EOF");
}

/* ---------------------------------------------------------- read economy */

static void	does_not_read_whole_file(void)
{
	/*
	** Subject: "Don't read the whole file and then process each line."
	** After the first call, read() may only have returned the chunks needed
	** to find the first '\n'.
	*/
	char	*content;
	size_t	len;
	size_t	limit;
	size_t	bs;
	int		fd;

	content = malloc(200 * 64 + 1);
	len = 0;
	for (int i = 0; i < 200; i++)
		len += (size_t)sprintf(content + len, "this is line %03d of the file\n", i);
	fd = open_mem(content, len);
	reset_counters();
	expect_line(fd, "this is line 000 of the file\n");
	bs = BUFFER_SIZE;
	limit = ((29 + bs - 1) / bs) * bs;
	CHECK(bytes_read() <= limit, "first line is 29 bytes, BUFFER_SIZE=%zu: "
		"read() should return at most %zu bytes, but %zu were read",
		bs, limit, bytes_read());
	drain(fd, 1000);
	close(fd);
	free(content);
	CHECK_NO_LEAK("after EOF");
}

/* ------------------------------------------------------- returned memory */

static void	lines_are_independent(void)
{
	/* The caller owns each line: modifying or freeing it must be safe. */
	char	*l1;
	char	*l2;
	char	*l3;
	int		fd;

	fd = open_str("one\ntwo\nthree\n");
	l1 = gnl(fd);
	l2 = gnl(fd);
	CHECK(l1 && l2 && l1 != l2, "two calls returned the same pointer");
	if (l1)
		memset(l1, 'X', strlen(l1));
	l3 = gnl(fd);
	CHECK(l2 && strcmp(l2, "two\n") == 0, "line 2: got %s", show_str(l2));
	CHECK(l3 && strcmp(l3, "three\n") == 0, "line 3: got %s", show_str(l3));
	free(l3);
	free(l1);
	free(l2);
	expect_line(fd, NULL);
	close(fd);
	CHECK_NO_LEAK("after EOF");
}

int	main(void)
{
#ifdef GNL_BONUS
	printf("mandatory tests on the bonus files (BUFFER_SIZE=%ld)\n",
		(long)BUFFER_SIZE);
#else
	printf("mandatory tests (BUFFER_SIZE=%ld)\n", (long)BUFFER_SIZE);
#endif
	run_case("empty file / empty pipe -> NULL", empty_file);
	run_case("one line ending with \\n", one_line_newline);
	run_case("one line without \\n at EOF", one_line_no_newline);
	run_case("single characters (\"a\", \"\\n\", \"a\\nb\")", single_chars);
	run_case("only newlines", only_newlines);
	run_case("several lines (with / without final \\n)", several_lines);
	run_case("empty lines in between, at start, at end", empty_lines_between);
	run_case("tabs, \\r\\n, UTF-8, high bytes, escapes", special_characters);
	run_case("every byte value 1..255", every_byte_value);
	run_case("lines of BUFFER_SIZE-1, BUFFER_SIZE, BUFFER_SIZE+1 chars",
		lines_around_buffer_size);
	run_case("file of exactly BUFFER_SIZE bytes", file_exactly_buffer_size);
	run_case("'\\n' at the edges of a read() chunk", newline_at_chunk_edges);
	run_case("20000-char line", long_line);
	run_case("long line followed by short lines", long_line_then_short_lines);
	run_case("3000 lines of varying length", many_lines);
	run_case("two files read one after the other", two_files_one_after_another);
	run_case("invalid fds (-1, closed, never opened) -> NULL", invalid_fds);
	run_case("directory fd (read fails) -> NULL", directory_fd);
	run_case("write-only fd (read fails) -> NULL", write_only_fd);
	run_case("valid file after invalid fds", valid_file_after_invalid_fd);
	run_case("read() fails mid-file -> NULL, leftovers freed",
		read_error_mid_file);
	run_case("every read() call fails in turn: no crash, no leak",
		every_read_fails_in_turn);
	run_case("every malloc() fails in turn: no crash, no leak",
		every_malloc_fails_in_turn);
	run_case("closed fd number reused by a new file starts clean",
		reused_fd_number_starts_clean);
	run_case("stdin redirected from a file", stdin_from_file);
	run_case("stdin redirected from a pipe", stdin_from_pipe);
	run_case("pipe: data arrives in small delayed chunks",
		pipe_chunks_arrive_slowly);
	run_case("pipe: 10000 lines (> 64 KiB pipe buffer)", big_pipe);
	run_case("returns a line without waiting for EOF (no extra read)",
		returns_without_waiting_for_eof);
	run_case("does not read the whole file on the first call",
		does_not_read_whole_file);
	run_case("returned lines are separate, caller-owned buffers",
		lines_are_independent);
#ifdef GNL_BONUS
	return (finish("mandatory (bonus files)"));
#else
	return (finish("mandatory"));
#endif
}
