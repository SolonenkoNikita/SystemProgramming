GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -g
LDFLAGS = -ldl -rdynamic -pthread
UTILS_PATH = ../utils

all: test test_leak

test:
	gcc $(GCC_FLAGS) thread_pool.c test.c $(UTILS_PATH)/unit.c -I $(UTILS_PATH) -o test

test_leak:
	gcc $(GCC_FLAGS) thread_pool.c test.c $(UTILS_PATH)/unit.c $(UTILS_PATH)/heap_help/heap_help.c \
	-I $(UTILS_PATH) -I $(UTILS_PATH)/heap_help $(LDFLAGS) -o test_leak

test_glob:
	gcc $(GCC_FLAGS) *.c $(UTILS_PATH)/unit.c -I $(UTILS_PATH) -o test

run_test: test
	./test

run_leak_check: test_leak
	HHREPORT=v HHBACKTRACE=on ./test_leak

clean:
	rm -f test test_leak

.PHONY: all test test_leak test_glob run_test run_leak_check clean