#include <stdint.h>

long __tiny_div64(long a, long b) {
  if (b == 0) {
    return 0;
  }
  return a / b;
}

long __tiny_mod64(long a, long b) {
  if (b == 0) {
    return 0;
  }
  return a % b;
}
