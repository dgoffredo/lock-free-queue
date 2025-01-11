#include "lock_free_queue.h"

#include <iostream>
#include <string>
#include <thread>
#include <vector>

void test() {
    Queue<std::string> queue;
    const int n_threads = 4;
    // const int rounds = 1'000'000;
    const int rounds = 1'000;
    std::vector<std::thread> threads;
    for (int i = 0; i < n_threads; ++i) {
        threads.emplace_back([i, &queue]() {
            std::cerr << ("Thread " + std::to_string(i) + " has started.\n");
            queue.push_back("node from thread " + std::to_string(i));
            for (int j = 0; j < rounds; ++j) {
                std::optional<std::string> element;
                do {
                    element = queue.try_pop_front();
                } while (!element);
                queue.push_back(std::move(*element));
            }
            std::cerr << ("Thread " + std::to_string(i) + " has finished.\n");
        });
    }

    for (std::thread& thread : threads) {
        thread.join();
    }

    for (int i = 0; i < n_threads / 2; ++i) {
        (void)queue.try_pop_front();
    }
}

int main() {
    std::cout << "Beginning test.\n";
    test();
    std::cout << "Test complete.\n";
}
