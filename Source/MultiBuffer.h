#pragma once

template<typename T, int COUNT>
class MultiBuffer {
    public:

    void Next()
    {
        current = (current + 1) % COUNT;
    }

    int Size() const
    {
        return COUNT;
    }

    T& Current()
    {
        return inner[current];
    }

    const T& Current() const
    {
        return inner[current];
    }

    T& operator[](int i)
    {
        return inner[i]; 
    }

    const T& operator[](int i) const
    {
        return inner[i];
    }

    private:

    T inner[COUNT];
    int current = 0;
};