
#include <atomic>
#include <optional>

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

        Node()
        : queue_next(nullptr) {}

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
        for (Node *node = front.load(); node; node = next) {
            next = node->queue_next.load();
            node->value.~T();
            delete node;
        }

        // Delete nodes in the free list.
        for (Node *node = free_list; node; node = next) {
            next = node->free_list_next;
            delete node;
        }
    }

    template <typename Value>
    void push_back(Value&& value) {
        // Get a node from the free list, or otherwise allocate a new node.
        Node *node;
        do {
            node = free_list.load();
        } while (node && !free_list.compare_exchange_weak(node, node->free_list_next));
        if (!node) {
            node = new Node;
        }

        new (&node->value) T(std::forward<Value>(value));

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
                // beat us to the punch.
                null = nullptr;
                if (!front.compare_exchange_strong(null, node)) {
                    // A concurrent call to `push_back_node` beat us to it.
                    continue;
                }   
            }
            back.store(node);
            break;
        }
    }
};
