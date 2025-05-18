GCC_FLAGS = -Wextra -Werror -Wall -Wno-gnu-folding-constant -g

all:
	gcc $(GCC_FLAGS) libcoro.c corobus.c test.c ../utils/unit.c \
		-I ../utils -o test

test_glob:
	gcc $(GCC_FLAGS) *.c ../utils/unit.c -I ../utils -o test
