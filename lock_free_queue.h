
#include <atomic>
#include <optional>

#include <iomanip> // TODO: no
#include <iostream> // TODO: no

// TODO: This implementation does not provide any exception guarantees.
// It could be modified to provide the strong exception guarantee, but I
// haven't yet bothered.

template <typename T>
class Queue {
private:
    struct Node {
        union {
            T value;
            Node *free_list_next;
        };
        std::atomic<Node*> queue_next;

        Node() {}

        ~Node() {}
    };

    std::atomic<Node*> front;
    std::atomic<Node*> back;
    std::atomic<Node*> free_list;

public:
    Queue()
    : front(nullptr)
    , back(nullptr) {}

    ~Queue() {
        Node *next;

        // Delete nodes in the queue.
        for (Node *node = front.load(std::memory_order_relaxed); node; node = next) {
            std::cout << "queue node has value: " << node->value << '\n';
            next = node->queue_next.load(std::memory_order_relaxed);
            std::cout << "queue node->queue_next is: " << std::hex << static_cast<const void*>(next) << '\n';
            node->value.~T();
            delete node;
        }

        // Delete nodes in the free list.
        for (Node *node = free_list; node; node = next) {
            std::cout << "free list node is: " << std::hex << static_cast<const void*>(node) << '\n';
            next = node->free_list_next;
            std::cout << "free list next is: " << std::hex << static_cast<const void*>(next) << '\n';
            delete node;
        }
    }

    template <typename Value>
    void push_back(Value&& value) {
        // Get a node from the free list, or otherwise allocate a new node.
        Node *node;
        Node *new_free_list;
        do {
            node = free_list.load();
            if (!node) {
                break;
            }
            new_free_list = node->free_list_next;
        } while (!free_list.compare_exchange_weak(node, new_free_list));
        if (!node) {
            node = new Node;
        }

        new (&node->value) T(std::forward<Value>(value));
        node->queue_next.store(nullptr, std::memory_order_relaxed);

        push_back_node(node); // the real guts of the implementation
    }

    std::optional<T> try_pop_front() {
        std::optional<T> result;

        Node *old_front;
        do {
            old_front = front.load();
            if (!old_front) {
                return result;
            } 
        } while (!front.compare_exchange_weak(old_front, old_front->queue_next.load()));

        result.emplace(std::move(old_front->value));
    
        old_front->value.~T();
        Node *old_free_list;
        do {
            Node *old_free_list = free_list.load();
            old_front->free_list_next = old_free_list;
        } while (!free_list.compare_exchange_weak(old_free_list, old_front));
        
        back.compare_exchange_strong(old_front, nullptr);

        return result;
    }

private:
    void push_back_node(Node *node) {
        for (;;) {
            Node *old_back;
             // `nullptr` value for use as "desired" value in `compare_exchange[...]`.
            Node *null;
            do {
                null = nullptr;
                old_back = back.load();
            } while (old_back && !old_back->queue_next.compare_exchange_weak(null, node));
            
            // Either `old_back` is nullptr, or we successfully stored `node` into
            // `old_back->queue_next`.
            
            if (!old_back) {
                // We're pushing onto an empty queue, so then `front` will be
                // null, too. That is, unless another call to `push_back_node`
                // or `push_front` beat us to the punch.
                null = nullptr;
                if (!front.compare_exchange_strong(null, node)) {
                    // A concurrent call to `push_back_node` or `push_front` beat us to it.
                    continue;
                }   
            }
            back.store(node);
            break;
        }
    }
};
