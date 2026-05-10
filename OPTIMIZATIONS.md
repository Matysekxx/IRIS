# 🎯 Budoucí Optimalizace IRIS

Tento dokument mapuje cestu k absolutnímu vrcholu výkonu skriptovacích jazyků.

## 🏁 Krátkodobé cíle (Další fáze)

### 1. Memory Pools (Alokační Ráj)
*   **Cíl:** Odstranit režii `new` a `delete`.
*   **Realizace:** Implementace fixního poolu pro `StringData` a `ArrayData`. Alokace bude v podstatě spočívat jen v inkrementaci pointeru v rámci před-alokované stránky paměti.

### 2. Specialized String Interpolation
*   **Cíl:** Beatnout Python v konkatenaci.
*   **Realizace:** Přidání instrukce `OP_CONCAT_N`, která spojí více řetězců najednou. VM nejprve vypočítá celkovou délku výsledku, provede jednu velkou alokaci a pak do ní nakopíruje data (místo postupného zvětšování bufferu).

### 3. NaN Boxing (8-byte Value)
*   **Cíl:** Zmenšit `Value` z 16 bajtů na 8 bajtů.
*   **Realizace:** Využití standardu IEEE 754 pro double. Všechny hodnoty (včetně pointerů a intů) se schovají do speciálních "NaN" (Not-a-Number) hodnot. 
*   **Dopad:** Dvojnásobná hustota dat v cache, extrémní rychlost předávání argumentů.

---

## 🏔️ Dlouhodobé cíle (High-End)

### 4. Just-In-Time (JIT) Kompilace
*   **Cíl:** Překonání nativního kódu.
*   **Realizace:** Použití knihovny (např. **DynASM** nebo **LLVM**) pro generování strojového kódu za běhu. Nejčastěji spouštěné "hot" smyčky budou přeloženy přímo do instrukcí procesoru (x64).

### 5. Inline Caching (PIC) pro Volání Metod
*   **Cíl:** O(1) vyhledávání metod v objektech.
*   **Realizace:** Ukládání posledního nalezeného v-table offsetu přímo do bytecode proudu. Při dalším volání se jen porovná `classId`. Pokud sedí, skočí se přímo na adresu bez prohledávání mapy.

### 6. Escape Analysis
*   **Cíl:** Alokace objektů na stacku místo heapu.
*   **Realizace:** Kompilátor analyzuje, zda objekt opouští scope funkce. Pokud ne, vytvoří ho přímo v registrovém okně, čímž se zcela vyhne Garbage Collectoru/Reference Countingu.

### 7. SIMD Vectorization
*   **Cíl:** Paralelní zpracování polí.
*   **Realizace:** Využití instrukcí jako AVX2/AVX-512 pro operace nad poli (`int[]`, `double[]`). Jedna instrukce procesoru by tak mohla sečíst např. 8 čísel v poli najednou.
