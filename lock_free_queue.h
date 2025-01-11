
#include <atomic>
#include <cstdint>
#include <optional>

// `TaggedPtr<T>` is a `T*`, but the least significant bit is used as a tag.
// On 16-bit systems or larger (32-bit, 64-bit), pointer-aligned addresses
// will always be a multiple of two, so the lowest bit of such addresses is
// always zero.
// That bit can be put to use. For example, in `Queue`, below, it's used to
// mark a `Node` as being "busy."
template <typename T>
struct TaggedPtr {
    std::uintptr_t raw;

    T *ptr() const;
    void ptr(T *new_value);
    bool bit() const;
    void bit(bool new_value);
    
    TaggedPtr();
    TaggedPtr(T*, bool);
    explicit TaggedPtr(std::uintptr_t);

    T *operator->() const;
};

template <typename T>
TaggedPtr<T>::TaggedPtr()
: raw(0) {}

template <typename T>
TaggedPtr<T>::TaggedPtr(T *p, bool b)
: raw(reinterpret_cast<std::uintptr_t>(p) | b) {}

template <typename T>
TaggedPtr<T>::TaggedPtr(std::uintptr_t raw)
: raw(raw) {}

template <typename T>
T *TaggedPtr<T>::ptr() const {
    return reinterpret_cast<T*>((raw >> 1) << 1);
}

template <typename T>
void TaggedPtr<T>::ptr(T *new_value) {
    *this = TaggedPtr(new_value, bit());
}

template <typename T>
bool TaggedPtr<T>::bit() const {
    return raw & 1;
}

template <typename T>
void TaggedPtr<T>::bit(bool new_value) {
    *this = TaggedPtr(ptr(), new_value);
}

template <typename T>
T *TaggedPtr<T>::operator->() const {
    return ptr();
}

template <typename T>
class AtomicTaggedPtr {
    std::atomic<std::uintptr_t> raw;

public:
    TaggedPtr<T> load() const;
    void store(TaggedPtr<T>, std::memory_order = std::memory_order_seq_cst);
    bool compare_exchange_weak(TaggedPtr<T>& expected, TaggedPtr<T> desired);
};

template <typename T>
TaggedPtr<T> AtomicTaggedPtr<T>::load() const {
    return TaggedPtr<T>(raw.load());
}

template <typename T>
void AtomicTaggedPtr<T>::store(TaggedPtr<T> new_value, std::memory_order order) {
    raw.store(new_value.raw, order);
}

template <typename T>
bool AtomicTaggedPtr<T>::compare_exchange_weak(TaggedPtr<T>& expected, TaggedPtr<T> desired) {
    return raw.compare_exchange_weak(expected.raw, desired.raw);
}

template <typename T>
class Queue {
private:
    struct Node {
        union {
            T value;
        };
        AtomicTaggedPtr<Node> next;

        Node()
        : next() {}

        ~Node() {}
    };

    std::atomic<Node*> before_first;
    std::atomic<Node*> last;
    std::atomic<Node*> free_list;

public:
    Queue()
    : before_first(new Node) // "dummy" node
    , last(before_first.load())
    , free_list(nullptr)
    {}

    ~Queue() {
        Node *next;

        // Delete nodes in the queue.
        Node *node = before_first.load(std::memory_order_relaxed);
        next = node->next.load().ptr();
        // The first node is the "dummy" without a value, so don't call ~T().
        delete node;
        node = next;
        while (node) {
            next = node->next.load().ptr();
            node->value.~T();
            delete node;
            node = next;
        }

        // Delete nodes in the free list.
        for (Node *node = free_list.load(); node; node = next) {
            next = node->next.load().ptr();
            delete node;
        }
    }

    template <typename Value>
    void push_back(Value&& value) {
        // Get a node from the free list, or otherwise allocate a new node.
        Node *node;
        TaggedPtr<Node> next;
        do {
            node = free_list.load();
            if (!node) {
                break;
            }
            next = node->next.load();
            // The `bit` is used to store whether the node is "busy."
            // A node is busy if its value is being moved from or is being destroyed.
            if (next.bit()) {
                // The node is busy. Bail.
                node = nullptr;
                break;
            }
            // The node is not busy. Snatch it.
        } while (!free_list.compare_exchange_weak(node, next.ptr()));
        
        if (!node) {
            node = new Node;
        }

        new (&node->value) T(std::forward<Value>(value));
        // Mark the node as "busy." We'll unmark it once the value is moved out
        // and destroyed in `try_pop_front`.
        node->next.store(TaggedPtr<Node>(nullptr, true), std::memory_order_relaxed);

        push_back_node(node); // the real guts of the implementation
    }

    std::optional<T> try_pop_front() {
        std::optional<T> result;

        // `before_first` always refers to a "dummy" node that either never had
        // a value (the initial state) or whose value was moved-from and
        // destroyed in the previous call to `try_pop_front`.
        //
        // The goal of `try_pop_front` is to advance `before_first` to the next
        // node, if any, and then to move that next node's value into `result`
        // and destroy the source value.
        // Finally, the previously `before_first` node can be added to the free list.
        Node *old_before_first, *new_before_first;
        do {
            old_before_first = before_first.load();
            new_before_first = old_before_first->next.load().ptr();
            if (!new_before_first) {
                return result; // empty queue
            }
        } while (!before_first.compare_exchange_weak(old_before_first, new_before_first));

        // Move the return value out of `new_before_first` and destroy the
        // empty source.
        // Unset the "busy" bit once we've done this.
        const auto set_busy_bit = [](Node *node, bool bit) {
            TaggedPtr<Node> next;
            do {
                next = node->next.load();
            } while (!node->next.compare_exchange_weak(next, TaggedPtr<Node>(next.ptr(), bit)));
        };
        result = std::move(new_before_first->value);
        new_before_first->value.~T();
        set_busy_bit(new_before_first, false);

        // Return `old_before_first` to the free list.
        Node *old_free_list;
        do {
            old_free_list = free_list.load();
            // Repurpose `old_before_list->next` to point to the next element
            // in the free list, as opposed to the next element in the queue.
            // Be sure to preserve the `bit()` value of the `TaggedPtr`,
            // because that holds information about whether `old_before_first`
            // is ready to be reused.
            TaggedPtr<Node> old_next;
            do {
                old_next = old_before_first->next.load();
            } while (!old_before_first->next.compare_exchange_weak(old_next, TaggedPtr<Node>(old_free_list, old_next.bit())));
        } while (!free_list.compare_exchange_weak(old_free_list, old_before_first));

        return result;
    }

private:
    void push_back_node(Node *node) {
        Node *old_last;
        TaggedPtr<Node> null;
        do {
            old_last = last.load();
            const TaggedPtr<Node> next = old_last->next.load();
            null = TaggedPtr<Node>(nullptr, next.bit());
        } while (!old_last->next.compare_exchange_weak(null, TaggedPtr<Node>(node, true)));

        // `last` now has `node` as its successor, and since `last->next`
        // is no longer `nullptr`, other calls to `push_back_node` are spinning
        // until we load a new value into `last`.
        last.store(node);
    }
};
