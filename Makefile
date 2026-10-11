GNL_DIR		?= ..
BUFFER_SIZE	?= 42

CC			= cc
CFLAGS		= -Wall -Wextra -Werror -g -I$(GNL_DIR) -D BUFFER_SIZE=$(BUFFER_SIZE)

M_SRC		= $(GNL_DIR)/get_next_line.c $(GNL_DIR)/get_next_line_utils.c
B_SRC		= $(GNL_DIR)/get_next_line_bonus.c $(GNL_DIR)/get_next_line_utils_bonus.c

export GNL_DIR

VALGRIND	= RUNNER="valgrind -q --leak-check=full \
			  --show-leak-kinds=definite,indirect,possible \
			  --errors-for-leak-kinds=definite,indirect,possible \
			  --error-exitcode=42" TEST_TIMEOUT=60 \
			  BUFFER_SIZES="$${BUFFER_SIZES:-1 42 9999}"

.PHONY: all main main_bonus test bonus valgrind valgrind_bonus clean fclean re

# `make` builds ./test, the interactive reader:  exec 10< file; ./test 10
# (always recompiled: the binary shares its name with the `test` target).
all: main

main:
	$(CC) $(CFLAGS) main.c $(M_SRC) -o test

# ./test_bonus: same, built from the _bonus files (several fds at once).
main_bonus:
	$(CC) $(CFLAGS) -DGNL_BONUS main.c $(B_SRC) -o test_bonus

# Checks + mandatory tests for every BUFFER_SIZE.
test:
	@./run_tests.sh mandatory

# Everything in `test`, plus mandatory + multi-fd tests on the bonus files.
bonus:
	@./run_tests.sh bonus

# The same suites under valgrind (invalid reads/writes, leaks).
valgrind:
	@$(VALGRIND) ./run_tests.sh mandatory

valgrind_bonus:
	@$(VALGRIND) ./run_tests.sh bonus

clean:
	rm -rf bin

fclean: clean
	rm -f test test_bonus

re: fclean all
