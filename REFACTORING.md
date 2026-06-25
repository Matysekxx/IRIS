# IRIS Refaktoring & Optimalizace – Souhrn změn

## 1. Reorganizace adresářové struktury

Původní `src/bytecode/` a `src/parser/`/`src/node/` byly rozděleny do specializovaných adresářů:

- `src/frontend/` – Parser, Token, NodeFactory, ASTNode (vše pro lexing/parsing)
- `src/ir/` – OpCode, Chunk, Compiler, **nový PeepholeOptimizer**
- `src/vm/` – VM, Trace, TraceOptimizer
- `src/jit/` – JITCompiler, JITHelpers
- `src/core/` – Value, Managed, ArrayData, MemoryPool, Variable, Native, **nový GC**

**Build systém aktualizován:**
- `CMakeLists.txt` – přidán `src/` do include paths (`target_include_directories`)
- `build.zig` – aktualizován seznam zdrojových souborů (`iris_sources`)

---

## 2. Garbage Collector – vyčlenění a vylepšení

**Nové soubory:**
- `src/core/GC.h`
- `src/core/GC.cpp`

### Co se změnilo:
- **GC byl extrahován** z `Value.cpp` do samostatných souborů.
- Zachována zpětná kompatibilita: globální `markValue()` a `collectGC()` fungují jako dřív.
- Připravena infrastruktura pro **generational GC** (`minorCollect` / `majorCollect`).
- Všechny managed objekty (`StringData`, `ObjectData`, `ArrayData`, `NativeObject`) nyní používají `MemoryPool` pro rychlou alokaci.
- `NativeObject` rozšířen o virtuální metodu `mark()` – umožňuje nativním objektům označit své vlastní reference při GC.

---

## 3. VM – odstranění tracing overheadu

**Soubor:** `src/vm/VM.cpp`, `src/vm/Trace.h`

### Problém:
`NEXT()` makro v hlavní dispatch smyčce provádělo **v každé instrukci**:
1. Volání `traceManager.isTracing()`
2. Čtení typových tagů z registrů (`R[_A].bits >> 48`)
3. Zápis do trace bufferu

To způsobovalo obrovský overhead i když tracing nebyl aktivní.

### Řešení:
- **Typové tagy (`tA`, `tB`, `tC`) odstraněny z `NEXT()` makra.** Trace nyní zaznamenává jen opkód a PC (`recordFast`), nikoliv typy registrů. Typové informace se získají až při kompilaci trace z `initialTypes` a guardů.
- Makro `NEXT()` je nyní **výrazně kompaktnější** – méně branch prediction missů.
- `TraceManager` používá přímo public `bool tracingFlag` místo metody `isTracing()`, což je jednoduché čtení bez function call overheadu.

---

## 4. Peephole Optimizer – samostatná komponenta

**Nové soubory:**
- `src/ir/PeepholeOptimizer.h`
- `src/ir/PeepholeOptimizer.cpp`

### Co dělá:
- Odstranění redundantních `MOVE` (např. `MOVE R1, R1` → NOP)
- **Instruction fusion**:
  - `LT_INT + JMPF` → `JGE_INT` (přímé skoky bez mezivýsledku)
  - `LOADBOOL 0 + JMPF` → `JMP` (vždy skok)
  - `LOADK + ADD` → `ADD_K` (konstantní operand)
- **Constant folding**:
  - `LOADINT k1; LOADINT k2; ADD_INT` → `LOADINT (k1+k2)`
- **Immediate fusion**:
  - `LOADINT imm; ADD_INT R2, R2, R1` → `ADDI_W R2, imm`

---

## 5. JIT optimalizace

**Soubor:** `src/jit/JITCompiler.cpp`, `src/vm/VM.cpp`

### Změny:
1. **Threshold pro JIT kompilaci funkcí zvýšen z 1000 na 5000 volání.**
   - Zabraňuje zbytečné JIT kompilaci krátkých běhů.
   - Ušetří čas strávený kompilací pro cold code.

2. **Trace JIT – inline int aritmetika (fast path):**
   - Pokud `compileTrace()` ví (z type guardů), že oba operandy `OP_ADD`/`OP_SUB`/`OP_MUL` jsou **unboxed int**, generuje přímo inline `add`/`sub`/`imul` instrukce místo volání C++ helperu `addHelper`/`subHelper`/`mulHelper`.
   - Toto dramaticky zrychluje hot loops, kde typy jsou stabilní.

---

## 6. Value – opravy a optimalizace

**Soubor:** `src/core/Value.cpp`

### Opravy:
- **String comparison bug:** `if (isSSO() && o.isSSO()) return false;` byl špatně – dvě SSO stejné délky se měly porovnat, ale vždy vracely `false`. Opraveno na přímé porovnání payloadu (`bits & mask`).
- **String comparison optimalizace:** Eliminovány zbytečné kopie do bufferů `buf1[8]`/`buf2[8]` pro SSO porovnání. Nyní se porovnávají přímo bity (pro stejnou délku).

### GC kód odstraněn:
- `markValue`, `collectGC`, `ObjectData::operator new/delete`, `StringData::operator new/delete` přesunuty do `GC.cpp`.

---

## 7. Zbývající doporučení pro další vývoj

Aby jazyk skutečně konkuroval LuaJIT, doporučuji následující kroky:

### A. JIT Compiler – další vylepšení
- **Inline cache pro field access** v `JITCompiler::compile()` – nyní se vždy volá `getField` přes pointer arithmetic, ale JIT by mohl inline `GET_FIELD` pro známé offsety.
- **Inline aritmetika i v `compile()`** (ne jen `compileTrace()`): Přidat fast-path pro int operandy pomocí runtime type checků.
- **Register allocation**: Místo fixed 5 vregs použít linear scan nebo graph coloring.

### B. Garbage Collector
- Implementovat skutečný **generational GC** s nursery (copying collector) a mature (mark-sweep).
- Přidat **write barrier** pro přechody z nursery do mature.
- Zvážit **incremental marking** pro lepší latenci.

### C. VM Dispatch
- **Superinstructions**: Přidat kombinované opkódy jako `LOADINT+ADD_INT` → `LOADADD_INT`, `JMPF+LOOP` → `LOOP_COND`, atd.
- **Threaded code**: Místo `goto *d[op]` použít přímé ukazatele na labels (již částečně implementováno pro GCC).

### D. Type System
- `OP_TYPECHECK` v současnosti nedělá nic (`NEXT()`). Přidat runtime type guardy, které mohou být využity JIT kompilátorem pro type specialization.
- **Monomorphic inline cache** pro `OP_GET_FIELD` a `OP_SET_FIELD` (podobně jako `OP_INVOKE_MONO`).

### E. Memory Layout
- `ArrayData` by mělo podporovat **dense arrays** pro int/double bez GC overheadu.
- `ObjectData` by mělo používat **shape-based hidden classes** (jako V8/SpiderMonkey) pro rychlejší field access a menší memory footprint.

---

## Souhrn

Projekt IRIS byl kompletně přeorganizován, zoptimalizován a připraven na další vývoj:
- ✅ Adresářová struktura rozdělena na specializované moduly (`frontend`, `ir`, `vm`, `jit`, `core`)
- ✅ Build systém (CMake + Zig) aktualizován
- ✅ GC vyčleněn do samostatné komponenty s přípravou na generational collector
- ✅ VM tracing overhead výrazně snížen (`recordFast` bez typových čtení)
- ✅ Peephole optimizer extrahován a vylepšen (fusion, constant folding)
- ✅ JIT threshold optimalizován, trace JIT získal inline int aritmetiku
- ✅ Value string comparison opraveno a zrychleno
- ✅ Všechny include cesty konzistentní
