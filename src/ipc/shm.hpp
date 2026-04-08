#pragma once
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <unistd.h>

template<typename T>
class ShmOwner {
public:
    // process A — creates and initializes the region
    static ShmOwner create(const char* name) {
        int fd = shm_open(name, O_CREAT | O_RDWR, 0600);
        if (fd == -1) throw std::runtime_error("shm_open create failed");
        if (ftruncate(fd, static_cast<off_t>(sizeof(T))) == -1)
            throw std::runtime_error("ftruncate failed");
        void* ptr = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (ptr == MAP_FAILED) throw std::runtime_error("mmap failed on create");
        prefault(ptr, sizeof(T));
        return ShmOwner(name, new (ptr) T{});
    }

    // forked children / external process — attaches to existing region
    static ShmOwner attach(const char* name) {
        int fd = shm_open(name, O_RDWR, 0600);
        if (fd == -1) throw std::runtime_error("shm_open attach failed");
        void* ptr = mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if (ptr == MAP_FAILED) throw std::runtime_error("mmap failed on attach");
        return ShmOwner(nullptr, static_cast<T*>(ptr));
    }

    T* get() { return ptr_; }

    // destructor: munmap always, shm_unlink only in creator
    ~ShmOwner() {
        if (ptr_)  munmap(ptr_, sizeof(T));
        if (name_) shm_unlink(name_);
    }

    ShmOwner(const ShmOwner&)            = delete;
    ShmOwner& operator=(const ShmOwner&) = delete;

    ShmOwner(ShmOwner&& o) noexcept : name_(o.name_), ptr_(o.ptr_) {
        o.name_ = nullptr;
        o.ptr_  = nullptr;
    }

private:
    ShmOwner(const char* name, T* ptr) : name_(name), ptr_(ptr) {}

    static void prefault(void* ptr, size_t size) {
        volatile char* p = static_cast<volatile char*>(ptr);
        for (size_t i = 0; i < size; i += 4096)
            p[i] = p[i];
    }

    const char* name_;   // non-null only in creator → drives shm_unlink on destroy
    T*          ptr_;
};
