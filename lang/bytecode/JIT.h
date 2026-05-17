#ifndef JIT_H
#define JIT_H

#include <vector>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#endif

namespace iris::bytecode {
    /**
     * @brief A simple x64 JIT compiler prototype.
     */
    class MicroJIT {
        uint8_t* codeBuffer;
        size_t offset = 0;
    public:
        MicroJIT() {
            codeBuffer = (uint8_t*)VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        }

        ~MicroJIT() {
            if (codeBuffer) VirtualFree(codeBuffer, 0, MEM_RELEASE);
        }

        /** @brief Emits a single byte. */
        void emit8(uint8_t b) { codeBuffer[offset++] = b; }
        
        /** @brief Emits a 32-bit dword. */
        void emit32(uint32_t d) { *(uint32_t*)(codeBuffer + offset) = d; offset += 4; }

        /** @brief Compiles a simple increment loop: while(i < limit) { x++; i++; } */
        typedef void (*LoopFunc)(int* x, int* i, int limit);
        
        LoopFunc compileSimpleLoop() {
            size_t start = offset;
            // x64 Assembly:
            // mov eax, [rdx] (load i)
            // cmp eax, r8d (compare with limit)
            // jge end
            // start_loop:
            // inc dword ptr [rcx] (x++)
            // inc eax (i++)
            // cmp eax, r8d
            // jl start_loop
            // mov [rdx], eax (save i)
            // ret

            emit8(0x8B); emit8(0x02); // mov eax, [rdx]
            emit8(0x41); emit8(0x3B); emit8(0xC0); // cmp eax, r8d
            emit8(0x7D); emit8(0x0A); // jge end (+10 bytes)
            
            size_t loop_start = offset;
            emit8(0xFF); emit8(0x01); // inc dword ptr [rcx]
            emit8(0xFF); emit8(0xC0); // inc eax
            emit8(0x41); emit8(0x3B); emit8(0xC0); // cmp eax, r8d
            emit8(0x7C); emit8(0xF7); // jl start_loop (-9 bytes)
            
            emit8(0x89); emit8(0x02); // mov [rdx], eax
            emit8(0xC3); // ret
            
            return (LoopFunc)(codeBuffer + start);
        }
    };
}

#endif //JIT_H
