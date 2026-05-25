# Pokročilé optimalizace jazyka IRIS (Návrh rozvoje)

Tento dokument shrnuje doporučené architektonické a kompilátorové optimalizace pro další fázi vývoje hybridního jazyka **IRIS**. Cílem těchto změn je přiblížit výkon IRIS nativnímu C++ a překonat výkon moderních JIT překladačů jako LuaJIT.

---

## 1. Static Single Assignment (SSA) IR v JIT
V současnosti kompilátor IRIS generuje lineární bytecode, který JIT překládá přímo do x64 instrukcí. Pro pokročilé optimalizace je klíčové zavést mezireprezentaci v **SSA (Static Single Assignment)** formě.

* **Co to je:** Každá proměnná je přiřazena právě jednou. Vícecestné větvení kódu se řeší pomocí $\phi$ (phi) uzlů.
* **Výhody:**
  * **Globální optimalizace:** Umožňuje implementovat Dead Code Elimination (DCE), Common Subexpression Elimination (CSE) a Loop-Invariant Code Motion (LICM).
  * **Efektivní Register Allocation:** Umožňuje nasadit pokročilý algoritmus **Linear Scan Register Allocation** (či graph coloring), což minimalizuje přesuny dat mezi registy a pamětí (spilly).

---

## 2. Analýza úniků (Escape Analysis) & Alokace na zásobníku
V současnosti jsou objekty a pole v IRIS alokovány výhradně na haldě (Heap) a spravovány pomocí čítačů referencí (Reference Counting). To s sebou nese vysokou režii na volání `malloc`/`free` a neustálé aktualizace čítačů referencí.

* **Princip:** Statická analýza kompilátoru zkontroluje, zda alokovaný objekt "uniká" (escape) mimo scope dané funkce (např. zda je vrácen jako návratová hodnota nebo uložen do globální proměnné).
* **Optimalizace:** Pokud objekt **neuniká**, JIT jej alokuje přímo na **zásobníku (Stack Allocation)**. 
* **Přínos:**
  * Nulové volání alokátoru haldy.
  * Úplné odstranění instrukcí pro Reference Counting (`retain`/`release`) pro daný objekt.
  * Dramatické snížení zatížení procesorové cache.

---

## 3. Bounds Check Elimination (BCE)
Při čtení z pole (`arr[i]`) musí virtuální stroj nebo JIT ověřit, zda je index `i` v mezích pole (`0 <= i < length`). V hotových smyčkách (např. Matrix Multiplication, Bubble Sort) to znamená neustálé větvení kódu (`cmp` a `jge`).

* **Princip:** Kompilátor analyzuje rozsahy indexů v cyklech. Pokud dokáže staticky dokázat, že řídicí proměnná cyklu nikdy nepřekročí velikost pole, tyto kontroly z výsledného kódu zcela **odstraní**.
* **Příklad:**
  ```iris
  val arr = new int[100]
  for (var i = 0; i < 100; i = i + 1) {
      arr[i] = i // Zde lze bounds check 100% eliminovat!
  }
  ```
* **Přínos:** Odstranění podmíněných skoků z vnitřních smyček zrychlí řazení a maticové operace o dalších **15–30 %**.

---

## 4. Polymorphic Inline Cache (PIC)
IRIS podporuje třídy a dynamický dispatch metod. Volání metod přes virtuální tabulky (nebo vyhledávání podle jména) je pomalé.

* **Princip:** 
  * **Monomorphic Inline Cache:** JIT si na daném místě volání (call site) zapamatuje třídu objektu a cíl volání z minulé iterace. Pokud se třída shoduje, skočí se přímo na zkompilovaný kód (rychlé provázání).
  * **Polymorphic Inline Cache:** Pokud se na stejném místě střídá více tříd (např. 2–3), JIT vygeneruje krátký vetvený kód (stub), který porovná ClassID s několika uloženými možnostmi a provede přímý skok.
* **Přínos:** Volání metod v polymorfních hierarchiích se zrychlí až o **300 %** a přiblíží se rychlosti nativního C++.

---

## 5. Optimalizace Reference Countingu (Coalescing)
Časté aktualizace čítačů referencí (`refCount++` a `refCount--`) způsobují masivní zápisy do paměti a invalidují L1/L2 cache procesoru v multithreadovém prostředí.

* **Optimalizace:**
  * **Refcount Coalescing:** Pokud kompilátor vidí, že objekt je v rámci jedné funkce opakovaně předáván a ukládán, sloučí více inkrementů a dekrementů do jediné operace na začátku a na konci.
  * **Deferred Reference Counting (DRC):** Lokální proměnné na zásobníku nebudou upravovat `refCount` objektu. Zápis na zásobník je bezpečný a úprava čítače proběhne pouze při uložení do dlouhodobých struktur (objekty na haldě, globální proměnné).

---

## 6. SIMD Auto-vectorization
Moderní x86-64 procesory mají k dispozici široké registry (AVX, AVX2, AVX-512) umožňující provádět matematické operace s více čísly najednou (Single Instruction, Multiple Data).

* **Optimalizace:** Pokud JIT detekuje nezávislé matematické operace nad poli (např. sčítání dvou polí `double`), nepoužije standardní instrukce FPU (`addsd`), ale vektorové instrukce (např. `vaddpd`).
* **Přínos:** Až **400% zrychlení** u algoritmů zpracovávajících masivní datové toky (grafika, signály, fyzikální simulace).
