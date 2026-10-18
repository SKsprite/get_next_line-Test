# get_next_line tests

Tests for `get_next_line`, the mandatory part and the bonus part. The suite has three parts:

- **An interactive reader (`./test`)**: you give it file descriptors that you
  opened in your shell, and it prints every line `get_next_line` returns.
- **C tests** (`tests/*.c`): your files are compiled with many `BUFFER_SIZE`
  values and run through 40 mandatory and 19 bonus cases. Each case runs
  in its own process, so a crash or an infinite loop fails only that case.
- **Program tests** (in `run_tests.sh`): the reader is built from your files
  and run from the shell, the way an evaluator would: fds opened with
  `exec 3< file`, several programs reading the same fd one after the other,
  a file appended between two programs, a slow writer on a pipe, a binary
  file.

`make test`, `make bonus` and `make valgrind` run the C tests and the program
tests together.

The tests compile your `.c` files directly. They never change them.

## Requirements

- `cc` (clang or gcc) and `make`
- `valgrind` (optional), for `make valgrind` / `make valgrind_bonus`

## Layout

```
get_next_line/
├── get_next_line.c, get_next_line_utils.c, get_next_line.h
├── get_next_line_bonus.c, get_next_line_utils_bonus.c, get_next_line_bonus.h
└── get_next_line-Test/
    ├── Makefile
    ├── main.c            <- the interactive reader (./test)
    ├── run_tests.sh      <- checks, C suites and program tests per BUFFER_SIZE
    ├── test.h            <- CHECK macros
    ├── test_utils.c      <- runner: fork per case, timeout, crash detection
    ├── gnl_utils.c/.h    <- malloc/free/read wrappers, file & pipe helpers,
    │                        child_reads() (a program that reads x lines, exits)
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

Run these from inside `get_next_line-Test/`:

| Command | What it does |
|---|---|
| `make` | Builds `./test` from the mandatory files (`BUFFER_SIZE=42`) |
| `make BUFFER_SIZE=1` | The same, with a different buffer size |
| `make main_bonus` | Builds `./test_bonus` from the `_bonus` files (several fds at once) |
| `make test` (or `make mandatory`) | Checks, mandatory C tests and program tests, for every buffer size |
| `make bonus` | Everything in `make test`, then the same on the `_bonus` files, plus the multi-fd tests |
| `make bonus_only` | Only the `_bonus` files: their checks, the mandatory tests, the multi-fd tests and the program tests on them (skips the mandatory files) |
| `make program` | Only the program tests (mandatory files) |
| `make valgrind` | `make test` under valgrind |
| `make valgrind_bonus` | `make bonus` under valgrind |
| `make valgrind_bonus_only` | `make bonus_only` under valgrind |
| `make valgrind_program` | `make program` under valgrind |
| `make clean` / `make fclean` | Deletes `bin/` / also deletes `./test` and `./test_bonus` |
| `make remove` | Deletes the whole tester folder (do this before you submit). Afterwards, `cd ..` |

Environment variables:

| Variable | Default | Meaning |
|---|---|---|
| `BUFFER_SIZES` | `default 1 2 3 7 42 1024 9999 10000000` (valgrind: `1 42 9999`) | Buffer sizes the suites are compiled with. `default` compiles **without** `-D BUFFER_SIZE`, so your header's value is used |
| `TEST_TIMEOUT` | `5` (valgrind: `60`) | Seconds before a case is killed |
| `V` | `0` | `1` or `2`: the same as `-v` / `-vv` (see [Verbose output](#verbose-output--v--vv)) |
| `GNL_DIR` | `..` | Where your files are |

For example, `BUFFER_SIZES="1 5" make test` runs a quick check.

### `make` or `run_tests.sh`?

`make test` and `make bonus` only call the script (`./run_tests.sh mandatory`
and `./run_tests.sh bonus`). The script first changes into its own folder.
So all of these run the same tests on the same files:

```sh
cd get_next_line-Test && make test
make -C get_next_line-Test test   # from your project folder
./get_next_line-Test/run_tests.sh # the mode defaults to mandatory
```

The differences:

| | `make` | `run_tests.sh` |
|---|---|---|
| Choosing the suite | target: `test`, `bonus`, `bonus_only`, `program`, `valgrind`, … | argument: `mandatory`, `bonus`, `bonus_only` or `program` |
| Valgrind | `make valgrind` sets the valgrind command, the 60 s timeout and the smaller buffer sizes for you | you set `RUNNER="valgrind ..."` yourself |
| Settings | `make test BUFFER_SIZES="1 42"` | `BUFFER_SIZES="1 42" ./run_tests.sh` |
| More detail on failures | `make test V=1` / `make test V=2` (`make -v` is make's own flag) | `./run_tests.sh -v` / `-vv` (or `--verbose`, twice for `-vv`) |
| When tests fail | adds a `make: *** ... Error 1` line | only the test output |

Both change into `get_next_line-Test/` first, so a relative `GNL_DIR` is resolved from
there, not from the folder you typed the command in.
`GNL_DIR=../other_gnl` means `get_next_line-Test/../other_gnl`.

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
- `norminette` on your files, if it is installed (otherwise `[!!]` and
  the Norm is not checked)

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
| Malloc failure (strict) | for every `malloc` in turn: when that call returns `NULL`, nothing may still be allocated **right then**, even if the caller never calls `get_next_line` again |
| Stopping early | read x lines (x = 0..6), stop, close the fd: further calls may only return lines that were already buffered, in order, then `NULL`, and nothing may leak. Then a new file reads cleanly |
| A program exits early | a child process reads x lines and calls `exit()`: no crash, the right lines (under valgrind: no definite leak in the child) |
| Several programs, one fd | one fd (a file, then stdin) is read by 8 child processes in a row, 3 lines each, like `exec 3< f; ./prog 3; ./prog 3`. Program k must print the 3 lines starting at the fd's current offset (what the previous one buffered is gone), and must not read further than those 3 lines need |
| Writing while reading | a pipe whose write end the test keeps open and writes into between calls; **(extra)** a file appended by another fd while it is read and after EOF (like `tail -f`); **(extra)** one `O_RDWR` fd: read to EOF, write new lines through the same fd, seek back, read them |
| Programs and binaries | a compiled binary (`/bin/true`): `'\0'` bytes and long lines must not crash, hang or leak, and it must end with `NULL`; the output of a running `sh` loop (2000 lines) through a pipe |

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
| malloc failures, round robin | 3 fds read in turn while `malloc` 1, 2, 3, … fails: only the fd whose call failed may lose lines; the others continue with exactly the right lines; no leak |
| stop early on 2 fds | read some lines of a and b, close both, keep reading c: c is unaffected, a and b only give their own buffered lines, then `NULL`, no leak |
| a program reads 2 fds and exits | a child reads x lines of each of 2 fds in turn and exits |
| 2 fds, 8 programs | 2 fds shared by 8 child processes in a row; each fd must continue from its own offset |
| 2 pipes written between calls | the test writes into both pipes between calls |
| (extra) 2 files appended | both files get new lines while open and after EOF |
| same file opened twice | 2 fds on the same path: each returns the whole file |

### 5. Program tests (`run_tests.sh`, also `make program`)

For every buffer size, `main.c` is compiled with your files (like `make`)
and run from the shell:

| Case | Command (roughly) | Expected |
|---|---|---|
| every sample file | `./test -r 3 3< files/X.txt` | output identical to the file (`cmp`) |
| stdin, pipe | `./test -r < f`, `cat f \| ./test -r` | identical to the file |
| stops early | `./test -r -n x 3 3< f` (x = 1, 3, 10) | the first x lines; exit 0 |
| 5 programs, one fd | `{ ./test -n 3 3; ./test -n 3 3; … } 3< f` | each program prints the 3 lines starting at the fd offset the shell sees in `/proc/$$/fdinfo/3`. With `BUFFER_SIZE=1` the 5 outputs together are exactly the first 15 lines |
| slow writer | `{ printf 'first\n'; sleep 0.2; printf 'sec'; …; } \| ./test -r` | `first\nsecond\nthird` |
| append between programs | `{ ./test -r 3; printf 'three\nfour' >> f; ./test -r 3; } 3< f` | the second program prints only the new lines |
| binary file | `./test -r < ./test` | no crash, no hang (no leak under valgrind) |
| bonus: round robin | `./test_bonus -r 3 4 5 3< a 4< b 5< c` | one line of each file in turn |
| bonus: same file twice | `./test_bonus -r 3 4 3< f 4< f` | every line twice |
| bonus: lengths, `-n` | 3 fds of very different lengths; `-n 2` on 2 fds | |

Under `make valgrind*`, a program that reads to EOF is run with
`--errors-for-leak-kinds=all`: like Sfabi's tester, every heap block must
be freed ("All heap blocks were freed"). A program that stops early may
leave its stash *still reachable*, so only definite/indirect/possible leaks
fail those cases. Files with a line over 20 000 chars are skipped under
valgrind for `BUFFER_SIZE` 1 and 2 (`[SKIP]`): they take minutes there.

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

The `[OK]` / `[KO]` / `[SKIP]` lines and the summary go to **stdout**.
Everything that explains a failure goes to **stderr**: the `->` messages,
diffs, build errors, norminette errors, valgrind reports and, with
`-v`/`-vv`, the traces. On the console the two are mixed in order, so the
details are printed right under their KO line. To keep the console short and
read the details later:

```sh
make test 2> errors.log          # console: OK/KO + summary; errors.log: why
./run_tests.sh -v bonus 2> errors.log
```

In the file, each failure starts with a header naming it,
`==== mandatory (BUFFER_SIZE=42): several lines (with / without final \n) ====`,
since its KO line is not next to it there. These headers appear only when
stderr is not the terminal.

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
- **[SKIP]**: the case was not run, the line underneath says why (e.g. the
  open file limit is too low for fd 1023). It counts as neither passed nor
  failed.
- **[KO]**: the case failed. The `->` lines give the expected and the
  actual line. `\n`, `\t` and `\r` are shown escaped; other non-printable
  bytes appear as `\xNN`. Strings are cut after 60 bytes (300 with `-v`,
  4000 with `-vv`). Run again with `-v` to see *how* it fails (see below).
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
  report (leak, invalid read/write, ...) is printed on stderr right under
  the KO line, headed `==== valgrind: <suite>: <test> ====`, with the file
  and line. Use `make valgrind 2> valgrind.log` to keep the console to the
  OK/KO lines and collect every report, labelled, in `valgrind.log`.
- **-> SEGFAULT / TIMEOUT with `-v`**: the trace shows what `get_next_line`
  was doing right before the crash or the timeout (e.g. a `read` returning
  0 over and over).
- **[BUILD FAIL]**: your files don't compile with the tests. The compiler
  errors are shown, sometimes with a hint, e.g. for a `BUFFER_SIZE` that
  is defined without `#ifndef`.

The summary at the end lists each suite by name, with the buffer size in
brackets (`mandatory[42]`). `make test` / `make bonus` exit non-zero if
anything failed.

### Common messages

- **`malloc N failed: expected NULL (or "…"), got "…"`**
  (`every malloc() fails in turn`): during that call the N-th `malloc` inside
  `get_next_line` returned `NULL`, and your function still returned a line,
  but not the right one: usually a partial line, or `""`. Typically a
  join/strjoin fails, your code keeps the old stash (or nothing) and builds a
  line out of it anyway. That data is lost. When a `malloc` fails, free the
  stash and return `NULL`; don't build a line from what is left.
- **`malloc N failed and get_next_line returned NULL, but X block(s) are
  still allocated (stash not freed on error)`** (strict): you did return
  `NULL`, but the static stash (or the read buffer) is still allocated. A
  caller stops at the first `NULL`, so that memory is never freed. Valgrind
  doesn't flag it: the test reads to EOF afterwards (which frees it), and a
  block still pointed to by a static counts as "still reachable", not a leak.
  Free and reset the stash on every error return:
  `return (free(stash), stash = NULL, NULL);`.
- **`X block(s) allocated by get_next_line were never freed`**: a leak the
  tester's own tracking found (no valgrind needed). `-v` lists each block:
  its size, which `get_next_line` call allocated it and what it holds.

## Verbose output (`-v`, `-vv`)

```sh
./run_tests.sh -v            # or: make test V=1
./run_tests.sh -vv bonus     # or: make bonus V=2
make valgrind V=1            # works with every target
```

Without a flag you get the messages above. `-v` adds, for each failure:

- **a diff.** For one line, expected and got are aligned, with a `^` under
  the first differing byte and what is wrong there (bytes missing, extra
  bytes, a different byte). For a sequence of lines, the calls are listed
  with `-` (expected) / `+` (got), as with `diff`. The program tests show
  sizes, `cmp`'s first difference, and up to 60 lines of `diff` instead of 8.
- **what was injected.** E.g. `malloc #3 (4 bytes) returned NULL during
  get_next_line call 13`.
- **leaked blocks.** For each block still allocated: size, the call that
  allocated it, its content (not shown under valgrind).
- **a trace.** The last 20 things `get_next_line` did before the first
  failed check, or before a crash or timeout: each call and what it
  returned, and every `malloc`, `free` and `read` inside it. An injected
  failure is marked `<== injected failure`, and a `free` of something
  `get_next_line` never allocated is marked too.
- for program tests, the command and which binary `./test` is, so you can
  rerun it by hand. Build logs are shown in full.

`-vv` gives the same with **the whole trace of the failing case** (up to
2000 events; in the loops that fail one malloc/read after another, the
trace covers only the iteration that failed). It also shows every leaked
block, strings up to 4000 bytes, every failure message (not just the first
10), the full `diff`, and an `od -c` view of the bytes around the first
difference in program tests.

Example: `-v` on a `get_next_line` that returns what it has when a join
fails (`BUFFER_SIZE=3`):

```
  [KO] every malloc() fails in turn: no crash, no leak
      -> tests/test_mandatory.c:502: malloc 3 failed: expected NULL (or "first line\n"), got "" (when malloc fails, return NULL and free the stash, not a line built from what you have so far)
         injected: malloc #3 (4 bytes) returned NULL during get_next_line call 13
         diff, call by call (- expected, + got):
         -    1  "first line\n"
         +    1  ""
         - expected: "first line\n"
         + got:      ""
                      ^ byte 0: got ends here, 11 byte(s) missing: "first line\n"
      trace: last 11 of 11 event(s) up to the first failed check:
         get_next_line(5)            [call 13]
             malloc(4) -> block #1
             read(5, buf, 3) -> 3  "fir"
             malloc(1) -> block #2
             malloc(4) -> NULL     <== injected failure (malloc #3)
             malloc(1) -> block #4
             malloc(1) -> block #5
             free(block #2, 1 bytes)
             free(block #1, 4 bytes)
             free(block #5, 1 bytes)
           -> returned ""
```

`block #N` is the N-th `malloc` of that test (the same N as in
`malloc N failed`). Here the join's `malloc` (#3) failed, but the code then
went on to build a line (#4, #5) from an empty stash instead of returning
`NULL`.

## What the labels mean

- **(strict)**: goes beyond the subject, but strict testers check it. The
  subject says to return `NULL` on error; strict testers also require that
  nothing is left allocated at that moment.
- **(extra)**: behaviour the subject doesn't mention, but that a sound
  implementation has anyway: a file that grows after `get_next_line`
  returned `NULL` (like `tail -f`), or an `O_RDWR` fd written to and seeked
  back. A KO here usually means a static "EOF reached" flag or a stash that
  isn't reset.

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

Some cases were added after comparing with
[Sfabi28/gnl_tester](https://github.com/Sfabi28/gnl_tester): building
without `-D BUFFER_SIZE` (`default`), the same file opened on 2 fds,
the norminette check, and valgrind requiring every block to be freed when a
program reads to EOF.
