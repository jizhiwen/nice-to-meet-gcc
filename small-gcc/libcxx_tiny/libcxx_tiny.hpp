#ifndef LIBCXX_TINY_HPP
#define LIBCXX_TINY_HPP

#include <stddef.h>
#include <stdint.h>

extern "C" {
#include "../libc_tiny/libc_tiny.h"
}

namespace tiny {

class StringView {
 public:
  explicit StringView(const char* s) : data_(s), size_(tiny_strlen(s)) {}
  const char* data() const { return data_; }
  size_t size() const { return size_; }

 private:
  const char* data_;
  size_t size_;
};

class VectorInt {
 public:
  VectorInt() : data_(nullptr), size_(0), cap_(0) {}
  ~VectorInt();
  void push_back(int value);
  int at(size_t i) const;
  size_t size() const { return size_; }

 private:
  void reserve(size_t want);

  int* data_;
  size_t size_;
  size_t cap_;
};

}  // namespace tiny

extern "C" long tiny_vec_demo_sum(void);
void* operator new(size_t size);
void operator delete(void* ptr) noexcept;
void operator delete(void* ptr, size_t) noexcept;

#endif
