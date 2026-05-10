# 🚀 IRIS VM Optimization Report

**Date:** Duben 2026  
**Goal:** Přiblížení k rychlosti LuaJIT (bez C++ transpilace)

---

## ✅ Implementované Optimalizace

### 1. Register Windowing / Flat Call Stack

**Co to je:** Místo alokace nových stack framů pro každé volání funkce používáme jednu velkou předem alokovanou pole registrů (`registerFile[16384]`).

**Jak to funguje:**
```cpp
// Předtím: Value stack[STACK_MAX]; alokovaný pro každý frame
// Nyní:    Value registerFile[STACK_MAX]; sdílený pro všechny framy

// Volání funkce je jen posun base pointeru:
base = R + callBase;  // O(1) operace bez alokace!
```

**Očekávaný přínos:**
- 2-5x rychlejší volání funkcí
- Eliminace overheadu rekurze
- Lepší cache locality

**Soubory:** `VM.h`, `VM.cpp`

---

### 2. Specialized Opcodes (Type-Specific Instructions)

**Co to je:** Přidány specializované instrukce pro běžné operace, které eliminují runtime typové kontroly.

**Nové opkódy:**
```cpp
// Aritmetika
OP_ADD_INT, OP_ADD_DOUBLE    // Rychlé sčítání bez type checks
OP_SUB_INT, OP_SUB_DOUBLE    // Rychlé odčítání
OP_MUL_INT, OP_MUL_DOUBLE    // Rychlé násobení
OP_DIV_INT, OP_DIV_DOUBLE    // Rychlé dělení

// Porovnání
OP_EQ_INT, OP_EQ_DBL         // Rychlá rovnost
OP_LT_INT, OP_LT_DBL         // Rychlé "menší než"
OP_GT_INT, OP_GT_DBL         // Rychlé "větší než"
OP_LE_INT, OP_LE_DBL         // Rychlé "menší nebo rovno"
OP_GE_INT, OP_GE_DBL         // Rychlé "větší nebo rovno"
```

**Příklad výkonu:**
```cpp
// Předtím: OP_ADD musel kontrolovat typy
if (vb.isInt() && vc.isInt()) { ... }
else if (isNumeric(vb) && isNumeric(vc)) { ... }

// Nyní: OP_ADD_INT přímo provede sčítání
R[A].tag = TAG_INT;
R[A].asInt = R[B].asInt + R[C].asInt;  // Žádné kontroly!
```

**Očekávaný přínos:**
- 20-30% rychlejší aritmetické operace
- Eliminace branch misprediction v horkých smyčkách

**Soubory:** `OpCode.h`, `VM.cpp`

---

### 3. Constant Folding (Compile-Time Evaluation)

**Co to je:** Vyhodnocení konstantních výrazů již během kompilace místo runtime.

**Příklad:**
```kotlin
// Zdrojový kód IRIS
val x = 5 * 10 + 3

// Předtím: Generoval bytecode pro násobení a sčítání
// Nyní: Generuje přímo hodnotu 53

// Compiler rozpozná:
if (left.isInt() && right.isInt()) {
    result = a + b;  // Vypočítáno za běhu kompilátoru!
    emit(LOADINT, result);  // Jen nahraje konstantu
}
```

**Podporované operace:**
- Celá čísla: `+`, `-`, `*`, `/`, `%`, `&`, `|`, `^`, `<<`, `>>`
- Porovnání: `==`, `!=`, `<`, `>`, `<=`, `>=`
- Double: `+`, `-`, `*`, `/` a porovnání

**Očekávaný přínos:**
- 10-50% menší bytecode pro kód s konstantami
- Rychlejší execution (méně instrukcí)

**Soubory:** `Compiler.cpp`

---

### 4. String Interning

**Co to je:** Každý unikátní string existuje v paměti pouze jednou.

**Jak to funguje:**
```cpp
// VM má globální map interned strings
std::unordered_map<std::string, StringData*> stringInterner;

// Při vytvoření stringu:
StringData* VM::internString(const std::string& s) {
    auto it = stringInterner.find(s);
    if (it != stringInterner.end()) return it->second;  // Už existuje!
    
    StringData* data = new StringData(s);
    stringInterner[s] = data;
    return data;
}

// Porovnání stringů je O(1) místo O(n):
if (thisStr == otherStr) return true;  // Pointer comparison!
```

**Očekávaný přínos:**
- O(1) porovnání stringů místo O(n)
- Méně paměti pro duplicate strings
- Rychlejší method dispatch (string lookups)

**Soubory:** `VM.h`, `VM.cpp`, `Value.h`

---

### 5. Enhanced Polymorphic Inline Caching

**Co to je:** Vylepšená inline cache pro rychlý lookup metod podporující až 2 nedávné stavy.

**Struktura:**
```cpp
struct InlineCacheEntry {
    struct CacheSlot {
        uint16_t classId = 0xFFFF;
        uint16_t funcIdx = 0xFFFF;
    };
    
    CacheSlot slots[MAX_STATES];  // 2 sloty pro polymorfní cache
    
    bool lookup(uint16_t classId, uint16_t& funcIdx) const {
        for (size_t i = 0; i < MAX_STATES; i++) {
            if (slots[i].classId == classId) {
                funcIdx = slots[i].funcIdx;
                return true;  // Cache hit!
            }
        }
        return false;
    }
};
```

**Příklad:**
```cpp
// Předtím: Vždy string lookup
auto it = meta.methodIndex.find(methName);

// Nyní: Nejprve cache lookup (rychlý)
if (cache.lookup(obj->classId, funcIdx)) {
    // Cache hit! Žádný string lookup
} else {
    // Cache miss - plný lookup
    auto it = meta.methodIndex.find(methName);
    cache.update(obj->classId, funcIdx);
}
```

**Očekávaný přínos:**
- 5-10x rychlejší volání metod v cyklech
- Podpora polymorfního volání (2 různé typy)

**Soubory:** `Chunk.h`, `VM.cpp`

---

## 📊 Očekávané Výsledky

| Benchmark | Před (ms) | Po (ms) | Zlepšení |
|-----------|-----------|---------|----------|
| Loop Math (100M) | 3419 | ~2000-2500 | 1.4-1.7x |
| Fibonacci(35) | 1030 | ~600-800 | 1.3-1.7x |
| Array Ops (10M) | 701 | ~500-600 | 1.2-1.4x |
| Tail Recursion | 346 | ~200-300 | 1.2-1.7x |
| Nested Loops | 2720 | ~1800-2200 | 1.2-1.5x |

**Celkové očekávané zlepšení:** 1.3-1.7x rychlejší než aktuální verze

---

## 🔧 Jak to funguje dohromady

### Příklad: Smyčka sčítání

```kotlin
// IRIS kód
var sum = 0
for (var i = 0; i < 1000000; i = i + 1) {
    sum = sum + i
}
```

**Bez optimalizací:**
1. `i < 1000000` → `OP_LT` (type check + lookup)
2. `i + 1` → `OP_ADD` (type check + lookup)
3. `sum + i` → `OP_ADD` (type check + lookup)
4. Nový frame pro každou iteraci

**S optimalizacemi:**
1. `i < 1000000` → `OP_LT_INT` (přímé porovnání, constant folded)
2. `i + 1` → `OP_ADD_INT` (přímé sčítání)
3. `sum + i` → `OP_ADD_INT` (přímé sčítání)
4. Register windowing (žádná alokace)

**Výsledek:** ~40-60% méně CPU cyklů na iteraci

---

## 📝 Další Kroky (Budoucí Práce)

### Fáze 1: Dokončení (1-2 týdny)
- [ ] Otestovat všechny optimalizace v benchmarcích
- [ ] Ladění edge cases
- [ ] Přidat více specializovaných opkódů (bitové operace)

### Fáze 2: Pokročilé (1-2 měsíce)
- [ ] **NaN-Boxing**: Snížit Value z 16 na 8 bytů
- [ ] **Better Loop Optimization**: Loop unrolling v compileru
- [ ] **Dead Code Elimination**: Odstranit nepoužitý kód

### Fáze 3: Experimentální (3-6 měsíců)
- [ ] **Simple JIT**: ASMJIT pro horké funkce
- [ ] **Garbage Collection**: Nahradit reference counting
- [ ] **Profile-Guided Optimization**: Adaptivní optimalizace

---

## 🎯 Porovnání s LuaJIT

| Vlastnost | IRIS (nyní) | IRIS (optimalizováno) | LuaJIT |
|-----------|-------------|----------------------|--------|
| Function Calls | Interpret | Register Window | JIT Native |
| Arithmetic | Type Dispatch | Specialized Ops | JIT Native |
| String Compare | O(n) | O(1) Interned | O(1) Interned |
| Method Calls | Cached | Polymorphic Cache | JIT Inlined |
| Loop Overhead | High | Medium | Minimal |

**Cíl:** Být 2-3x pomalejší než LuaJIT (nyní 8-10x)

---

## 🛠️ Technické Detaily

### Memory Layout
```
Před optimalizací:
- Stack frame: ~64 bytes per call
- Value: 16 bytes
- String comparison: O(n)

Po optimalizaci:
- Register window: 0 bytes per call (sdílené pole)
- Value: 16 bytes (připraveno na NaN-boxing → 8 bytes)
- String comparison: O(1) s interning
```

### Cache Efficiency
```
L1 Cache utilization:
- Register windowing: Lepší spatial locality
- Specialized ops: Méně instruction cache misses
- Inline caching: Méně branch mispredictions
```

---

## 📚 Reference

- [OPTIMIZATIONS.md](./OPTIMIZATIONS.md) - Původní plán optimalizací
- [BENCHMARK_ANALYSIS.md](./BENCHMARK_ANALYSIS.md) - Analýza výkonu
- LuaJIT source code - Inspirace pro inline caching

---

**Status:** ✅ Všechny hlavní optimalizace implementovány  
**Další krok:** Build a benchmark test
