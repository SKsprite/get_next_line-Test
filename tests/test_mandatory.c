#include "gnl_utils.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>

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

/*
** -v / -vv, after a wrong line from a call where a malloc or read() failure
** was injected: which one failed, the calls so far as a diff (the earlier
** ones matched `expected`), and how the last line differs.
*/
static void	explain_failed_call(const char **expected, size_t i,
				const char *line, int null_only)
{
	const char	*exp[8];
	const char	*got[8];

	if (verbosity() < 1)
		return ;
	if (*last_failure_info())
		report_note("injected: %s", last_failure_info());
	for (size_t k = 0; k < i && k < 7; k++)
	{
		exp[k] = expected[k];
		got[k] = expected[k];
	}
	exp[i] = null_only ? NULL : expected[i];
	got[i] = line;
	if (i < 7)
		diff_lines(exp, got, i + 1);
	explain_diff(exp[i], line);
}

static void	every_read_fails_in_turn(void)
{
	/* read() call 1, 2, 3, ... fails. Never crash, never leak. */
	const char	*content = "abc\ndefghij\nklmnopqrstuvwxyz\nlast";
	const char	*expected[] = {"abc\n", "defghij\n", "klmnopqrstuvwxyz\n",
		"last", NULL, NULL, NULL, NULL};
	char		*line;
	int			fd;
	int			triggered;

	for (long n = 1; n < 200; n++)
	{
		trace_clear();
		fd = open_str(content);
		fail_read_at(n);
		for (int calls = 0; calls < 20; calls++)
		{
			line = gnl(fd);
			if (read_failed())
			{
				CHECK(line == NULL, "read() call %ld returned -1 but "
					"get_next_line returned %s", n, show_str(line));
				if (line && calls < 7)
					explain_failed_call(expected, (size_t)calls, line, 1);
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
		trace_clear();
		fd = open_str(content);
		fail_malloc_at(n);
		for (size_t i = 0; i < 4; i++)
		{
			line = gnl(fd);
			if (malloc_failed())
			{
				CHECK(!line || strcmp(line, expected[i]) == 0,
					"malloc %ld failed: expected NULL (or %s), got %s%s",
					n, show_str(expected[i]), show_str(line),
					line && strcmp(line, expected[i]) != 0
					? " (when malloc fails, return NULL and free the stash, not "
					"a line built from what you have so far)" : "");
				if (line && strcmp(line, expected[i]) != 0)
					explain_failed_call(expected, i, line, 0);
				free(line);
				break ;
			}
			CHECK(line && strcmp(line, expected[i]) == 0,
				"expected %s, got %s", show_str(expected[i]), show_str(line));
			if (!line || strcmp(line, expected[i]) != 0)
				explain_failed_call(expected, i, line, 0);
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

static void	strict_malloc_failure_frees_stash(void)
{
	/*
	** (strict) After the call where malloc fails returns NULL, nothing may be
	** left allocated, even if the caller never calls get_next_line again.
	*/
	const char	*content = "first line\nsecond line is longer\n\nend";
	char		*line;
	int			fd;
	int			triggered;

	for (long n = 1; n < 500; n++)
	{
		trace_clear();
		fd = open_str(content);
		fail_malloc_at(n);
		for (int i = 0; i < 5; i++)
		{
			line = gnl(fd);
			if (malloc_failed())
			{
				CHECK(line || gnl_live_blocks() == 0, "malloc %ld failed and "
					"get_next_line returned NULL, but %zu block(s) are still "
					"allocated (stash not freed on error)", n, gnl_live_blocks());
				if (!line && gnl_live_blocks() && verbosity() >= 1)
				{
					report_note("injected: %s", last_failure_info());
					report_live_blocks();
				}
				free(line);
				break ;
			}
			if (!line)
				break ;
			free(line);
		}
		triggered = malloc_failed();
		fail_malloc_at(0);
		drain(fd, 100);
		close(fd);
		if (!triggered || g_case_failures)
			break ;
	}
}

/* -------------------------------------------- stopping early, exiting */

#define SIX_LINES "line 1\nline 2 is longer\n\nline 4\nline 5\nline 6 no newline"

static void	stop_early_then_close(void)
{
	/*
	** Read x lines, stop, close the fd. Further calls on the closed fd may
	** only return lines that were already buffered (the next ones, in
	** order), then NULL, and must free everything.
	*/
	const char	*lines[] = {"line 1\n", "line 2 is longer\n", "\n", "line 4\n",
		"line 5\n", "line 6 no newline"};
	char		*got;
	int			fd;
	int			next;

	for (int x = 0; x <= 6 && !g_case_failures; x++)
	{
		fd = open_str(SIX_LINES);
		for (int i = 0; i < x; i++)
			expect_line(fd, lines[i]);
		close(fd);
		next = x;
		while ((got = gnl(fd)) != NULL && !g_case_failures)
		{
			CHECK(next < 6 && strcmp(got, lines[next]) == 0, "stopped after %d "
				"line(s) and closed the fd: next call returned %s, expected %s "
				"or NULL", x, show_str(got), next < 6 ? show_str(lines[next])
				: "NULL");
			free(got);
			next++;
		}
		free(got);
		CHECK_NO_LEAK("after stopping early, closing and reaching NULL");
	}
	fd = open_str("fresh file\n");
	EXPECT_LINES(fd, "fresh file\n");
	close(fd);
}

static void	child_reads_some_and_exits(void)
{
	/*
	** A program reads x lines and exits without reaching EOF. It must not
	** crash, and it must have printed the right lines. (Under valgrind, the
	** child's lines must also all be freed: definite leaks fail the case.)
	*/
	int		fd;
	int		status;
	char	*out;
	char	*exp;

	for (int x = 1; x <= 6; x++)
	{
		fd = open_str(SIX_LINES);
		status = child_reads(&fd, 1, x, &out);
		exp = first_lines(SIX_LINES, x);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
			"program reading %d line(s): %s", x, status_str(status));
		CHECK(strcmp(out, exp) == 0, "program reading %d line(s) printed %s, "
			"expected %s", x, show_str(out), show_str(exp));
		free(out);
		free(exp);
		close(fd);
	}
}

static char	*numbered_lines(int n, int with_tail)
{
	char	*s;
	size_t	len;

	s = malloc((size_t)n * 48 + 32);
	len = 0;
	for (int i = 0; i < n; i++)
		len += (size_t)sprintf(s + len, "line %d%.*s\n", i, i % 23,
			"..........................");
	if (with_tail)
		strcpy(s + len, "no newline at the end");
	else
		s[len] = '\0';
	return (s);
}

static void	several_programs_same_fd_at(int fd, const char *content,
				const char *what)
{
	size_t	size;
	size_t	bs;
	off_t	off;
	off_t	after;
	size_t	limit;
	int		status;
	char	*out;
	char	*exp;

	size = strlen(content);
	bs = BUFFER_SIZE;
	for (int run = 1; run <= 8 && !g_case_failures; run++)
	{
		off = lseek(fd, 0, SEEK_CUR);
		status = child_reads(&fd, 1, 3, &out);
		after = lseek(fd, 0, SEEK_CUR);
		exp = first_lines(content + off, 3);
		CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
			"%s, program %d: %s", what, run, status_str(status));
		CHECK(strcmp(out, exp) == 0, "%s, program %d (fd offset was %ld): "
			"printed %s, expected %s", what, run, (long)off, show_str(out),
			show_str(exp));
		limit = (size_t)off + ((strlen(exp) + bs - 1) / bs) * bs;
		if (limit > size)
			limit = size;
		CHECK((size_t)after <= limit, "%s, program %d read the fd from offset "
			"%ld to %ld, but its 3 lines only needed up to %zu (it read more "
			"than it needed)", what, run, (long)off, (long)after, limit);
		free(out);
		free(exp);
	}
}

static void	several_programs_same_fd(void)
{
	/*
	** One fd shared by programs run one after the other (like
	** `exec 3< file; ./prog 3; ./prog 3`). Each program reads 3 lines and
	** exits. The lines it buffered but didn't return are lost with it, so
	** program k must print the 3 lines that start where the fd offset was.
	** With BUFFER_SIZE=1 nothing is lost.
	*/
	char	*content;
	int		fd;

	content = numbered_lines(30, 1);
	fd = open_str(content);
	several_programs_same_fd_at(fd, content, "regular file");
	close(fd);
	fd = open_str(content);
	dup2(fd, STDIN_FILENO);
	close(fd);
	several_programs_same_fd_at(STDIN_FILENO, content, "stdin");
	free(content);
}

/* ------------------------------------------ writing while the fd is open */

static void	pipe_written_between_calls(void)
{
	/*
	** The test keeps the write end of the pipe and writes between calls, so
	** the fd stays open the whole time (no EOF until the end).
	*/
	int	p[2];

	if (pipe(p) < 0)
		exit(2);
	write_str(p[1], "one\n");
	expect_line(p[0], "one\n");
	write_str(p[1], "tw");
	write_str(p[1], "o\nthree\n");
	expect_line(p[0], "two\n");
	expect_line(p[0], "three\n");
	write_str(p[1], "fo");
	write_str(p[1], "ur\n");
	expect_line(p[0], "four\n");
	write_str(p[1], "\n");
	expect_line(p[0], "\n");
	write_str(p[1], "last, no newline");
	close(p[1]);
	EXPECT_LINES(p[0], "last, no newline");
	close(p[0]);
	CHECK_NO_LEAK("after EOF");
}

static void	file_appended_while_open(void)
{
	/*
	** Another fd appends to the file while it is being read: before EOF the
	** new lines are simply part of the file; after EOF (NULL), the next call
	** returns what was added since (like `tail -f`).
	*/
	char	path[64];
	int		fd;

	make_tmp_file(path, "a\nb\n");
	fd = open(path, O_RDONLY);
	expect_line(fd, "a\n");
	append_str(path, "c\n");
	expect_line(fd, "b\n");
	expect_line(fd, "c\n");
	expect_line(fd, NULL);
	CHECK_NO_LEAK("at EOF");
	append_str(path, "d\npartial");
	expect_line(fd, "d\n");
	expect_line(fd, "partial");
	expect_line(fd, NULL);
	append_str(path, " more\n");
	EXPECT_LINES(fd, " more\n");
	close(fd);
	unlink(path);
	CHECK_NO_LEAK("after EOF");
}

static void	same_fd_read_and_write(void)
{
	/*
	** One O_RDWR fd: read to EOF, write new lines through the same fd, seek
	** back to them and read them; then seek to 0 and read it all again.
	*/
	char	path[64];
	int		fd;

	make_tmp_file(path, "x\ny\n");
	fd = open(path, O_RDWR);
	EXPECT_LINES(fd, "x\n", "y\n");
	write_str(fd, "z\nw\n");
	lseek(fd, -4, SEEK_CUR);
	EXPECT_LINES(fd, "z\n", "w\n");
	lseek(fd, 0, SEEK_SET);
	EXPECT_LINES(fd, "x\n", "y\n", "z\n", "w\n");
	close(fd);
	unlink(path);
	CHECK_NO_LEAK("after EOF");
}

/* ------------------------------------------------ programs and binaries */

static void	binary_file(void)
{
	/*
	** A compiled program: '\0' bytes, long runs without '\n'. The lines are
	** C strings, so what follows a '\0' can't be seen by the caller, but
	** get_next_line must not crash, hang or leak, and must end with NULL.
	*/
	const char	*paths[] = {"/bin/true", "/usr/bin/true", "/bin/ls"};
	int			fd;
	char		buf[4096];
	ssize_t		r;
	size_t		newlines;
	size_t		calls;
	char		*line;

	fd = -1;
	for (size_t i = 0; i < 3 && fd < 0; i++)
		fd = open(paths[i], O_RDONLY);
	if (fd < 0)
		skip_case("no /bin/true, /usr/bin/true or /bin/ls to read");
	newlines = 0;
	while ((r = read(fd, buf, sizeof(buf))) > 0)
		for (ssize_t i = 0; i < r; i++)
			newlines += (buf[i] == '\n');
	lseek(fd, 0, SEEK_SET);
	line = gnl(fd);
	CHECK(line && strncmp(line, "\x7f" "ELF", 4) == 0,
		"first line should start with \"\\x7fELF\", got %s", show_str(line));
	calls = 1;
	while (line && calls <= newlines + 2)
	{
		free(line);
		line = gnl(fd);
		calls++;
	}
	CHECK(line == NULL, "still returning lines after %zu calls, but the file "
		"has only %zu '\\n'", calls, newlines);
	free(line);
	expect_line(fd, NULL);
	close(fd);
	CHECK_NO_LEAK("after EOF");
}

static void	program_output_through_pipe(void)
{
	/* The output of a running program (sh), read while it is writing. */
	int		p[2];
	pid_t	pid;
	char	exp[64];
	char	*got;

	if (pipe(p) < 0 || (pid = fork()) < 0)
		exit(2);
	if (pid == 0)
	{
		close(p[0]);
		dup2(p[1], STDOUT_FILENO);
		close(p[1]);
		execl("/bin/sh", "sh", "-c", "i=0; while [ $i -lt 2000 ]; do "
			"echo \"output line $i\"; i=$((i+1)); done; printf 'done'",
			(char *)NULL);
		_exit(127);
	}
	close(p[1]);
	for (int i = 0; i < 2000 && !g_case_failures; i++)
	{
		sprintf(exp, "output line %d\n", i);
		got = gnl(p[0]);
		CHECK(got && strcmp(got, exp) == 0, "line %d: expected %s, got %s", i,
			show_str(exp), show_str(got));
		free(got);
	}
	EXPECT_LINES(p[0], "done");
	close(p[0]);
	waitpid(pid, NULL, 0);
	CHECK_NO_LEAK("after EOF");
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
	run_case("(strict) malloc fails -> NULL and the stash is freed at once",
		strict_malloc_failure_frees_stash);
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
	run_case("read x lines, stop, close: no leak, no stale lines",
		stop_early_then_close);
	run_case("a program reads x lines and exits", child_reads_some_and_exits);
	run_case("one fd read by 8 programs in a row (file and stdin)",
		several_programs_same_fd);
	run_case("pipe written between calls (fd stays open)",
		pipe_written_between_calls);
	run_case("(extra) file appended while open and after EOF (tail -f)",
		file_appended_while_open);
	run_case("(extra) same O_RDWR fd: read, write, seek back, read",
		same_fd_read_and_write);
	run_case("binary file (a compiled program): no crash, hang or leak",
		binary_file);
	run_case("output of a running program (sh) through a pipe",
		program_output_through_pipe);
#ifdef GNL_BONUS
	return (finish("mandatory (bonus files)"));
#else
	return (finish("mandatory"));
#endif
}
