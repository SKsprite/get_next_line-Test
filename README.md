# get_next_line tests

Tests for `get_next_line`, the mandatory part and the bonus part. The suite has two parts:

- **An interactive reader (`./test`)**: you give it file descriptors that you
  opened in your shell, and it prints every line `get_next_line` returns.
- **An automatic suite** (`make test`, `make bonus`, `make valgrind`, …). It
  compiles your files with many `BUFFER_SIZE` values and runs about 45
  test cases on them. Each case runs in its own process, so a crash or an
  infinite loop fails only that case.

The tests compile your `.c` files directly. They never change them.

## Requirements

- `cc` (clang or gcc) and `make`
- `valgrind` (optional), for `make valgrind` / `make valgrind_bonus`

## Layout

```
get_next_line/
├── get_next_line.c, get_next_line_utils.c, get_next_line.h
├── get_next_line_bonus.c, get_next_line_utils_bonus.c, get_next_line_bonus.h
└── gnl_test/
    ├── Makefile
    ├── main.c            <- the interactive reader (./test)
    ├── run_tests.sh      <- checks + builds and runs the suites per BUFFER_SIZE
    ├── test.h            <- CHECK macros
    ├── test_utils.c      <- runner: fork per case, timeout, crash detection
    ├── gnl_utils.c/.h    <- malloc/free/read wrappers, file & pipe helpers
    ├── tests/
    │   ├── test_mandatory.c   <- single-fd behaviour (mandatory part)
    │   ├── test_bonus.c       <- several fds at once (bonus part)
    │   └── test_bad_buffer.c  <- BUFFER_SIZE=0 and -1
    └── files/            <- sample text files for ./test
```

The tests look for your files in `..`. If they are somewhere else, pass
`GNL_DIR`:

```sh
make test GNL_DIR=/path/to/get_next_line
```

## Usage

Run these from inside `gnl_test/`:

| Command | What it does |
|---|---|
| `make` | Builds `./test` from the mandatory files (`BUFFER_SIZE=42`) |
| `make BUFFER_SIZE=1` | The same, with a different buffer size |
| `make main_bonus` | Builds `./test_bonus` from the `_bonus` files (several fds at once) |
| `make test` | Checks + mandatory tests, for every buffer size |
| `make bonus` | Everything in `make test`, then the checks, mandatory tests and multi-fd tests on the `_bonus` files |
| `make valgrind` | `make test` under valgrind |
| `make valgrind_bonus` | `make bonus` under valgrind |
| `make clean` / `make fclean` | Deletes `bin/` / also deletes `./test` and `./test_bonus` |

Environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `BUFFER_SIZES` | `1 2 3 7 42 1024 9999 10000000` (valgrind: `1 42 9999`) | Buffer sizes the suites are compiled with |
| `TEST_TIMEOUT` | `5` (valgrind: `60`) | Seconds before a case is killed |
| `GNL_DIR` | `..` | Where your files are |

For example, `BUFFER_SIZES="1 5" make test` runs a quick check.

### `make` or `run_tests.sh`?

`make test` and `make bonus` only call the script (`./run_tests.sh mandatory`
and `./run_tests.sh bonus`). The script first changes into its own folder.
So all of these run the same tests on the same files:

```sh
cd gnl_test && make test
make -C gnl_test test          # from your project folder
./gnl_test/run_tests.sh        # the mode defaults to mandatory
```

The differences:

| | `make` | `run_tests.sh` |
|---|---|---|
| Choosing the suite | target: `test`, `bonus`, `valgrind`, `valgrind_bonus` | argument: `mandatory` or `bonus` |
| Valgrind | `make valgrind` sets the valgrind command, the 60 s timeout and the smaller buffer sizes for you | you set `RUNNER="valgrind ..."` yourself |
| Settings | `make test BUFFER_SIZES="1 42"` | `BUFFER_SIZES="1 42" ./run_tests.sh` |
| When tests fail | adds a `make: *** ... Error 1` line | only the test output |

Both change into `gnl_test/` first, so a relative `GNL_DIR` is resolved from
there, not from the folder you typed the command in.
`GNL_DIR=../other_gnl` means `gnl_test/../other_gnl`.

## The interactive reader: `./test`

```
./test [-r] [-n count] [fd ...]
```

| Example | What it does |
|---|---|
| `./test` | Reads stdin (fd 0) until `get_next_line` returns `NULL` |
| `./test 3` | Reads fd 3 |
| `./test_bonus 3 4 5` | Reads fd 3, 4, 5, 3, 4, 5, … one line at a time (bonus) |
| `./test -n 2 3` | Stops after 2 lines |
| `./test -r 3` | Raw: prints each line exactly as returned, so you can `diff` it |

Each line is printed with its fd and number. `\n`, `\t`, `\r` and other
control bytes are shown in grey, so you can see whether the line really
ends with `\n`:

```
fd 3 |    1 | Hello, world!\n
fd 3 |    2 | This is line two.\n
fd 3 |    3 | \n
fd 3 |    4 | Line four after an empty line.\n
fd 3 |    5 | last line ends with a newline\n
fd 3 | NULL (after 5 lines)
```

### Opening file descriptors in your shell

`exec N< file` opens `file` on fd `N` in the current shell. Every program
you start afterwards inherits that fd, and `exec N<&-` closes it.

```sh
make
exec 3< files/basic.txt       # open fd 3
./test 3                      # read it with get_next_line
exec 3<&-                     # close fd 3
```

> **zsh vs bash:** zsh accepts only single-digit fds in this form (`3` to
> `9`). `exec 10< file` in zsh fails with `command not found: 10`. For bigger
> numbers in zsh, let the shell pick the fd:
>
> ```sh
> exec {fd}< files/basic.txt; ./test $fd; exec {fd}<&-
> ```
>
> In bash, `exec 10< file.txt; ./test 10; exec 10<&-` works as written.

More things to try:

```sh
# Several fds at once (bonus)
make main_bonus
exec 3< files/a.txt 4< files/b.txt 5< files/c.txt
./test_bonus 3 4 5
exec 3<&- 4<&- 5<&-

# stdin: a pipe, a redirect, or type yourself (Ctrl-D to end)
printf 'one\ntwo\nno newline' | ./test
./test < files/no_newline.txt
./test

# Compare with the real file (no output = identical)
./test -r < files/many_lines.txt | diff - files/many_lines.txt

# Invalid / closed fds must give NULL straight away
./test 42
./test -1

# The fd offset survives between runs, the static buffer does not
exec 3< files/many_lines.txt
./test -n 2 3       # line 1, line 2
./test -n 2 3       # BUFFER_SIZE=42 -> starts at "line 7", not "line 3"
exec 3<&-
```

The last example shows how `BUFFER_SIZE` affects reading. The first run
called `read(3, buf, 42)`, so the kernel moved fd 3 forward by 42 bytes.
Lines 3 to 6 were in that process's static buffer, and they were lost when
it exited. Rebuild with `make BUFFER_SIZE=1` and the second run starts
exactly at `line 3`.

Sample files in `files/`: `basic.txt`, `empty.txt`, `no_newline.txt`,
`only_newlines.txt`, `one_line.txt`, `long_line.txt` (50 000 chars),
`many_lines.txt` (10 000 lines), and `a.txt`, `b.txt`, `c.txt` for the bonus.

## What the suite checks

### 1. Checks (no tests run yet)

For the mandatory files, and also for the bonus files with `make bonus`:

- all 3 files exist, and the header declares `char *get_next_line(int fd)`
- builds with `cc -Wall -Wextra -Werror` **without** `-D BUFFER_SIZE` and
  **with** `-D BUFFER_SIZE=42` (the subject requires both)
- the only external functions are `read`, `malloc` and `free`, read from the
  compiled objects with `nm`. `lseek`, `printf`, `strlen` and the like are
  reported
- no global variables. File-scope `static` variables count as globals
- bonus: exactly **one** static variable

### 2. Mandatory tests (`tests/test_mandatory.c`)

Run once for each `BUFFER_SIZE`:

| Area | Cases |
|---|---|
| Basics | empty file, one line with/without `\n`, `"a"`, `"\n"`, `"a\nb"`, only newlines, several lines, empty lines at the start/middle/end |
| Content | tabs, `\r\n`, UTF-8, high bytes, escape sequences, every byte value 1..255 |
| Buffer edges | lines of `BUFFER_SIZE-1`, `BUFFER_SIZE` and `BUFFER_SIZE+1` chars, a file of exactly `BUFFER_SIZE` bytes, `\n` as the first/last byte of a `read()` chunk |
| Size | 20 000-char line, long line then short lines, 3 000 lines, two files read one after the other |
| Bad fds | `-1`, `-42`, closed fd, fd that was never opened, a directory (`read` gives `EISDIR`), a write-only fd, then a valid file afterwards |
| Errors | `read()` failing mid-file must return `NULL` and free the leftover; **every** `read()` call failing in turn; **every** `malloc()` failing in turn (no crash, no leak, a clean state for the next file); a closed fd number reused by a new file must not return leftovers from the old one |
| stdin / pipes | stdin redirected from a file and from a pipe, data arriving in small delayed chunks, 10 000 lines through a pipe (more than the 64 KiB a pipe holds) |
| Economy | **returns a line without waiting for EOF**: the writer sends `"first\n"` and then waits, and if `get_next_line` calls `read()` again it blocks and the case times out. **Does not read the whole file**: after the first call, `read()` may only have returned the chunks needed to reach the first `\n` |
| Ownership | each returned line is a separate buffer: you can modify it or free it in any order |

Every case also checks that **nothing allocated by `get_next_line` is
still allocated** once it has returned `NULL`. That includes a static
stash you forgot to free at EOF or after an error. Valgrind only calls that
"still reachable", but this test reports it as a leak.

### 3. Invalid buffer sizes (`tests/test_bad_buffer.c`)

Built with `-D BUFFER_SIZE=0` and `-D BUFFER_SIZE=-1`. `get_next_line`
must return `NULL`, must not crash or leak, and must never call `read()`
with a negative size.

### 4. Bonus tests (`tests/test_bonus.c`, with `make bonus`)

The **whole mandatory suite** runs again on the `_bonus` files. Then:

| Case | What it checks |
|---|---|
| 3 fds round robin | the subject's example: fd 3, 4, 5, 3, 4, 5, … |
| different lengths | one fd reaches EOF early, the others carry on |
| leftovers per fd | several lines per `read()` chunk on each fd, so each fd's leftover must stay with that fd |
| same content twice | two fds on identical files, read at different speeds |
| file + pipe + stdin | different fd kinds mixed |
| invalid fds in between | `-1`, closed fds and fd 4242 between good calls must not reset the good fds |
| error on one fd | a directory and a failing `read()` on one fd, the other fd is unaffected |
| close one mid-read | its fd number is reused by a new file, which starts clean |
| 100 fds | 100 files open at the same time |
| high fd numbers | fd 1023 works (a `stash[FOPEN_MAX]` table of 16 is too small); fd 1024, 100000 and `INT_MAX` return `NULL` without crashing |
| malloc failure on one fd | the other fd keeps its state, nothing leaks |
| two slow pipes | short reads on two fds, interleaved |

## How it works

Each test binary is linked with
`-Wl,--wrap=malloc,--wrap=free,--wrap=read`. Every `malloc`, `free` and
`read` call in your files then goes through `gnl_utils.c`, but only
while the test is inside `get_next_line`. This lets the tests:

- track every block your code allocates until it is freed (leak checks
  without valgrind),
- make the N-th `malloc` return `NULL`, or the N-th `read` return `-1`,
- count how many bytes `read()` returned (the "doesn't read the whole file"
  check).

Your code keeps calling plain `malloc`/`read`/`free` and needs no changes.

## Reading the output

```
mandatory tests (BUFFER_SIZE=1)
  [OK] empty file / empty pipe -> NULL
  [KO] one line without \n at EOF
      -> tests/test_mandatory.c:101: fd 3: expected "hello world", got NULL
  [KO] read() fails mid-file -> NULL, leftovers freed
      -> tests/test_mandatory.c:..: right after the read() error: 1 block(s) allocated by get_next_line were never freed (static stash not freed at EOF / on error?)
  [KO] 20000-char line -> SEGFAULT
mandatory: 28/31 passed
```

- **[OK]**: the case passed.
- **[KO]**: the case failed. The `->` lines give the expected and the
  actual line. Non-printable bytes appear as `\xNN`.
- **-> SEGFAULT / ABORT**: your code crashed. ABORT usually means a double
  free or heap corruption, e.g. a `malloc` one byte too small.
  A crash only at `BUFFER_SIZE=10000000` usually means
  `char buf[BUFFER_SIZE + 1]` on the stack. 10 MB is bigger than the
  default 8 MB stack, so `malloc` the read buffer instead.
- **-> TIMEOUT**: the case took more than `TEST_TIMEOUT` seconds. This means
  an infinite loop, or a `read()` that blocks because your code kept reading
  after it already had a full line (see "Economy" above). With
  `BUFFER_SIZE=1` it can also mean the code is very slow, e.g. re-scanning
  the whole stash from the start on every `read()`.
- **-> valgrind reported memory errors**: (valgrind runs only) the valgrind
  report printed just above the KO line shows the file and line.
- **[BUILD FAIL]**: your files don't compile with the tests. The compiler
  errors are shown, sometimes with a hint, e.g. for a `BUFFER_SIZE` that
  is defined without `#ifndef`.

The summary at the end lists each suite by name, with the buffer size in
brackets (`mandatory[42]`). `make test` / `make bonus` exit non-zero if
anything failed.

## Tips

- Start with `BUFFER_SIZES="42" make test`, then run the full range. Bugs
  at `BUFFER_SIZE=1` or `2` are usually in how leftovers are joined. Bugs
  at very large sizes usually mean a stack buffer.
- Your header needs a default that `-D` can override:

  ```c
  # ifndef BUFFER_SIZE
  #  define BUFFER_SIZE 42
  # endif
  ```

  A bare `# define BUFFER_SIZE 4096` breaks the build when the evaluator
  compiles with `-D BUFFER_SIZE=n`.
- Run `make valgrind_bonus` even when `make bonus` passes. Valgrind finds
  reads one byte past the end of a buffer and use of uninitialised memory,
  which often don't crash in a normal run.
- To add a case, write another `static void` function in
  `tests/test_*.c` and register it in `main()` with
  `run_case("description", fn)`. The helpers in `gnl_utils.h` (`open_str`,
  `open_pipe_chunks`, `EXPECT_LINES`, `CHECK_NO_LEAK`, `fail_read_at`, …) keep
  cases short.

## Credits

The test suite was generated with AI (Claude, through Claude Code), using
the structure of the libft tests in `Milestone0/libft Test`. It is used to
check get_next_line and is not part of the submitted project.
