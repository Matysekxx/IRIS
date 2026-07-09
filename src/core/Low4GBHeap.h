#ifndef LOW4GBHEAP_H
#define LOW4GBHEAP_H

#include <cstddef>

namespace iris { namespace core {

void* low4GBAlloc(size_t size);
void low4GBFree(void* ptr);

}} // namespace iris::core

#endif // LOW4GBHEAP_H
