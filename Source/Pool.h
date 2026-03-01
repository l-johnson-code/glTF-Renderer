#pragma once

#include <cstdint>
#include <utility>

template<typename T>
class Pool {
    public:
    bool Init(uint32_t capacity)
    {
        size = 0;
        nodes = new Node[capacity];
        assert(nodes);
        if (!nodes) {
            this->capacity = 0;
            free = nullptr;
            return false;
        }
        this->capacity = capacity;
        for (int i = 0; i < (capacity - 1); i++) {
            nodes[i].next = &nodes[i + 1];
        }
        nodes[capacity - 1].next = nullptr;
        free = &nodes[0];
        return true;
    }

    template<typename... Args>
    T* Construct(Args&&... args)
    {
        if (free) {
            Node* next = free->next;
            T* element = new (&free->element) T (std::forward<Args>(args)...);
            free = next;
            return element;
        }
        return nullptr;
    }

    bool IsOwned(T* ptr)
    {
        Node* node_ptr = reinterpret_cast<Node*>(ptr);
        return (node_ptr >= this->nodes) && (node_ptr < (this->nodes + capacity));
    }

    void Destroy(T* ptr)
    {
        if (ptr) {
            assert(IsOwned(ptr));
            ptr->~T();
            Node* node_ptr = reinterpret_cast<Node*>(ptr);
            node_ptr->next = free;
            free = node_ptr;
        }
    }

    void DeInit()
    {
        capacity = 0;
        size = 0;
        delete[] nodes;
        free = nullptr;
    }
    
    private:

    union Node {
        Node* next = nullptr;
        alignas(T) std::byte element[sizeof(T)];
    };

    uint32_t capacity = 0;
    uint32_t size = 0;
    Node* nodes = nullptr;
    Node* free = nullptr;
};