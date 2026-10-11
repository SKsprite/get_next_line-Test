#include "gnl_utils.h"

/*
** Compiled with -D BUFFER_SIZE=0 and -D BUFFER_SIZE=-1. There is nothing
** sensible to read, so get_next_line must return NULL without crashing,
** looping forever or calling read() with a huge size.
*/
static void	returns_null(void)
{
	int		fd;
	char	*line;

	fd = open_str("some content\nmore\n");
	line = gnl(fd);
	CHECK(line == NULL, "expected NULL, got %s", show_str(line));
	free(line);
	line = gnl(fd);
	CHECK(line == NULL, "second call: expected NULL, got %s", show_str(line));
	free(line);
	close(fd);
	CHECK(!bad_read_size(), "read() was called with a negative size "
		"(missing BUFFER_SIZE <= 0 check?)");
	CHECK_NO_LEAK("after returning NULL");
}

int	main(void)
{
	printf("invalid BUFFER_SIZE (BUFFER_SIZE=%ld)\n", (long)BUFFER_SIZE);
	run_case("returns NULL, no crash, no leak", returns_null);
	return (finish("invalid BUFFER_SIZE"));
}
