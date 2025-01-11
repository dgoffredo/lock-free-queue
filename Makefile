test: test.cpp lock_free_queue.h Makefile
	clang++ -Wall -Wextra -pedantic -Werror --std=c++20 -fsanitize=undefined -fsanitize=thread -g -Og -o$@ $<
