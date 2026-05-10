# 🗺️ IRIS Language Roadmap (TODO)

Tento dokument slouží jako plán rozvoje jazyka IRIS. Cílem je vytvořit moderní, rychlý a bezpečný jazyk vhodný pro automatizaci a skriptování.

---

## ✅ Fáze 1: Základy (Dokončeno)
- [x] Návrh registrového VM (Register-based VM).
- [x] Základní datové typy (`int`, `double`, `bool`, `string`, `null`).
- [x] Operátory (aritmetické, logické, bitové).
- [x] Řídící struktury (`if`, `while`, `for`, `repeat`).
- [x] Funkce (parametry, návratové hodnoty, overloading).
- [x] Portabilní dispatch (Switch-based dispatch v `VM.cpp`).
- [x] String Interpolation (Např.: `"Ahoj ${name}"`).
- [x] RPA bloky (`mouse`, `keyboard`).

---

## 🏗️ Fáze 2: Objektově orientované programování (Právě probíhá)
- [ ] **Třídy (Classes)**:
    - [x] Deklarace tříd, fieldů a metod.
    - [x] Klíčové slovo `this`.
    - [x] Modifikátory přístupu (`public`, `private`).
    - [x] Konstruktor přes `fun init()`.
    - [x] Plná podpora abstrakce (`abstract class`, `abstract fun`).
- [ ] **Kolekce**:
    - [ ] Plná implementace polí (`array`).
    - [x] Implementace map/slovníků (`Map<K, V>`).

---

## 📦 Fáze 3: Standardní knihovna & Systém
- [ ] **Souborový systém**: Čtení a zápis souborů (`File.read`, `File.write`).
- [ ] **Network**: Jednoduchý HTTP klient pro API volání.
- [ ] **JSON**: Vestavěný parser pro práci s daty.
- [ ] **Argumenty příkazové řádky**: Přístup k parametrům při spuštění skriptu.
- [x] **Datum a čas**: Knihovna pro práci s časem a měření trvání.

---

- [x] **Výjimky (Exceptions)**: Implementace `try { ... } catch (e) { ... }`.
- [ ] **Zlepšení chybových hlášení**: Přesné určení řádku a sloupce v parseru i runtime.
- [ ] **Static Analysis**: Kontrola typů a definic proměnných už v čase kompilace (před spuštěním).

---

## ⚡ Fáze 5: Výkon & Pokročilé funkce
- [ ] **Garbage Collector / Reference Counting Optimalizace**: Bezpečnější uvolňování cyklů (nebo implementce Tracing GC).
- [ ] **Concurrency**: Podpora pro asynchronní operace nebo vlákna.
- [ ] **Optimalizace bytecodu**: Odstraňování mrtvého kódu a vylepšení alokace registrů.
- [x] **Native Interop**: Možnost volat C++ funkce přímo z IRIS skriptu.
- [ ] **Memory Optimization**: Úvaha o implementaci *NaN-Boxingu* pro snížení velikosti proměnných ze 16 B na 8 B.

---

## 🛠️ Fáze 6: Ekosystém & Nástroje
- [ ] **Module System**: Možnost importovat jiné `.iris` soubory (`import "utils"`).
- [ ] **LSP (Language Server)**: Podpora pro našeptávání a zvýraznění syntaxe v editorech (VS Code).
- [ ] **iris-fmt**: Automatický formátovač kódu.
- [ ] **iris-pkg**: Jednoduchý správce balíčků pro sdílení knihoven.

---

## 🚀 Dlouhodobé cíle
- [ ] Překonání výkonu Pythonu ve všech standardních benchmarcích.
- [ ] Vlastní debugger s vizualizací VM stacku.
- [ ] Portabilita na Linux a macOS (zatím se zaměřujeme na Windows kvůli RPA).
