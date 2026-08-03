#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

template<typename T>
class Pool {
    public:
    
    template<typename... Args>
    uint32_t Emplace(Args&&... args)
    {
        uint32_t index = std::numeric_limits<uint32_t>::max();
        if (free != std::numeric_limits<uint32_t>::max()) {
            index = free;
            #ifdef DEBUG
            occupied_bitmap[index] = true;
            #endif
        } else {
            nodes.emplace_back();
            index = nodes.size() - 1;
            #ifdef DEBUG
            occupied_bitmap.emplace_back(true);
            #endif
        }

        Node& node = nodes[index];
        free = node.next;
        node.element = T(std::forward<Args>(args)...);
        size++;

        return index;
    }

    void Erase(uint32_t index)
    {
        if (index != std::numeric_limits<uint32_t>::max()) {
            assert(index >= 0 && index < nodes.size());

            #ifdef DEBUG
            assert(occupied_bitmap[index]);
            #endif

            nodes[index].element.~T();
            nodes[index].next = free;
            free = index;
            size--;

            #ifdef DEBUG
            occupied_bitmap[index] = false;
            #endif
        }
    }

    void Reserve(uint32_t count)
    {
        nodes.reserve(count);
    }

    T& operator[](uint32_t index)
    {
        assert(index >= 0 && index < nodes.size());

        #ifdef DEBUG
        assert(occupied_bitmap[index]);
        #endif

        return nodes[index].element;
    }

    const T& operator[](uint32_t index) const
    {
        assert(index >= 0 && index < nodes.size());

        #ifdef DEBUG
        assert(occupied_bitmap[index]);
        #endif

        return nodes[index].element;
    }

    uint32_t Size() const
    {
        return size;
    }

    uint32_t Capacity() const
    {
        return nodes.capacity();
    }
  
    private:

    union Node {
        uint32_t next = std::numeric_limits<uint32_t>::max();
        T element;
    };

    uint32_t free = std::numeric_limits<uint32_t>::max();
    uint32_t size = 0;
    std::vector<Node> nodes;

    #ifdef DEBUG
    std::vector<bool> occupied_bitmap;
    #endif
};