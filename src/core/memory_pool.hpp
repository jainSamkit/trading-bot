template<typename T, size_t N>
class MemoryPool {

public: 
    MemoryPool() {
        std::iota(free_list_.begin(), free_list_.end(), 0);
        top_ = N;
    }

    T* allocate() {
        if(top_ == 0 ) return nullptr;
        return &buffer_[free_list_[--top_]];
    }

    void deallocate(T* ptr_) {
        size_t idx = (ptr_ - buffer_) / (sizeof(T));
        free_list_[top_++] = idx;
    }

    void reset() {
        top_ = N;
        std::iota(free_list_.begin(), free_list_.end(), 0);
    }

private:
    T buffer_[N]{};
    std::array<size_t, N> free_list_;
    size_t top_ = N;
}