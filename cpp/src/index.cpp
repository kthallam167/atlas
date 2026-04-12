#include "atlas/index.hpp"
#include <fcntl.h>
#include <stdexcept>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace atlas {

Mmap::Mmap(const std::string& path) {
    fd_ = ::open(path.c_str(), O_RDONLY);
    if (fd_ < 0) throw std::runtime_error("cannot open: " + path);
    struct stat st{};
    if (::fstat(fd_, &st) != 0) throw std::runtime_error("fstat failed: " + path);
    size_ = static_cast<size_t>(st.st_size);
    if (size_ > 0) {
        void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
        if (p == MAP_FAILED) throw std::runtime_error("mmap failed: " + path);
        data_ = static_cast<const uint8_t*>(p);
    }
}

Mmap::~Mmap() {
    if (data_) ::munmap(const_cast<uint8_t*>(data_), size_);
    if (fd_ >= 0) ::close(fd_);
}

Mmap::Mmap(Mmap&& o) noexcept : data_(o.data_), size_(o.size_), fd_(o.fd_) {
    o.data_ = nullptr; o.size_ = 0; o.fd_ = -1;
}

Mmap& Mmap::operator=(Mmap&& o) noexcept {
    if (this != &o) {
        if (data_) ::munmap(const_cast<uint8_t*>(data_), size_);
        if (fd_ >= 0) ::close(fd_);
        data_ = o.data_; size_ = o.size_; fd_ = o.fd_;
        o.data_ = nullptr; o.size_ = 0; o.fd_ = -1;
    }
    return *this;
}

}  // namespace atlas
