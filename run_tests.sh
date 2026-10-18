#!/bin/sh
# Usage: ./run_tests.sh [-v|-vv] [mandatory|bonus|bonus_only|program]
#        (default: mandatory)
#   mandatory  checks + C tests + program tests on get_next_line.c / _utils.c
#   bonus      everything above, plus the same on the _bonus files, plus the
#              multi-fd tests
#   bonus_only only the _bonus files: their checks, the mandatory tests, the
#              multi-fd tests and the program tests on them
#   program    only the program tests (main.c run from the shell)
# Env: GNL_DIR=path         where your get_next_line files are (default ..)
#      BUFFER_SIZES="1 42"  buffer sizes to test ("default" = no -D flag)
#      RUNNER="valgrind ..." wraps each test binary
#      TEST_TIMEOUT=N       seconds before a single case is killed (default 5)
#      V=1 / V=2            same as -v / -vv (what `make test V=1` sets)
# -v, --verbose  more detail on each failure: a diff, how the line differs,
#                which malloc/read failed, leaked blocks, and the last 20
#                get_next_line events (calls, malloc, free, read)
# -vv            all of it in full, with the whole trace of the failing case
# [OK]/[KO] lines and the summary go to stdout; every failure detail goes to
# stderr, so `./run_tests.sh 2> errors.log` keeps the console short.

cd "$(dirname "$0")" || exit 1

usage() {
	echo "usage: $0 [-v|-vv|--verbose] [mandatory|bonus|bonus_only|program]"
}
VERBOSE="${V:-0}"
MODE=""
for arg in "$@"; do
	case "$arg" in
		-v|--verbose) VERBOSE=$((VERBOSE + 1)) ;;
		-vv) VERBOSE=2 ;;
		-h|--help) usage; exit 0 ;;
		-*) usage >&2; exit 2 ;;
		*)
			[ -z "$MODE" ] || { usage >&2; exit 2; }
			MODE="$arg"
			;;
	esac
done
case "$VERBOSE" in
	0|1|2) ;;
	*[!0-9]*|'') echo "V must be 0, 1 or 2 (got '$VERBOSE')" >&2; exit 2 ;;
	*) VERBOSE=2 ;;
esac
export VERBOSE
MODE="${MODE:-mandatory}"
GNL_DIR="${GNL_DIR:-..}"
BUFFER_SIZES="${BUFFER_SIZES:-default 1 2 3 7 42 1024 9999 10000000}"
TEST_TIMEOUT="${TEST_TIMEOUT:-5}"
export TEST_TIMEOUT
CC="${CC:-cc}"
CFLAGS="-Wall -Wextra -Werror"
TFLAGS="$CFLAGS -g -I. -I$GNL_DIR"
WRAP="-Wl,--wrap=malloc,--wrap=free,--wrap=read"
UTILS="test_utils.c gnl_utils.c"

M_SRC="get_next_line.c get_next_line_utils.c"
M_HDR="get_next_line.h"
B_SRC="get_next_line_bonus.c get_next_line_utils_bonus.c"
B_HDR="get_next_line_bonus.h"
ALLOWED="read malloc free"

case "$MODE" in
	mandatory|bonus|bonus_only|program) ;;
	*) usage >&2; exit 2 ;;
esac

RED="$(printf '\033[31m')"
GREEN="$(printf '\033[32m')"
YELLOW="$(printf '\033[33m')"
BOLD="$(printf '\033[1m')"
RESET="$(printf '\033[0m')"

mkdir -p bin/check

# Under valgrind, each process logs to its own file in bin/valgrind; after
# each test the non-empty logs are printed to stderr under the test's name
# (vg_dump, and run_case() in test_utils.c). They show on the console, and
# `2> file` puts every valgrind report in that file, labelled.
VG_LOG_DIR=""
case "$RUNNER" in
	*--log-file*|*--log-fd*|*--log-socket*) ;;
	*valgrind*)
		# Relative (the script cd's to its own directory and the tests never
		# chdir): $RUNNER is word-split, so an absolute path with a space in
		# it would break the valgrind command (exit 127).
		VG_LOG_DIR="bin/valgrind"
		mkdir -p "$VG_LOG_DIR"
		rm -f "$VG_LOG_DIR"/*.log
		RUNNER="$RUNNER --log-file=$VG_LOG_DIR/%p.log"
		;;
esac
export VG_LOG_DIR
if [ -t 2 ]; then VG_RED="$(printf '\033[31m')"; VG_RESET="$(printf '\033[0m')"; fi

# vg_dump <test name>: print the valgrind logs written since the last call
vg_dump() {
	[ -n "$VG_LOG_DIR" ] || return 0
	for f in "$VG_LOG_DIR"/*.log; do
		if [ -s "$f" ]; then
			echo "${VG_RED}==== valgrind: $1 ====${VG_RESET}"
			cat "$f"
			echo
		fi
		rm -f "$f"
	done >&2
}
passed=""
failed=""
broken=""

ok()   { echo "  ${GREEN}[OK]${RESET} $1"; }
ko()   { echo "  ${RED}[KO]${RESET} $1"; check_failed=1; }
warn() { echo "  ${YELLOW}[!!]${RESET} $1"; }
# indent [N]: indents stdin, keeping N lines (at least 60 with -v, all with
# -vv) and saying how many were cut.
indent() {
	n="${1:-15}"
	[ "$VERBOSE" -ge 1 ] && [ "$n" -lt 60 ] && n=60
	[ "$VERBOSE" -ge 2 ] && n=1000000000
	awk -v n="$n" '{ if (NR <= n) print "      " $0 }
		END { if (NR > n) printf "      ... %d more line(s) (-v / -vv show more)\n", NR - n }'
}
count() { set -- $1; echo $#; }
# err_header <what>: names the failure on stderr when it is not the terminal
# (2> file), where the [KO] line on stdout is not next to it.
err_header() { [ -t 2 ] || echo "==== $1 ===="; }

# ------------------------------------------------------------------ checks
# check_files <label> <sources> <header> <is_bonus>
check_files() {
	label="$1"; srcs="$2"; hdr="$3"; is_bonus="$4"
	check_failed=0
	echo "${BOLD}checks: $label${RESET}"

	missing=""
	for f in $srcs $hdr; do
		[ -f "$GNL_DIR/$f" ] || missing="$missing $f"
	done
	if [ -n "$missing" ]; then
		ko "missing file(s) in $GNL_DIR:$missing"
		echo
		broken="$broken checks($label)"
		return 1
	fi
	ok "files present: $srcs $hdr"

	if grep -Eq 'char[[:space:]]*\*[[:space:]]*get_next_line[[:space:]]*\([[:space:]]*int' "$GNL_DIR/$hdr"; then
		ok "$hdr declares char *get_next_line(int fd)"
	else
		ko "$hdr does not declare char *get_next_line(int fd)"
	fi

	# Must build with and without -D BUFFER_SIZE (subject).
	for variant in "" "-D BUFFER_SIZE=42"; do
		objs=""
		log="bin/check/build.log"
		: > "$log"
		built=1
		for f in $srcs; do
			o="bin/check/$(basename "$f" .c).o"
			if ! $CC $CFLAGS $variant -I"$GNL_DIR" -c "$GNL_DIR/$f" -o "$o" >>"$log" 2>&1; then
				built=0
			fi
			objs="$objs $o"
		done
		name="cc $CFLAGS${variant:+ $variant}"
		if [ $built -eq 1 ]; then
			ok "builds with: $name"
		else
			ko "does not build with: $name"
			{
				err_header "checks: $label: does not build with: $name"
				indent < "$log"
				if grep -q '"BUFFER_SIZE" redefined\|BUFFER_SIZE.* macro redefined' "$log"; then
					echo "      ${YELLOW}hint: wrap the default in #ifndef BUFFER_SIZE / # define BUFFER_SIZE n / #endif${RESET}"
				fi
				if grep -q "BUFFER_SIZE.*undeclared" "$log"; then
					echo "      ${YELLOW}hint: give BUFFER_SIZE a default value in $hdr (inside #ifndef BUFFER_SIZE)${RESET}"
				fi
			} >&2
		fi
	done

	# Analyse the objects from the last successful build.
	if [ $built -eq 1 ]; then
		defined=$(nm $objs 2>/dev/null | awk '$2 ~ /^[TtDdBbRrCc]$/ {print $3}')
		bad=""
		for sym in $(nm -u $objs 2>/dev/null | awk '$1 == "U" {print $2}' | sort -u); do
			case "$sym" in _*) continue ;; esac
			echo "$defined" | grep -qx "$sym" && continue
			echo " $ALLOWED " | grep -q " $sym " && continue
			bad="$bad $sym"
		done
		if [ -z "$bad" ]; then
			ok "external functions: only $ALLOWED"
		else
			ko "forbidden external function(s):$bad (allowed: $ALLOWED)"
		fi

		globals=$(nm $objs 2>/dev/null | awk '$2 ~ /^[BDCGS]$/ {print $3}' | sort -u | tr '\n' ' ')
		filestatic=$(nm $objs 2>/dev/null | awk '$2 ~ /^[bdgs]$/ && $3 !~ /\./ {print $3}' | sort -u | tr '\n' ' ')
		if [ -z "$globals$filestatic" ]; then
			ok "no global variables"
		else
			ko "global variable(s) are forbidden: $globals$filestatic"
		fi

		statics=$(nm $objs 2>/dev/null | awk '$2 ~ /^[bdgs]$/ && $3 ~ /\./ {print $3}' | sed 's/\.[0-9]*$//' | sort -u)
		nstatic=$(count "$statics")
		if [ "$is_bonus" = 1 ]; then
			if [ "$nstatic" -eq 1 ]; then
				ok "exactly one static variable ($statics)"
			else
				ko "bonus must use exactly one static variable, found $nstatic: $(echo $statics)"
			fi
		else
			echo "      static variables: ${nstatic} ($(echo $statics))"
		fi
	fi
	if grep -q 'libft' $(for f in $srcs $hdr; do echo "$GNL_DIR/$f"; done); then
		ko "libft is not allowed in this project"
	fi
	if command -v norminette >/dev/null 2>&1; then
		norm=$(cd "$GNL_DIR" && norminette $srcs $hdr 2>&1 | grep -v ': OK!$\|^Setting locale')
		if [ -z "$norm" ]; then
			ok "norminette"
		else
			ko "norminette errors (on stderr)"
			{ err_header "checks: $label: norminette"; echo "$norm" | indent 20; } >&2
		fi
	else
		warn "norminette not installed: Norm not checked"
	fi
	echo
	if [ $check_failed -eq 0 ]; then
		passed="$passed checks($label)"
	else
		failed="$failed checks($label)"
	fi
	return 0
}

# ------------------------------------------------------------------- tests
# "default" builds without -D BUFFER_SIZE, so the header's value is used.
bs_flag() { [ "$1" = default ] || echo "-D BUFFER_SIZE=$1"; }

# run_suite <name> <test.c> <sources> <bs> <extra cflags>
run_suite() {
	name="$1"; test_src="$2"; srcs="$3"; bs="$4"; extra="$5"
	bin="bin/${name}_bs$bs"
	log="$bin.log"
	files=""
	for f in $srcs; do files="$files $GNL_DIR/$f"; done
	if ! $CC $TFLAGS $extra $(bs_flag "$bs") "$test_src" $UTILS $files \
			-o "$bin" $WRAP >"$log" 2>&1; then
		echo "${BOLD}$name (BUFFER_SIZE=$bs)${RESET}"
		echo "  ${RED}[BUILD FAIL]${RESET} could not compile the tests with your files"
		{
			err_header "$name (BUFFER_SIZE=$bs): build failed"
			[ "$VERBOSE" -ge 1 ] && echo "      command: $CC $TFLAGS $extra $(bs_flag "$bs") $test_src $UTILS$files -o $bin $WRAP"
			indent < "$log"
			if grep -q '"BUFFER_SIZE" redefined\|BUFFER_SIZE.* macro redefined' "$log"; then
				echo "      ${YELLOW}hint: wrap the default in #ifndef BUFFER_SIZE / # define BUFFER_SIZE n / #endif${RESET}"
			fi
		} >&2
		echo
		broken="$broken $name[$bs]"
		return
	fi
	TEST_CONTEXT="$name (BUFFER_SIZE=$bs)" $RUNNER "./$bin"
	status=$?
	vg_dump "$name (BUFFER_SIZE=$bs)"
	if [ $status -eq 0 ]; then
		passed="$passed $name[$bs]"
	else
		[ $status -eq 42 ] && echo "  ${RED}valgrind reported memory errors (log on stderr)${RESET}" >&2
		failed="$failed $name[$bs]"
	fi
}

# ---------------------------------------------------------- program tests
# main.c (the interactive reader) built as a real program and run from the
# shell: fds opened by the shell, several programs on one fd, files written
# while they are read, binaries. Under valgrind, a program that reads to EOF
# must free every block (like Sfabi's tester); one that stops early may leave
# its stash "still reachable".

if command -v timeout >/dev/null 2>&1; then TIMEOUT="timeout $TEST_TIMEOUT"; else TIMEOUT=""; fi
case "$RUNNER" in
	*valgrind*) STRICT="$RUNNER --show-leak-kinds=all --errors-for-leak-kinds=all" ;;
	*) STRICT="$RUNNER" ;;
esac

# prog_status <status>: OK if 0, else a reason (and sets check_failed)
prog_failed() {
	case "$1" in
		124) echo "TIMEOUT (infinite loop or blocked read?)" ;;
		42) echo "valgrind reported memory errors (log on stderr)" ;;
		139) echo "SEGFAULT" ;;
		134) echo "ABORT (bad free / heap corruption?)" ;;
		*) echo "exit status $1" ;;
	esac
}

# -v: where ./test comes from, to rerun a failing command by hand.
explain_cmd() {
	[ "$VERBOSE" -ge 1 ] || return 0
	echo "      command: $1"
	echo "      (./test here is $prog: main.c built with BUFFER_SIZE=$bs; run it from $PWD)"
}

# explain_output <expected> <got>: sizes, first difference (-v), bytes (-vv)
explain_output() {
	[ "$VERBOSE" -ge 1 ] || return 0
	echo "      expected $(wc -c < "$1") bytes in $(wc -l < "$1") line(s), got $(wc -c < "$2") bytes in $(wc -l < "$2") line(s)"
	first=$(LC_ALL=C cmp "$1" "$2" 2>&1 | head -n 1)
	echo "      cmp: ${first#cmp: }"
	[ "$VERBOSE" -ge 2 ] || return 0
	# cmp: "differ: byte N" or "char N" (1-based), or "EOF on f after byte N"
	case "$first" in
		*EOF*after*) off=$(echo "$first" | sed -n 's/.*after [a-z]* \([0-9]*\).*/\1/p') ;;
		*) off=$(echo "$first" | sed -n 's/.*differ: [a-z]* \([0-9]*\).*/\1/p'); off=$((${off:-1} - 1)) ;;
	esac
	off=${off:-0}
	start=$((off > 16 ? off - 16 : 0))
	echo "      expected, bytes $start to $((start + 47)):"
	tail -c +$((start + 1)) "$1" | head -c 48 | od -An -c | sed 's/^/        /'
	echo "      got, bytes $start to $((start + 47)):"
	tail -c +$((start + 1)) "$2" | head -c 48 | od -An -c | sed 's/^/        /'
}

# pcase <name> <status> <expected file> <output file>
pcase() {
	if [ "$2" -ne 0 ]; then
		ko "$1 -> $(prog_failed "$2")"
		[ "$VERBOSE" -ge 1 ] && { err_header "program $label (BUFFER_SIZE=$bs): $1"; explain_cmd "$1"; } >&2
	elif [ -n "$3" ] && ! cmp -s "$3" "$4"; then
		ko "$1"
		{
			err_header "program $label (BUFFER_SIZE=$bs): $1"
			echo "      ${RED}->${RESET} output differs from the expected output (diff expected got):"
			explain_cmd "$1"
			explain_output "$3" "$4"
			diff "$3" "$4" 2>&1 | indent 8
		} >&2
	else
		ok "$1"
	fi
	vg_dump "program $label (BUFFER_SIZE=$bs): $1"
}

# The lines of the files, one line of each file in turn (like ./test_bonus).
round_robin() {
	spec=""
	for f in "$@"; do
		n=$(wc -l < "$f")
		nl=1
		if [ -s "$f" ] && [ "$(tail -c 1 "$f" | od -An -c | tr -d ' ')" != '\n' ]; then
			nl=0; n=$((n + 1))
		fi
		spec="$spec $f:$n:$nl"
	done
	awk 'BEGIN {
		n = ARGC - 1
		for (i = 1; i <= n; i++) { split(ARGV[i], p, ":"); f[i] = p[1]; t[i] = p[2]; nl[i] = p[3]; c[i] = 0 }
		do {
			any = 0
			for (i = 1; i <= n; i++)
				if (c[i] < t[i] && (getline l < f[i]) > 0) {
					c[i]++; any = 1
					printf "%s%s", l, (c[i] == t[i] && !nl[i]) ? "" : "\n"
				}
		} while (any)
	}' $spec
}

# fd_pos <fd>: offset of the shell's own fd (Linux /proc), empty if unknown
fd_pos() { awk '/^pos:/ {print $2}' "/proc/$$/fdinfo/$1" 2>/dev/null; }

# run_program_suite <label> <sources> <bs> <extra cflags>
run_program_suite() {
	label="$1"; srcs="$2"; bs="$3"; extra="$4"
	prog="bin/prog_${label}_bs$bs"
	log="$prog.log"
	files=""
	for f in $srcs; do files="$files $GNL_DIR/$f"; done
	echo "${BOLD}program tests: $label (BUFFER_SIZE=$bs)${RESET}"
	if ! $CC $CFLAGS -g -I"$GNL_DIR" $extra $(bs_flag "$bs") main.c $files \
			-o "$prog" >"$log" 2>&1; then
		echo "  ${RED}[BUILD FAIL]${RESET} could not compile main.c with your files"
		{ err_header "program $label (BUFFER_SIZE=$bs): build failed"; indent < "$log"; } >&2
		echo
		broken="$broken program_$label[$bs]"
		return
	fi
	check_failed=0
	out="$prog.out"
	exp="$prog.exp"

	# 1. Every sample file, through an fd opened by the shell, stdin and a pipe.
	for f in files/*.txt; do
		if [ "$STRICT" != "$RUNNER" ] && [ "$bs" != default ] && [ "$bs" -le 2 ] \
				&& [ "$(awk 'length > m { m = length } END { print m + 0 }' "$f")" -gt 20000 ]; then
			echo "  ${YELLOW}[SKIP]${RESET} exec 3< $f; ./test -r 3"
			echo "      ${YELLOW}->${RESET} a 20000+ char line is too slow under valgrind with BUFFER_SIZE=$bs (run it without valgrind)"
			continue
		fi
		$TIMEOUT $STRICT "./$prog" -r 3 3< "$f" > "$out"
		pcase "exec 3< $f; ./test -r 3" $? "$f" "$out"
	done
	$TIMEOUT $STRICT "./$prog" -r < files/basic.txt > "$out"
	pcase "./test -r < files/basic.txt (stdin)" $? files/basic.txt "$out"
	cat files/many_lines.txt | $TIMEOUT $STRICT "./$prog" -r > "$out"
	pcase "cat files/many_lines.txt | ./test -r (pipe)" $? files/many_lines.txt "$out"

	# 2. A program reads x lines and exits without reaching EOF.
	for x in 1 3 10; do
		head -n $x files/many_lines.txt > "$exp"
		$TIMEOUT $RUNNER "./$prog" -r -n $x 3 3< files/many_lines.txt > "$out"
		pcase "./test -n $x: reads $x lines and exits" $? "$exp" "$out"
	done

	# 3. One fd, opened once by the shell, read by 5 programs in a row. Each
	#    program prints the 3 lines starting where the fd offset was (the
	#    rest of what it buffered is lost when it exits).
	{
		good=1
		for run in 1 2 3 4 5; do
			pos=$(fd_pos 3)
			$TIMEOUT $RUNNER "./$prog" -r -n 3 3 > "$out"
			st=$?
			if [ $st -ne 0 ]; then
				ko "5 programs on one fd: program $run -> $(prog_failed $st)"; good=0; break
			fi
			if [ -n "$pos" ]; then
				tail -c +$((pos + 1)) files/many_lines.txt | head -n 3 > "$exp"
				if ! cmp -s "$exp" "$out"; then
					ko "5 programs on one fd: program $run (fd offset $pos) printed the wrong lines"
					{
						err_header "program $label (BUFFER_SIZE=$bs): 5 programs on one fd"
						explain_output "$exp" "$out"
						diff "$exp" "$out" | indent 8
					} >&2
					good=0; break
				fi
			fi
		done
		[ $good -eq 1 ] && ok "exec 3< file; 5 x ./test -n 3 3 (programs share the fd)"
		vg_dump "program $label (BUFFER_SIZE=$bs): 5 programs on one fd"
	} 3< files/many_lines.txt
	if [ "$bs" = 1 ]; then
		{ for run in 1 2 3 4 5; do $TIMEOUT $RUNNER "./$prog" -r -n 3 3; done; } \
			3< files/many_lines.txt > "$out"
		head -n 15 files/many_lines.txt > "$exp"
		pcase "BUFFER_SIZE=1: 5 programs x 3 lines = the first 15 lines" 0 "$exp" "$out"
	fi

	# 4. Writing while reading: a slow writer on a pipe, and a file appended
	#    between two programs that share the fd.
	{ printf 'first\n'; sleep 0.2; printf 'sec'; sleep 0.2; printf 'ond\nthi'; sleep 0.2; printf 'rd'; } \
		| $TIMEOUT $STRICT "./$prog" -r > "$out"
	st=$?
	printf 'first\nsecond\nthird' > "$exp"
	pcase "slow writer on a pipe (lines arrive in pieces)" $st "$exp" "$out"

	tmp="$prog.append"
	printf 'one\ntwo\n' > "$tmp"
	{
		$TIMEOUT $STRICT "./$prog" -r 3 > "$out"
		st1=$?
		printf 'three\nfour' >> "$tmp"
		$TIMEOUT $STRICT "./$prog" -r 3 > "$out.2"
		st2=$?
	} 3< "$tmp"
	printf 'one\ntwo\n' > "$exp"
	pcase "file read to EOF by one program..." $st1 "$exp" "$out"
	printf 'three\nfour' > "$exp"
	pcase "...appended, then the next program reads only the new lines" $st2 "$exp" "$out.2"
	rm -f "$tmp" "$out.2"

	# 5. Binary files: the program itself ('\0' bytes, long lines).
	$TIMEOUT $STRICT "./$prog" -r < "$prog" > /dev/null
	pcase "binary file: ./test -r < ./test (no crash, hang or leak)" $? "" ""

	# 6. Bonus: several fds in one program.
	if [ "$extra" = "-DGNL_BONUS" ]; then
		round_robin files/a.txt files/b.txt files/c.txt > "$exp"
		$TIMEOUT $STRICT "./$prog" -r 3 4 5 3< files/a.txt 4< files/b.txt 5< files/c.txt > "$out"
		pcase "./test_bonus -r 3 4 5 (a, b, c round robin)" $? "$exp" "$out"
		awk '{print; print}' files/basic.txt > "$exp"
		$TIMEOUT $STRICT "./$prog" -r 3 4 3< files/basic.txt 4< files/basic.txt > "$out"
		pcase "same file opened twice: ./test_bonus -r 3 4" $? "$exp" "$out"
		round_robin files/many_lines.txt files/one_line.txt files/no_newline.txt > "$exp"
		$TIMEOUT $RUNNER "./$prog" -r 3 4 5 3< files/many_lines.txt 4< files/one_line.txt \
			5< files/no_newline.txt > "$out"
		pcase "3 fds of very different lengths" $? "$exp" "$out"
		$TIMEOUT $RUNNER "./$prog" -r -n 2 3 4 3< files/a.txt 4< files/a.txt > "$out"
		awk '{print; print}' files/a.txt | head -n 4 > "$exp"
		pcase "./test_bonus -n 2 3 4: stops early on 2 fds" $? "$exp" "$out"
	fi
	rm -f "$out" "$exp"
	echo
	if [ $check_failed -eq 0 ]; then
		passed="$passed program_$label[$bs]"
	else
		failed="$failed program_$label[$bs]"
	fi
}

run_all_sizes() {
	label="$1"; srcs="$2"; extra="$3"
	for bs in $BUFFER_SIZES; do
		run_suite "$label" tests/test_mandatory.c "$srcs" "$bs" "$extra"
		if [ "$extra" = "-DGNL_BONUS" ]; then
			run_suite bonus tests/test_bonus.c "$srcs" "$bs" "$extra"
		fi
		run_program_suite "$label" "$srcs" "$bs" "$extra"
	done
	for bs in 0 -1; do
		run_suite "${label}_badbuf" tests/test_bad_buffer.c "$srcs" "$bs" "$extra"
	done
}

if [ "$MODE" = program ]; then
	for bs in $BUFFER_SIZES; do
		run_program_suite mandatory "$M_SRC" "$bs" ""
	done
elif [ "$MODE" = bonus_only ]; then
	:
elif check_files mandatory "$M_SRC" "$M_HDR" 0; then
	run_all_sizes mandatory "$M_SRC" ""
fi
if [ "$MODE" = bonus ] || [ "$MODE" = bonus_only ]; then
	if check_files bonus "$B_SRC" "$B_HDR" 1; then
		run_all_sizes mandatory_on_bonus "$B_SRC" "-DGNL_BONUS"
	fi
fi

echo "${BOLD}==================== SUMMARY ====================${RESET}"
echo "${GREEN}passed:     $(count "$passed")${RESET}"
[ -n "$failed" ] && echo "${RED}failed:     $(count "$failed") ->$failed${RESET}"
[ -n "$broken" ] && echo "${YELLOW}build fail: $(count "$broken") ->$broken${RESET}"
echo "(name[N] = test suite run with BUFFER_SIZE=N)"

[ -z "$failed" ] && [ -z "$broken" ]
