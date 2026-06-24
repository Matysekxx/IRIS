# IRIS Optimalizace pro konkurenceschopnost s LuaJIT

## Aktuální výkonnost (GCC build vs LuaJIT vs Python)

| Benchmark         | IRIS (GCC) | LuaJIT    | Python    |
|-------------------|-----------|-----------|-----------|
| array_sum         | 33.59 ms  | 70.79 ms  | 162.31 ms |
| fib (rekurze)     | 2.46 ms   | 61.21 ms  | 81.44 ms  |
| hashmap_ops       | 7.54 ms   | 62.38 ms  | 79.88 ms  |
| loop              | 318.47 ms | 60.99 ms  | 395.03 ms |
| math_heavy        | 348.62 ms | 154.60 ms | 1457.51 ms|
| sieve             | 86.58 ms  | 80.22 ms  | 120.76 ms |
| stress_objects    | 68.49 ms  | 71.36 ms  | 297.85 ms |
| string_concat     | 64.98 ms  | 54.77 ms  | 72.99 ms  |

IRIS je již rychlejší než CPython v 8/9 benchmarků a v některých případech i rychlejší než LuaJIT (`array_sum`, `fib`, `hashmap_ops`, `stress_objects`). Hlavní slabiny jsou `loop` (5x pomalejší) a `math_heavy` (2x pomalejší).

---

## 1. VM Interpret

### 1.1 Superinstrukce (VM.cpp ~337-400)
Fúze často párovaných instrukcí do jedné:
- `LOADINT R,imm + ADD_INT R,R,R` → jedna instrukce
- `LOADINT R,imm + IDX_SET V,R,I` → jedna instrukce
- Ušetří 1 dispatch na každé fúzi (~5-10% zrychlení)

### 1.2 Snížit režii tracingu v NEXT() (VM.cpp:249-265)
Každý dispatch volá `updateTracing()`, i když tracing není aktivní. Stačí kontrolovat jednou za N instrukcí (např. každých 256) podobně jako `CHECK_GC()`.

### 1.3 Cache DECODE_ABC() (VM.cpp:274)
Místo dekompozice instrukce v každém case předdekódovat A/B/C/Cx při NEXT() a uložit do lokálních proměnných.

### 1.4 Rozdělit monolitickou run() (VM.cpp:200-1068)
Rozdělit do menších funkcí podle kategorie (aritmetika, paměť, řízení toku). Sníží I-cache pressure.

---

## 2. JIT Kompilátor

### 2.1 Zvýšit počet virtuálních registrů (JITCompiler.cpp:28)
Aktuálně jen **5 mapovaných x86-64 registrů**. Zvýšit na 12-16. Většina funkcí používá <8 registrů; spilling do paměti zabíjí výkon.

### 2.2 Implementovat chybějící operace v compile() (JITCompiler.cpp:157)
Aktuálně desítky opcode mají `default: break;` -- JIT pro ně negeneruje kód. Prioritně:
- `OP_NOT`, `OP_AND`, `OP_OR` → inline and/or/not + tagování
- `OP_BIT_AND/OR/XOR/SHL/SHR` → inline bitové instrukce
- `OP_NEG`, `OP_INC`, `OP_DEC` → inline

### 2.3 Unboxování integerů v compile() (JITCompiler.cpp)
Zkopírovat logiku z `compileTrace()` – typové guardy na vstupu funkce, které unboxují integery do nativních registrů. Vyhne se opakovanému maskování tagů.

### 2.4 Inlining volání (JITCompiler.cpp:92-96)
Aktuálně `OP_CALL` volá `callFunctionHelper` → re-entry do interpreteru. Inlinovat:
- U JIT-kompilovaných callee: uložit registry, větvit přímo do nativního kódu callee
- U malých funkcí: zcela inlinovat tělo (eliminuje overhead volání)

### 2.5 Polymorfní inline cache (JITCompiler.cpp:86-91)
`OP_INVOKE_MONO` má 1 slot. Pro polymorfní místa přidat PIC (2-4 sloty) s guardem na `classId` a fallbackem.

### 2.6 GC roots v JIT (nový kód)
JIT funkce musí registrovat své stack/register hodnoty jako GC roots. Jinak GC nevidí objekty držené v JIT registrech → může je předčasně uvolnit.

---

## 3. Trace Optimizer (TraceOptimizer.cpp)

### 3.1 Eliminace kontrol pole (TraceOptimizer.cpp)
V tracech typu `for i=0..n-1: arr[i]` je kontrola indexu zbytečná – délka pole `n` známa, i vždy v rozsahu. Eliminovat bounds check.

### 3.2 Eliminace null checků
Po `OP_NEW_OBJ` je objekt garantovaně non-null → `GET_FIELD` nepotřebuje null check.

### 3.3 Redukce síly
- `i * 2` → `i << 1`
- `i / 4` → `i >> 2`
- `i % 8` → `i & 7`
Provést v hot tracech.

### 3.4 Konstantní propagace skrz trace
Pokud LICM přesune `LOADINT R,k` do preamble, propagovat konstantu skrz aritmetické operace.

### 3.5 Typová specializace skrz volání (TraceOptimizer.cpp:131-134)
Aktuálně se po každém volání resetují všechny typy. Přidat jednoduchou analýzu side-effectů: pokud callee nemodifikuje registr, typ zůstává známý.

---

## 4. Garbage Collector

### 4.1 Pravá generacionalita (GC.cpp:123-127)
- `minorCollect()`: kopírovací nursery (semi-space) pro mladé objekty
- Objekty přeživší X kolekcí → promotion do mature space (mark-sweep)
- Vyžaduje **write barrier** na všech pointer storech

### 4.2 Write barrier
Při každém zápisu pointeru do pole/objektu: pokud target je v mature a zdroj v nursery, zaznamenat referenci pro příští minor GC.

### 4.3 Incremental marking (GC.cpp:101-121)
Rozdělit mark fázi na malé slice vkládané mezi instrukce. Tri-color marking (white/grey/black). Eliminuje long pause times.

### 4.4 Paměťové pooly pro overflow fields (Value.h:170)
`overflowFields` používá `new Value[]` – měl by používat `MemoryPool` jako ostatní alokace.

---

## 5. Peephole Optimizer (PeepholeOptimizer.cpp)

### 5.1 Jump threading
- `JMP L1; L1: JMP L2` → `JMP L2`
- `JMPF R, L1; L1: JMP L2` → `JMPF R, L2`

### 5.2 Dead store elimination
Pokud výsledek `LOADINT`/`LOADK` je přepsán bez přečtení, instrukci odstranit.

### 5.3 Kopírovací propagace
- `MOVE R_a, R_b; MOVE R_c, R_a` → `MOVE R_c, R_b`

### 5.4 Algebraická zjednodušení
- `ADD_INT R, R, 0` → `NOP`
- `MUL_INT R, R, 1` → `NOP`
- `NEG; NEG` → `NOP`

---

## 6. Kompilátor (Compiler.cpp)

### 6.1 Eliminace kontrol rozsahu (Compiler.cpp:1363-1388)
Při průchodu `for i=0..len(a)-1: a[i]` kompilátor ví, že i je vždy v rozsahu → vynechat bounds check.

### 6.2 Rozšířit unrolling (Compiler.cpp:310-321)
Aktuálně jen `repeat(n)` pro n<=8. Přidat unrolling for cyklů s malým/nízkým konstantním počtem iterací.

### 6.3 Tail call optimalizace
Už implementována pro přímá volání. Rozšířit na metody a dynamická volání, kde je callee znám.

---

## 7. Bytový kód / SSA

### 7.1 SSA IR pro optimalizující JIT (nový modul)
Místo kompilace přímo z bytecode trace, převést trace do SSA formy. Umožní:
- GVN (Global Value Numbering)
- Konstantní propagaci
- Eliminaci redundantních výpočtů
- Alias analýzu

### 7.2 Shapes (hidden classes) pro objekty (ObjectData redesign)
Místo fixních field indexů použít hidden classes (jako V8). Objekty se stejnými fieldy sdílejí shape → field access je: load shape → load offset → load from base. Umožní inline caching a rychlejší polymorfní přístup.

---

## 8. Běhové prostředí

### 8.1 Bytecode cache (nový modul)
Serializovat zkompilovaný `Chunk` na disk. Načítat podle hash souboru. Odstraní re-parsing a re-kompilaci.

### 8.2 Baseline JIT (nový kód)
Nemusíme čekat na 5000 volání. Na první volání funkce vygenerovat baseline JIT kód s minimum optimalizací. Tiered: interpreter → baseline JIT → optimizing trace JIT.

---

## 9. Standardní knihovna a FFI

### 9.1 Přímé volání C funkcí (nový FFI modul)
Použít AsmJit k dynamickému generování call stubů pro libovolné C funkce. Podpora struct return values a pointer arguments.

### 9.2 Async IO
Wrapovat Windows IOCP nebo Linux io_uring. Implementovat jednoduchý event loop.

---

## 10. Implementační priority

### Phase 1 (rychlé výhry, ~1-2 týdny)
- [x] Opravit chybějící opcode v JIT compile()
- [x] Unboxování integerů v JIT compile()
- [x] Cache DECODE_ABC()
- [x] Snížit režii tracingu
- [x] Jump threading + dead store v peephole

### Phase 2 (střední, ~2-4 týdny)
- [ ] Superinstrukce v interpretu
- [ ] Inlining volání v JIT
- [ ] PIC pro polymorfní metody
- [ ] GC roots v JIT
- [ ] Rozšířit počet JIT registrů na 12+

### Phase 3 (komplexní, ~1-2 měsíce)
- [ ] Generační GC s write barrier
- [ ] SSA IR pro trace JIT
- [ ] Shapes (hidden classes)
- [ ] Baseline JIT (tiered compilation)
- [ ] Bytecode cache

### Phase 4 (dlouhodobé, ~3-6 měsíců)
- [ ] Alokation sinking / escape analysis
- [ ] Auto-vectorizace
- [ ] Async IO
- [ ] Přímé FFI volání C funkcí
- [ 】 Incremental marking v GC
