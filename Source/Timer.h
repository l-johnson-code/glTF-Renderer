#pragma once

class Timer {
    
    public:
    
    void Create();
    float Delta();
    
    private:

    long long counts_per_second;
    long long last_count;
};