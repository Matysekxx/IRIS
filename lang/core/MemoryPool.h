#ifndef MEMORYPOOL_H
#define MEMORYPOOL_H

#include <vector>

/**
 * @brief Memory Pool for fast allocation of fixed-size objects.
 * 
 * OPTIMIZATION: Eliminates malloc/free overhead for frequently allocated objects.
 * Uses a chunk-based allocation strategy with O(1) allocate/deallocate.
 * 
 * @tparam T Type of object to allocate
 * @tparam ChunkSize Number of objects per chunk (default: 256)
 */
template<typename T, size_t ChunkSize = 256>
class MemoryPool {
    struct Chunk {
        char data[sizeof(T) * ChunkSize];
        Chunk *next = nullptr;
    };

    struct FreeNode {
        FreeNode *next = nullptr;
    };

    std::vector<Chunk *> chunks;
    FreeNode *freeList = nullptr;
    Chunk *currentChunk = nullptr;
    size_t currentIndex = 0;

public:
    MemoryPool() {
        allocateChunk();
    }

    ~MemoryPool() {
        for (Chunk *chunk: chunks) {
            delete[] reinterpret_cast<char *>(chunk);
        }
    }

    /**
     * @brief Allocate a new object from the pool.
     * @return Pointer to uninitialized memory for T
     */
    T *allocate() {
        if (freeList) {
            // Reuse from free list
            T *ptr = reinterpret_cast<T *>(freeList);
            freeList = freeList->next;
            return ptr;
        }

        if (currentIndex >= ChunkSize) {
            allocateChunk();
        }

        T *ptr = reinterpret_cast<T *>(currentChunk->data + currentIndex * sizeof(T));
        currentIndex++;
        return ptr;
    }

    /**
     * @brief Return an object to the pool.
     * @param ptr Pointer to object to deallocate
     */
    void deallocate(T *ptr) {
        // Add to free list
        FreeNode *node = reinterpret_cast<FreeNode *>(ptr);
        node->next = freeList;
        freeList = node;
    }

    /**
     * @brief Reset the pool (invalidate all allocations).
     * Faster than individual deallocations.
     */
    void reset() {
        // Keep first chunk, reset index
        for (size_t i = 1; i < chunks.size(); i++) {
            delete[] reinterpret_cast<char *>(chunks[i]);
        }
        chunks.resize(1);
        currentChunk = chunks[0];
        currentIndex = 0;
        freeList = nullptr;
    }

    /**
     * @brief Get number of allocated chunks.
     */
    size_t chunkCount() const {
        return chunks.size();
    }

private:
    void allocateChunk() {
        Chunk *chunk = reinterpret_cast<Chunk *>(new char[sizeof(Chunk)]);
        chunk->next = nullptr;
        chunks.push_back(chunk);
        currentChunk = chunk;
        currentIndex = 0;
    }
};

/**
 * @brief StringData pool - optimized for short-lived strings.
 */
class StringPool {
    struct Block {
        static constexpr size_t DATA_SIZE = 64;
        char data[DATA_SIZE];
        Block *next = nullptr;
        size_t used = 0;
    };

    std::vector<Block *> blocks;
    Block *currentBlock = nullptr;

public:
    StringPool() {
        allocateBlock();
    }

    ~StringPool() {
        for (Block *block: blocks) {
            delete[] reinterpret_cast<char *>(block);
        }
    }

    char *allocate(size_t size) {
        // Align to 8 bytes
        size = (size + 7) & ~7;

        if (currentBlock->used + size > Block::DATA_SIZE) {
            allocateBlock();
        }

        char *ptr = currentBlock->data + currentBlock->used;
        currentBlock->used += size;
        return ptr;
    }

    void reset() {
        for (size_t i = 1; i < blocks.size(); i++) {
            delete[] reinterpret_cast<char *>(blocks[i]);
        }
        blocks.resize(1);
        currentBlock = blocks[0];
        currentBlock->used = 0;
    }

private:
    void allocateBlock() {
        Block *block = reinterpret_cast<Block *>(new char[sizeof(Block)]);
        block->used = 0;
        blocks.push_back(block);
        currentBlock = block;
    }
};

#endif //MEMORYPOOL_H
