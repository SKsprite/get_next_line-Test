GNL_DIR		?= ..
BUFFER_SIZE	?= 42

CC			= cc
CFLAGS		= -Wall -Wextra -Werror -g -I$(GNL_DIR) -D BUFFER_SIZE=$(BUFFER_SIZE)

M_SRC		= $(GNL_DIR)/get_next_line.c $(GNL_DIR)/get_next_line_utils.c
B_SRC		= $(GNL_DIR)/get_next_line_bonus.c $(GNL_DIR)/get_next_line_utils_bonus.c

export GNL_DIR

# V=1 / V=2: same as run_tests.sh -v / -vv (more detail on each failure).
# Failure details go to stderr: `make test 2> errors.log`.
export V

VALGRIND	= RUNNER="valgrind -q --leak-check=full \
			  --show-leak-kinds=definite,indirect,possible \
			  --errors-for-leak-kinds=definite,indirect,possible \
			  --error-exitcode=42" TEST_TIMEOUT=60 \
			  BUFFER_SIZES="$${BUFFER_SIZES:-1 42 9999}"

.PHONY: all main main_bonus test mandatory bonus bonus_only program valgrind \
		valgrind_bonus valgrind_bonus_only valgrind_program clean fclean re \
		remove

# `make` builds ./test, the interactive reader:  exec 10< file; ./test 10
# (always recompiled: the binary shares its name with the `test` target).
all: main

main:
	$(CC) $(CFLAGS) main.c $(M_SRC) -o test

# ./test_bonus: same, built from the _bonus files (several fds at once).
main_bonus:
	$(CC) $(CFLAGS) -DGNL_BONUS main.c $(B_SRC) -o test_bonus

# Checks + mandatory tests + program tests for every BUFFER_SIZE.
test:
	@./run_tests.sh mandatory

mandatory: test

# Everything in `test`, plus mandatory + multi-fd tests on the bonus files.
bonus:
	@./run_tests.sh bonus

# Only the _bonus files: checks, mandatory + multi-fd + program tests.
bonus_only:
	@./run_tests.sh bonus_only

# Only the program tests: main.c run from the shell on shared fds, pipes,
# appended files and binaries.
program:
	@./run_tests.sh program

# The same suites under valgrind (invalid reads/writes, leaks).
valgrind:
	@$(VALGRIND) ./run_tests.sh mandatory

valgrind_bonus:
	@$(VALGRIND) ./run_tests.sh bonus

valgrind_bonus_only:
	@$(VALGRIND) ./run_tests.sh bonus_only

valgrind_program:
	@$(VALGRIND) ./run_tests.sh program

clean:
	rm -rf bin

fclean: clean
	rm -f test test_bonus

re: fclean all

# Deletes this whole tester directory (e.g. before submitting). Refuses
# unless the directory really is the tester, so it can't wipe anything else.
remove:
	@test -f "$(CURDIR)/run_tests.sh" && test -f "$(CURDIR)/gnl_utils.h" \
		|| { echo "refusing: $(CURDIR) does not look like the tester"; exit 1; }
	@echo "Removing $(CURDIR)"
	@rm -rf -- "$(CURDIR)"
