#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
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
            free = std::numeric_limits<uint32_t>::max();
            return false;
        }
        this->capacity = capacity;
        for (int i = 0; i < (capacity - 1); i++) {
            nodes[i].next = i + 1;
        }
        nodes[capacity - 1].next = std::numeric_limits<uint32_t>::max();
        free = 0;
        return true;
    }

    template<typename... Args>
    uint32_t Construct(Args&&... args)
    {
        if (free != std::numeric_limits<uint32_t>::max()) {
            uint32_t index = free;
            Node& node = nodes[index];
            free = node.next;
            node.element = T(std::forward<Args>(args)...);
            return index;
        }
        return std::numeric_limits<uint32_t>::max();
    }

    bool IsOwned(uint32_t index)
    {
        return (index >= 0) && (index < capacity);
    }

    void Destroy(uint32_t index)
    {
        if (index != std::numeric_limits<uint32_t>::max()) {
            assert(IsOwned(index));
            nodes[index].element.~T();
            nodes[index].next = free;
            free = index;
        }
    }

    T& operator[](uint32_t index)
    {
        assert(IsOwned(index));
        return nodes[index].element;
    }

    void DeInit()
    {
        capacity = 0;
        size = 0;
        delete[] nodes;
        free = std::numeric_limits<uint32_t>::max();
    }
    
    private:

    union Node {
        uint32_t next = std::numeric_limits<uint32_t>::max();
        T element;
    };

    uint32_t capacity = 0;
    uint32_t size = 0;
    Node* nodes = nullptr;
    uint32_t free = std::numeric_limits<uint32_t>::max();
};