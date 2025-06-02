GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant

all: test

test:
	gcc $(GCC_FLAGS) userfs.c test.c ../utils/unit.c -I ../utils -o test

test_glob:
	gcc $(GCC_FLAGS) *.c ../utils/unit.c -I ../utils -o test
