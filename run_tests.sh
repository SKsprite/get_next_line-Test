#!/bin/sh
# Usage: ./run_tests.sh [mandatory|bonus]          (default: mandatory)
#   mandatory  checks + mandatory tests on get_next_line.c / _utils.c
#   bonus      everything above, plus the mandatory tests and the multi-fd
#              tests on the _bonus files
# Env: GNL_DIR=path         where your get_next_line files are (default ..)
#      BUFFER_SIZES="1 42"  buffer sizes to test
#      RUNNER="valgrind ..." wraps each test binary
#      TEST_TIMEOUT=N       seconds before a single case is killed (default 5)

cd "$(dirname "$0")" || exit 1

MODE="${1:-mandatory}"
GNL_DIR="${GNL_DIR:-..}"
BUFFER_SIZES="${BUFFER_SIZES:-1 2 3 7 42 1024 9999 10000000}"
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
	mandatory|bonus) ;;
	*) echo "usage: $0 [mandatory|bonus]"; exit 2 ;;
esac

RED="$(printf '\033[31m')"
GREEN="$(printf '\033[32m')"
YELLOW="$(printf '\033[33m')"
BOLD="$(printf '\033[1m')"
RESET="$(printf '\033[0m')"

mkdir -p bin/check
passed=""
failed=""
broken=""

ok()   { echo "  ${GREEN}[OK]${RESET} $1"; }
ko()   { echo "  ${RED}[KO]${RESET} $1"; check_failed=1; }
warn() { echo "  ${YELLOW}[!!]${RESET} $1"; }
indent() { sed 's/^/      /' | head -n "${1:-15}"; }
count() { set -- $1; echo $#; }

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
			indent < "$log"
			if grep -q '"BUFFER_SIZE" redefined\|BUFFER_SIZE.* macro redefined' "$log"; then
				echo "      ${YELLOW}hint: wrap the default in #ifndef BUFFER_SIZE / # define BUFFER_SIZE n / #endif${RESET}"
			fi
			if grep -q "BUFFER_SIZE.*undeclared" "$log"; then
				echo "      ${YELLOW}hint: give BUFFER_SIZE a default value in $hdr (inside #ifndef BUFFER_SIZE)${RESET}"
			fi
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
	echo
	if [ $check_failed -eq 0 ]; then
		passed="$passed checks($label)"
	else
		failed="$failed checks($label)"
	fi
	return 0
}

# ------------------------------------------------------------------- tests
# run_suite <name> <test.c> <sources> <bs> <extra cflags>
run_suite() {
	name="$1"; test_src="$2"; srcs="$3"; bs="$4"; extra="$5"
	bin="bin/${name}_bs$bs"
	log="$bin.log"
	files=""
	for f in $srcs; do files="$files $GNL_DIR/$f"; done
	if ! $CC $TFLAGS $extra -D BUFFER_SIZE="$bs" "$test_src" $UTILS $files \
			-o "$bin" $WRAP >"$log" 2>&1; then
		echo "${BOLD}$name (BUFFER_SIZE=$bs)${RESET}"
		echo "  ${RED}[BUILD FAIL]${RESET} could not compile the tests with your files"
		indent < "$log"
		if grep -q '"BUFFER_SIZE" redefined\|BUFFER_SIZE.* macro redefined' "$log"; then
			echo "      ${YELLOW}hint: wrap the default in #ifndef BUFFER_SIZE / # define BUFFER_SIZE n / #endif${RESET}"
		fi
		echo
		broken="$broken $name[$bs]"
		return
	fi
	$RUNNER "./$bin"
	status=$?
	if [ $status -eq 0 ]; then
		passed="$passed $name[$bs]"
	else
		[ $status -eq 42 ] && echo "  ${RED}valgrind reported memory errors above${RESET}"
		failed="$failed $name[$bs]"
	fi
}

run_all_sizes() {
	label="$1"; srcs="$2"; extra="$3"
	for bs in $BUFFER_SIZES; do
		run_suite "$label" tests/test_mandatory.c "$srcs" "$bs" "$extra"
		if [ "$extra" = "-DGNL_BONUS" ]; then
			run_suite bonus tests/test_bonus.c "$srcs" "$bs" "$extra"
		fi
	done
	for bs in 0 -1; do
		run_suite "${label}_badbuf" tests/test_bad_buffer.c "$srcs" "$bs" "$extra"
	done
}

if check_files mandatory "$M_SRC" "$M_HDR" 0; then
	run_all_sizes mandatory "$M_SRC" ""
fi
if [ "$MODE" = bonus ]; then
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
