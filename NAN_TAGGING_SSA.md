# IRIS: Architektura další generace (NaN Tagging & SSA)

Tento dokument slouží jako technický návrh a referenční implementace pro další velký skok ve výkonu IRISu, který nás plně srovná s LuaJITem.

## 1. NaN Tagging (Small Value Optimization)

Aktuálně má `iris::core::Value` 16 bytů. To znamená, že při průchodu polem 1M prvků procesor načítá dvojnásobné množství dat z RAM do Cache. 
NaN Tagging využívá faktu, že v 64-bitovém IEEE-754 `double` formátu existuje obrovské množství neplatných hodnot (NaN), do kterých můžeme bezpečně ukrýt pointery (48-bit na x64) a tagy.

### Referenční implementace (`ValueNan.h`):
```cpp
#include <cstdint>

namespace iris::core {
    /**
     * @brief 8-byte NaN-Tagged Value.
     * 
     * 64-bit mask structure:
     * - Double: 0x0000000000000000 to 0xFFF0000000000000 (Standard IEEE 754)
     * - Pointers: 0xFFFC000000000000 + 48-bit address
     * - Integer: 0xFFF9000000000000 + 32-bit int
     * - Boolean: 0xFFFA000000000000 + 1-bit
     * - Null:    0xFFFB000000000000
     */
    struct ValueNan {
        uint64_t asBits;

        static constexpr uint64_t QNAN = 0x7FFC000000000000;
        static constexpr uint64_t SIGN_BIT = 0x8000000000000000;
        
        static constexpr uint64_t TAG_INT = 1;
        static constexpr uint64_t TAG_BOOL = 2;
        static constexpr uint64_t TAG_NULL = 3;
        static constexpr uint64_t TAG_PTR = 4;

        ValueNan() : asBits(QNAN | ((uint64_t)TAG_NULL << 48)) {}
        
        explicit ValueNan(double d) {
            // C++20 bit_cast would be safer, but this illustrates the concept
            asBits = *reinterpret_cast<uint64_t*>(&d); 
        }
        
        explicit ValueNan(int i) {
            asBits = QNAN | ((uint64_t)TAG_INT << 48) | (uint32_t)i;
        }
        
        explicit ValueNan(void* ptr) {
            asBits = QNAN | SIGN_BIT | ((uint64_t)TAG_PTR << 48) | (reinterpret_cast<uint64_t>(ptr) & 0x0000FFFFFFFFFFFF);
        }

        inline bool isDouble() const { return (asBits & QNAN) != QNAN; }
        inline bool isInt() const { return (asBits >> 48) == (QNAN >> 48 | TAG_INT); }
        inline bool isPtr() const { return (asBits & SIGN_BIT) != 0; }
        
        inline int asInt() const { return static_cast<int>(asBits & 0xFFFFFFFF); }
        inline double asDouble() const { return *reinterpret_cast<const double*>(&asBits); }
        inline void* asPtr() const { return reinterpret_cast<void*>(asBits & 0x0000FFFFFFFFFFFF); }
    };
}
```
**Přínos:** Velikost `Value` klesne z 16 bytů na 8 bytů. Tím se zdvojnásobí kapacita L1 cache procesoru a zrychlí se přesuny registrů `OP_MOVE`.

## 2. SSA (Static Single Assignment) v JIT Kompilátoru

Náš aktuální JIT (`MicroJIT`) je naivní – čte a zapisuje z paměti (`rBase`) při každé instrukci. LuaJIT používá SSA k tomu, aby proměnné udržel v registrech CPU.

### Princip implementace v IRISu:
1.  **Trace Recording**: VM nebude kompilovat statické metody, ale bude "nahrávat" (tracing) horké smyčky za běhu, čímž vznikne lineární kód bez složitých větvení.
2.  **SSA Převod**: Nahraný bytecode se převede do formátu, kde je každá proměnná přiřazena pouze jednou (např. `R0_1 = R0_0 + 1`).
3.  **Register Allocation (Linear Scan)**: SSA proměnné se namapují na fyzické registry `eax`, `ebx`, `ecx`. Do paměti (`rBase`) se zapisuje až ve chvíli, kdy JIT opouští trace (tzv. "Guard Fail").

**Závěr:** NaN-Tagging a SSA JIT jsou poslední dva kroky potřebné k tomu, aby IRIS dosáhl magické hranice výkonu LuaJITu (jednotky milisekund pro milionové iterace).
