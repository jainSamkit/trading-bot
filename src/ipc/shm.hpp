#pragma once
#include <sys/mman.h>
#include <fcntl.h>
#include <stdexcept>

template<typename T>
class ShmOwner {

    static ShmOwner create(const char* shm_name) {
        int fd = shm_open(shm_name, O_CREAT | O_RDWR, 0600);
        if(fd==-1) throw std::runtime_error("shm_open create failed");
        ftruncate(fd, sizeof(T));
        void* ptr = mmap(nullptr, sizeof(T),
                            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if(ptr == MAP_FAILED) throw std::runtime_error("mmap failed on create");
        return ShmOwner(shm_name, new (ptr) T{});
    }
    
    static ShmOwner attach(char* SHM_NAME) {
        int fd = shm_open(SHM_NAME, O_RDWR, 0600);
        if(fd==-1) throw std::runtime_error("shm_open attach failed");

        void* ptr = mmap(nullptr, sizeof(T),
                            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        close(fd);
        if(ptr == MAP_FAILED) throw std::runtime_error("mmap failed on attach");
        return ShmOwner(nullptr, static_cast<T*>(ptr));
    }

    static void destroy(char* SHM_NAME) {
        shm_unlink(SHM_NAME);
    }

    ~ShmOwner() {
        if(ptr_) munmap(ptr_, sizeof(T));
        if(name_) shm_unlink(name_);
    }

    ShmOwner(const ShmOwner&) = delete;
    ShmOwner& operator =(const ShmOwner&) = delete;
    ShmOwner(ShmOwner&& o) ptr_(o.ptr_), name_(o.name_) {
        o.ptr_ = nullptr;
        o.name_ = nullptr;
    };

    ShmOwner(const char* name, T* ptr): name_(name) ptr_(ptr) {};

    private:
        T* ptr_;
        const char* name_;
};