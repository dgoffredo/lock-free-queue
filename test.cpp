#include "lock_free_queue.h"

#include <string>
#include <thread>
#include <vector>

int main() {
    Queue<std::string> queue;
    const int n_threads = 32;
    // const int rounds = 1'000'000;
    const int rounds = 100;
    std::vector<std::thread> threads;
    for (int i = 0; i < n_threads; ++i) {
        threads.emplace_back([i, &queue]() {
            queue.push_back("node from thread " + std::to_string(i));
            for (int j = 0; j < rounds; ++j) {
                std::optional<std::string> element;
                do {
                    element = queue.try_pop_front();
                } while (!element);
                queue.push_back(std::move(*element));
            }
        });
    }

    for (std::thread& thread : threads) {
        thread.join();
    }

    for (int i = 0; i < n_threads / 2; ++i) {
        (void)queue.try_pop_front();
    }
}
