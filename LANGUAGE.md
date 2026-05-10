# 📘 IRIS Language Specification

Tato dokumentace popisuje aktuální syntaxi a funkční možnosti jazyka **IRIS**. Jazyk je navržen pro automatizaci (RPA) a skriptování s moderní syntaxí inspirovanou jazyky Kotlin a Go.

---

## 1. Základní prvky

### Komentáře
Podporovány jsou jednořádkové komentáře.
```kotlin
// Toto je jednořádkový komentář
```

### Proměnné
IRIS rozlišuje mezi měnitelnými (`var`) a neměnnými (`val`) proměnnými. Podporuje také volitelné **typové anotace** pro runtime kontrolu.

```kotlin
val pi = 3.14              // Neměnná hodnota (konstanta)
var count = 0              // Měnitelná proměnná
var name: string = "Iris"  // Proměnná s typovou anotací
```

**Dostupné typy pro anotace:** `int`, `double`, `bool`, `string`.

---

## 2. Operátory

### Aritmetické
`+` (sčítání/spojování řetězců), `-` (odčítání), `*` (násobení), `/` (dělení), `%` (modulo).

### Porovnávací
`==` (rovnost), `!=` (nerovnost), `<` (menší než), `>` (větší než), `<=` (menší nebo rovno), `>=` (větší nebo rovno).

### Logické a Bitové
*   **Logické:** `&&` (AND), `||` (OR), `!` (NOT).
*   **Bitové:** `&` (AND), `|` (OR), `^` (XOR), `<<` (LShift), `>>` (RShift).

---

## 3. Řídící struktury

### Podmínky (If-Else)
```kotlin
if (x > 10) {
    print("Větší než 10")
} else if (x == 10) {
    print("Je to 10")
} else {
    print("Menší než 10")
}
```

### Cykly
IRIS nabízí tři typy cyklů pro maximální flexibilitu.

```kotlin
// While cyklus
while (i < 5) {
    i = i + 1
}

// Klasický For cyklus
for (var j = 0; j < 10; j = j + 1) {
    print(j)
}

// Jednoduchý Repeat cyklus
repeat (5) {
    print("Opakuji pětkrát")
}
```

V cyklech lze používat příkazy `break` pro okamžité ukončení a `continue` pro skok na další iteraci.

---

## 4. Funkce

Definují se klíčovým slovem `fun`. Podporují parametry s typovými anotacemi i návratové typy.

```kotlin
fun add(a: int, b: int): int {
    return a + b
}

val result = add(5, 10)
```

---

## 5. Vestavěné funkce

*   `print(expr)`: Vypíše hodnotu do konzole.
*   `wait(ms)`: Pozastaví vykonávání na určitý čas v milisekundách.

---

## 6. Třídy a Objekty (OOP)

IRIS podporuje objektově orientované programování inspirované Javou. Třídy mohou mít proměnné (fieldy) a metody s modifikátory přístupu.

### Definice třídy
```kotlin
class Person {
    public var name: string
    private var age: int

    // Konstruktor se jmenuje vždy 'init'
    fun init(name: string, age: int) {
        this.name = name
        this.age = age
    }

    public fun greet() {
        print("Ahoj, jmenuji se " + this.name)
    }

    private fun hidden() {
        print("Tato metoda není vidět zvenčí")
    }
}
```

### Vytvoření instance a přístup k členům
```kotlin
// Vytvoření objektu
val p = Person("Jan", 25)

// Přístup k public fieldům
print(p.name)

// Volání public metod
p.greet()

// p.age = 26 -> CHYBA (private field)
```

### Dědičnost (:)
IRIS podporuje jednoduchou dědičnost (ne vícenásobnou) pomocí dvojtečky. Potomek dědí všechny fieldy a metody rodiče.

```kotlin
class Zvire {
    public var jmeno: string

    fun init(jmeno: string) {
        this.jmeno = jmeno
    }

    public fun zvuk() {
        print("...")
    }
}

class Pes : Zvire {
    public var plemeno: string

    fun init(jmeno: string, plemeno: string) {
        super(jmeno)              // Volání konstruktoru rodiče
        this.plemeno = plemeno
    }

    // Override rodičovské metody
    public fun zvuk() {
        print("Haf!")
    }

    public fun info() {
        print(this.jmeno + " (" + this.plemeno + ")")
    }
}

val rex = Pes("Rex", "Ovčák")
rex.zvuk()   // "Haf!" (override)
rex.info()   // "Rex (Ovčák)"
print(rex.jmeno)  // "Rex" (zděděný field)
```

**`super(args)`** — Volá konstruktor (`init`) rodičovské třídy.
**`super.metoda()`** — Volá rodičovskou verzi přetížené metody.

---

## 7. Pole (Arrays)

IRIS podporuje vysoce výkonná pole. Pole mohou být vytvořena jako literály, nebo alokována s pevnou velikostí pro konkrétní typ.

### Literály
```kotlin
val colors = ["red", "green", "blue"]
val fib = [1, 1, 2, 3, 5]
```
---

## 8. Nové funkce (Moderní syntaxe)

### Kód funkční na jeden řádek (Kotlin Styl)
Pro krátké funkce můžete použít operátor `=` místo složených závorek a `return`:
```kotlin
fun double(x: int): int = x * 2
fun greet(name: string): string = "Ahoj, " + name
```

### String Interpolace
Můžete skládat text s proměnnými mnohem čistěji pomocí `${promenna}`:
```kotlin
val jmeno = "Karel"
val vek = 25
print("Uživatel ${jmeno} má ${vek} let.")
// Ekvivalentní k: print("Uživatel " + jmeno + " má " + vek + " let.")
```

### Zpracování výjimek (Try/Catch)
IRIS nyní poskytuje obranu proti chybám běhu programu (např. dělení nulou) přes bloky `try/catch`. Podporuje také ruční vyhození chyby (`throw`).
```kotlin
fun deleni(a: double, b: double): double {
    return a / b // vyhodí chybu pokud b = 0
}

try {
    val vysledek = deleni(10.0, 0.0)
    print("Výsledek je: " + vysledek)
} catch (e) {
    print("Zachycena chyba: " + e)
}

// Můžete si také vyhazovat vlastní chyby
try {
    throw "Moje vlastní aplikační chyba"
} catch (e) {
    print("Zachyceno: " + e)
}
```
### Alokace s pevnou velikostí (Fast Path)
Pro maximální výkon lze alokovat pole pro konkrétní typ. Takové pole je v paměti uloženo jako souvislý blok (raw memory), což je mnohem rychlejší než v Pythonu.
```kotlin
val marks = int[10]       // Pole 10 integerů (inicializované na 0)
val prices = double[5]    // Pole 5 desetinných čísel
val names = string[20]    // Pole 20 řetězců
```

### Přístup a Manipulace
```kotlin
val a = int[5]
a[0] = 100               // Nastavení hodnoty
val x = a[0]             // Čtení hodnoty

// Zjištění délky
val size = len(a)        // Pomocí vestavěné funkce
```

---

## 8. Typový systém (Shrnutí)

IRIS je dynamicky typovaný, ale dovoluje striktní typování pomocí anotací:
*   **Základní typy**: `int`, `double`, `bool`, `string`.
*   **Pole (Arrays)**: `int[]`, `double[]`, `bool[]`, `string[]`.
*   **Objekty**: Instance uživatelsky definovaných tříd.

Spojování pomocí `+` (včetně řetězců) automaticky převádí čísla na text, pokud je alespoň jeden operand řetězec.
