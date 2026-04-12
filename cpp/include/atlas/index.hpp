#pragma once
#include <cstdint>
#include <string>

namespace atlas {

class Mmap {
public:
    Mmap() = default;
    explicit Mmap(const std::string& path);
    ~Mmap();
    Mmap(Mmap&&) noexcept;
    Mmap& operator=(Mmap&&) noexcept;
    Mmap(const Mmap&) = delete;
    Mmap& operator=(const Mmap&) = delete;

    const uint8_t* data() const { return data_; }
    size_t size() const { return size_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    int fd_ = -1;
};

}  // namespace atlas
