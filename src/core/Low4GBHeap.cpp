#include "Low4GBHeap.h"
#include <windows.h>

namespace iris { namespace core {

void* low4GBAlloc(size_t size) {
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void low4GBFree(void* ptr) {
    VirtualFree(ptr, 0, MEM_RELEASE);
}

}} // namespace iris::core
