#include "libcxx_tiny.hpp"

namespace tiny {

VectorInt::~VectorInt() { tiny_free(data_); }

void VectorInt::reserve(size_t want) {
  if (want <= cap_) {
    return;
  }
  size_t next = cap_ == 0 ? 4 : cap_ * 2;
  while (next < want) {
    next *= 2;
  }
  int* fresh = static_cast<int*>(tiny_malloc(next * sizeof(int)));
  if (!fresh) {
    tiny_abort();
  }
  if (data_) {
    tiny_memcpy(fresh, data_, size_ * sizeof(int));
    tiny_free(data_);
  }
  data_ = fresh;
  cap_ = next;
}

void VectorInt::push_back(int value) {
  reserve(size_ + 1);
  data_[size_] = value;
  size_++;
}

int VectorInt::at(size_t i) const {
  if (i >= size_) {
    tiny_abort();
  }
  return data_[i];
}

}  // namespace tiny

extern "C" long tiny_vec_demo_sum(void) {
  tiny::VectorInt v;
  v.push_back(1);
  v.push_back(2);
  v.push_back(3);
  v.push_back(4);
  long sum = 0;
  for (size_t i = 0; i < v.size(); i++) {
    sum += v.at(i);
  }
  return sum;
}

void* operator new(size_t size) {
  void* p = tiny_malloc(size);
  if (!p) {
    tiny_abort();
  }
  return p;
}

void operator delete(void* ptr) noexcept { tiny_free(ptr); }

void operator delete(void* ptr, size_t) noexcept { tiny_free(ptr); }
