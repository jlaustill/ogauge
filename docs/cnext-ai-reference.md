# C-Next AI Reference

A complete reference for AI code generation. C-Next transpiles to C/C++. Every rule here maps to transpiler behavior — if you violate a rule, the transpiler will reject it.

## Operators

```
ASSIGNMENT:    x <- 5          →  x = 5
COMPARISON:    if (x = 5)      →  if (x == 5)
NOT EQUAL:     if (x != 5)     →  if (x != 5)
COMPOUND:      x +<- 1         →  x += 1
               x -<- 1         →  x -= 1
               x *<- 2         →  x *= 2
               x &<- mask      →  x &= mask
               x |<- flags     →  x |= flags
               x <<<- 1        →  x <<= 1
ARITHMETIC:    + - * / %       (same as C)
BITWISE:       & | ^ ~ << >>   (same as C)
LOGICAL:       && || !          (same as C)
```

## Types

```
UNSIGNED:  u8  u16  u32  u64   → uint8_t  uint16_t  uint32_t  uint64_t
SIGNED:    i8  i16  i32  i64   → int8_t   int16_t   int32_t   int64_t
FLOAT:     f32  f64            → float    double
BOOL:      bool                → bool
SIZE:      usize               → size_t
STRING:    string<N>           → char[N+1]
```

## Variable Declarations

```cnx
u32 x <- 42;                    // initialized
u32 y;                           // zero-initialized (NOT garbage)
const u32 MAX <- 100;            // compile-time constant
clamp u8 brightness <- 200;      // saturating arithmetic (DEFAULT behavior)
wrap u32 counter <- 0;           // wrapping arithmetic (opt-in)
atomic u32 shared <- 0;          // ISR-safe
```

All variables are zero-initialized. No uninitialized variables exist.

## Overflow Behavior

- **Default is clamp** (saturating): `u8 x <- 200; x +<- 100;` → x = 255
- **opt-in wrap**: `wrap u32 c <- 0; c -<- 1;` → c = UINT32_MAX
- Combine with atomic: `atomic clamp u8 brightness <- 0;`

## Arrays

```cnx
u8[256] buffer;                  // fixed-size, zero-initialized
u8[5] data <- [1, 2, 3, 4, 5];  // literal init with []
u8[100] zeros <- [0*];           // fill all elements
u8[4][8] matrix;                 // multi-dimensional

usize len <- buffer.element_count;  // 256 (compile-time)
u32 bits <- buffer.bit_length;      // 2048 (compile-time)
u32 bytes <- buffer.byte_length;    // 256 (compile-time)
```

**Rules:**
- Use `[]` for array literals, NOT `{}`
- Partial initialization forbidden (MISRA 9.3) — provide all elements or use `[val*]`
- Size goes BEFORE the name: `u8[256] buffer` not `u8 buffer[256]`

## Strings

```cnx
string<64> name <- "Hello";         // 64-char max capacity
u32 len <- name.char_count;         // runtime: strlen → 5
u32 cap <- name.capacity;           // compile-time: 64
u32 sz <- name.size;                // compile-time: 65 (capacity + null terminator)
string<5> sub <- name[0, 5];        // substring
string<96> joined <- name + " World"; // concat (capacity >= sum of operand capacities)
const string VERSION <- "1.0.0";    // auto-sized
```

## Structs

```cnx
struct Point {
    i32 x;
    i32 y;
}

Point origin;                        // zero-initialized
Point p <- { x: 10, y: 20 };        // named field init (MUST use names)
p.x <- 100;                         // member access

// Nested
struct Rect {
    Point topLeft;
    Point bottomRight;
}

Rect r <- {
    topLeft: { x: 0, y: 0 },
    bottomRight: { x: 100, y: 50 }
};
```

**Rules:**
- Named field initialization only: `{ x: 10, y: 20 }` — NOT positional `{ 10, 20 }`
- All fields public (structs are data containers)
- Zero-initialized by default

## Enums

```cnx
enum State {
    IDLE,                // 0
    RUNNING,             // 1
    ERROR <- 255         // explicit value
}

State s <- State.IDLE;
if (s = State.RUNNING) { }

switch (s) {
    case State.IDLE { start(); }
    case State.RUNNING { check(); }
    default { handleError(); }
}
```

## Bitmaps

```cnx
bitmap8 Flags {          // MUST be bitmap8, bitmap16, bitmap24, or bitmap32
    Running,             // bit 0 (1 bit)
    Direction,           // bit 1
    Fault,               // bit 2
    Mode[3],             // bits 3-5 (3 bits)
    Reserved[2]          // bits 6-7 (2 bits)
}
// Total bits MUST equal the bitmap size (8 here)

Flags f <- 0;
f.Running <- true;
f.Mode <- 5;
bool r <- f.Running;
```

## Bit Manipulation

```cnx
u8 flags <- 0;
flags[3] <- true;              // set bit 3
bool b <- flags[3];            // read bit 3
flags[4, 3] <- 5;             // set 3 bits starting at bit 4
u8 field <- flags[4, 3];      // read 3-bit field

// Use for narrowing (casts are FORBIDDEN)
u32 big <- 0xDEADBEEF;
u8 low <- big[0, 8];          // bits 0-7 → 0xEF
u8 high <- big[24, 8];        // bits 24-31 → 0xDE
```

## Type Casting Rules

```
WIDENING (implicit, safe):     u8 → u32, i8 → i32
NARROWING (FORBIDDEN):         u32 → u8   — use bit indexing: val[0, 8]
SIGN CHANGE (FORBIDDEN):       i32 → u32  — use bit indexing: val[0, 32]
FLOAT TRUNCATION (allowed):    (u32)floatVal — truncates fractional part
POINTER CAST (NOT SUPPORTED):  use register keyword for MMIO
```

## Functions

```cnx
void doNothing() { }

u32 add(u32 a, u32 b) {
    return a + b;
}
```

**Rules:**
- Define before use (no forward declarations)
- No recursion (MISRA 17.2)
- No `break`/`continue` — use structured conditions

## Pass-by-Reference (ADR-006)

**ALL parameters are pass-by-reference automatically.** No pointer syntax exists.

```cnx
void increment(u32 x) {     // transpiles to: void increment(uint32_t *x)
    x +<- 1;                // transpiles to: *x += 1;
}

increment(myVar);            // transpiles to: increment(&myVar);
```

**Consequence:** Cannot pass literals to functions (no addressable location).
```cnx
const u32 LED_PIN <- 13;
init(LED_PIN);                // OK: LED_PIN has an address
// init(13);                  // ERROR: literal has no address
```

## Control Flow

```cnx
// If/else (braces required)
if (x > 0) {
    doA();
} else if (x < 0) {
    doB();
} else {
    doC();
}

// While
while (running) { process(); }

// For
for (u32 i <- 0; i < 10; i +<- 1) { buffer[i] <- 0; }

// Do-while (condition MUST be boolean comparison)
do {
    byte <- read();
} while (byte != END_MARKER);
// } while (byte);           // ERROR: bare bool not allowed

// Ternary (condition MUST be comparison or logical op)
u32 max <- (a > b) ? a : b;
// u32 y <- flag ? 1 : 0;   // ERROR: bare bool
// Nested ternary FORBIDDEN

// Switch (braces, no colons, no fallthrough, no break needed)
switch (state) {
    case State.IDLE { start(); }
    case State.RUNNING { check(); }
    default { error(); }
}

// Multiple cases
switch (cmd) {
    case Cmd.READ || Cmd.PEEK { readData(); }
    case Cmd.WRITE { writeData(); }
}
```

## Scopes

Scopes are singleton modules with automatic name prefixing.

```cnx
scope Counter {
    u32 value <- 0;                 // private (default)

    public void increment() {       // public — accessible outside
        this.value +<- 1;
    }

    public u32 get() {
        return this.value;
    }
}

// External usage
Counter.increment();                // → Counter_increment()
u32 v <- Counter.get();             // → Counter_get()
```

### Name Resolution (ADR-057)

Priority: **local → scope → global**

```cnx
scope Foo {
    u32 x <- 10;

    void bar() {
        u32 x <- 5;        // local shadows scope
        // x = 5            (local)
        // this.x = 10      (scope, explicit)
        // global.x          (global, explicit)
    }
}
```

**Rules:**
- Inside a scope, bare names resolve local first, then scope, then global
- `this.name` forces scope resolution
- `global.name` forces global resolution
- `global.ScopeName.function()` calls another scope's public function

### Scope Transpilation

```cnx
scope LED {
    u32 pin <- 13;                    // → static uint32_t LED_pin = 13;
    public void on() { }             // → void LED_on(void) { }
    void helper() { }                // → static void LED_helper(void) { }
}
```

- Private members → `static` (file-scoped)
- Public members → non-static + header prototype
- Names prefixed: `ScopeName_memberName`

## Includes

```cnx
#include "local_file.cnx"           // transpiles to: #include "local_file.h"
#include <Arduino.h>                 // C/C++ header (passed through)
#include <lvgl.h>                    // external library header
```

C-Next `.cnx` includes transpile to `.h` includes. C/C++ headers pass through unchanged.

## Preprocessor

```cnx
#define ARDUINO                      // flags OK for conditional compilation
#define DEBUG

#ifdef ARDUINO
// platform-specific code
#endif

// #define MAX_SIZE 100              // FORBIDDEN: use const instead
const u32 MAX_SIZE <- 100;          // correct
```

`#define` with values is forbidden. Use `const` for values, `#define` only for flags.

## Constants

```cnx
const u32 BUFFER_SIZE <- 256;
const f32 PI <- 3.14159;
const string VERSION <- "1.0.0";     // auto-sized string
```

Constants are compile-time values with fixed addresses (can be passed to functions).

## Callbacks (ADR-029: Function-as-Type)

A function definition creates both a callable function AND a type. Callbacks are never null.

```cnx
// Define callback type + default implementation
void onReceive(const CAN_Message msg) {
    // no-op default
}

// Use as struct field type
struct Controller {
    onReceive handler;               // initialized to onReceive (never null)
}

// User provides implementation
void myHandler(const CAN_Message msg) {
    Serial.println(msg.id);
}

controller.handler <- myHandler;     // OK: signatures match
controller.handler(msg);             // always safe to call
```

**Nominal typing:** Type identity is the function NAME, not just signature. Two callbacks with identical signatures are different types.

### Scope Function as Callback

```cnx
scope LvglPort {
    void disp_flush(lv_display_t disp, const lv_area_t area, u8 px_map) {
        // ...
    }

    public void init() {
        lv_display_t disp <- lv_display_create(480, 480);
        lv_display_set_flush_cb(disp, this.disp_flush);
    }
}
```

`this.functionName` passes the scope function as a callback.

## Nullable C Interop (ADR-046)

C-Next types are never null. C library return values that CAN be null require `c_` prefix.

```cnx
#include <stdio.h>

// Nullable C return → requires c_ prefix
FILE c_file <- fopen("data.txt", "r");
if (c_file != NULL) {
    // use c_file
    fclose(c_file);
}

// Non-nullable C-Next variable → NO c_ prefix
string<64> buffer;                   // always valid
u32 count <- 0;                      // always valid
```

**Rules:**
- `c_` prefix REQUIRED for variables holding nullable C pointer returns
- `c_` prefix FORBIDDEN on non-nullable types (error E0906)
- NULL comparison only allowed on `c_`-prefixed variables
- `malloc`/`calloc`/`realloc`/`free` FORBIDDEN (ADR-003)

### When c_ IS Needed

| Returns pointer that can be NULL | Example |
|---|---|
| `fopen()` | `FILE c_file <- fopen(...)` |
| `lv_display_create()` | `lv_display_t c_disp <- lv_display_create(...)` |
| `lv_label_create()` | `lv_obj_t c_label <- lv_label_create(...)` |

### When c_ is NOT Needed

| Returns non-pointer or C-Next type | Example |
|---|---|
| Primitive return values | `u32 len <- strlen(s)` |
| C-Next variables | `u32 count <- 0` |
| Scope members | `this.value` |

## C Library Interop

### Calling C Functions

C functions from included headers are called with `global.` prefix (or bare if unambiguous):

```cnx
#include <Arduino.h>

void setup() {
    Serial.begin(115200);            // implicit global resolution
    global.pinMode(LED_PIN, OUTPUT); // explicit global
    delay(100);                      // implicit global
}
```

### Using C Types

C types from headers (structs, typedefs, enums) are used directly:

```cnx
#include <driver/spi_master.h>

spi_bus_config_t cfg <- {
    mosi_io_num: 1,
    miso_io_num: -1,
    sclk_io_num: 2,
    quadwp_io_num: -1,
    quadhd_io_num: -1,
    max_transfer_sz: 64
};
```

### Opaque/Handle Types

C libraries often use opaque pointer types (e.g., `esp_lcd_panel_handle_t` = `void*`). These work as scope variables and function parameters. The transpiler manages the pointer nature.

```cnx
scope Display {
    esp_lcd_panel_handle_t panel_handle;    // stored as pointer internally

    public void init() {
        esp_lcd_new_rgb_panel(rgb_config, this.panel_handle);
        esp_lcd_panel_init(this.panel_handle);
    }
}
```

### Anonymous Struct Flags Workaround

C structs with anonymous nested structs (common in ESP-IDF) can't use compound literal initialization for the nested part in C++. Set flags separately:

```cnx
// Can't init flags inline due to C++ anonymous struct limitation
esp_lcd_rgb_panel_config_t cfg <- { /* other fields */ };
cfg.flags.fb_in_psram <- true;       // set flag after init
```

## Atomic Types (ADR-049)

```cnx
atomic u32 counter <- 0;
atomic clamp u8 brightness <- 0;
atomic bool ready <- false;

counter +<- 1;                       // lock-free on Cortex-M3+ (LDREX/STREX)
brightness +<- 10;                   // atomic add with clamp
ready <- true;                       // atomic store
u32 val <- counter;                  // atomic load
```

Transpiles to LDREX/STREX loops on Cortex-M3+, critical sections on Cortex-M0.

## Critical Sections (ADR-050)

```cnx
critical {
    buffer[writeIdx] <- data;
    writeIdx +<- 1;
}
// No return/break/continue inside critical blocks
```

## Register Bindings (ADR-004)

```cnx
register GPIO7 @ 0x42004000 {
    DR:         u32 rw @ 0x00,
    GDIR:       u32 rw @ 0x04,
    PSR:        u32 ro @ 0x08,
    DR_SET:     u32 wo @ 0x84,
    DR_CLEAR:   u32 wo @ 0x88,
}

GPIO7.DR <- 0xFF;
GPIO7.DR_SET[3] <- true;            // atomic set bit 3
bool bit <- GPIO7.PSR[3];           // read bit 3
```

Access modes: `rw` (read-write), `ro` (read-only), `wo` (write-only), `w1c` (write-1-to-clear), `w1s` (write-1-to-set).

## MISRA Compliance

C-Next enforces several MISRA C:2012 rules at the language level:

| Rule | Enforcement |
|------|-------------|
| 10.1 | No signed shift operands; hex masks use unsigned literals |
| 12.2 | Shift amount must be < type bit width |
| 13.5 | No function calls in `if`/`while` conditions |
| 9.3 | No partial array initialization |
| 17.2 | No recursion |
| 10.3 | No implicit narrowing conversions |
| 11.1 | No function pointer type conversions |

### MISRA 13.5 Pattern

```cnx
// WRONG: function call in condition
if (config.enabled && manager.isReady()) { }

// RIGHT: extract to variable
bool ready <- manager.isReady();
if (config.enabled && ready) { }
```

### MISRA 12.2 Pattern

```cnx
// WRONG: shifting u8 by 8 (equals type width)
u8 val <- 1;
u16 result <- val << 8;             // ERROR

// RIGHT: widen first
u16 wide <- val;
u16 result <- wide << 8;            // OK: 8 < 16
```

## Common AI Mistakes

### 1. Wrong assignment/comparison operators
```cnx
// WRONG
x = 5;                               // This is COMPARISON, not assignment
if (x == 5) { }                      // == doesn't exist

// RIGHT
x <- 5;                              // assignment
if (x = 5) { }                       // comparison
```

### 2. C-style array init
```cnx
// WRONG
u8[3] data <- {1, 2, 3};            // {} is for structs

// RIGHT
u8[3] data <- [1, 2, 3];            // [] for arrays
```

### 3. Using pointers
```cnx
// WRONG — no pointer syntax exists
u32* ptr <- &value;
ptr->field <- 5;

// RIGHT — everything is pass-by-reference automatically
void modify(u32 x) { x <- 5; }      // modifies original
```

### 4. Using malloc/dynamic allocation
```cnx
// WRONG — forbidden (ADR-003)
void c_buf <- heap_caps_malloc(size, MALLOC_CAP_SPIRAM);

// RIGHT — static allocation
u8[46080] buf;
```

### 5. Missing c_ prefix on nullable returns
```cnx
// WRONG — E0906 if type IS nullable, E0905 if missing prefix
lv_display_t disp <- lv_display_create(480, 480);

// RIGHT (if the C function returns a pointer that can be NULL)
lv_display_t c_disp <- lv_display_create(480, 480);
```

### 6. Bare bool in conditions
```cnx
// WRONG
if (flag) { }
do { } while (running);
u32 x <- flag ? 1 : 0;

// RIGHT
if (flag = true) { }
do { } while (running = true);
u32 x <- (flag = true) ? 1 : 0;
```

### 7. Function call in if condition
```cnx
// WRONG — MISRA 13.5
if (sensor.read() > threshold) { }

// RIGHT
u32 val <- sensor.read();
if (val > threshold) { }
```

### 8. Narrowing cast
```cnx
// WRONG
u8 byte <- (u8)bigValue;

// RIGHT
u8 byte <- bigValue[0, 8];
```

### 9. Increment/decrement operators
```cnx
// WRONG — no ++ or -- in C-Next
x++;
--y;

// RIGHT
x +<- 1;
y -<- 1;
```

### 10. Break/continue
```cnx
// WRONG — break/continue don't exist
while (true) {
    if (done) break;
}

// RIGHT — structured conditions
while (!done) {
    process();
}
```

### 11. Type aliases
```cnx
// WRONG — type aliases don't exist (ADR-019 rejected)
type Byte <- u8;

// RIGHT — use the type directly
u8 value <- 0;
```

### 12. Bitmap with wrong size
```cnx
// WRONG — bits don't sum to 8
bitmap8 Bad {
    A,          // 1
    B,          // 1
    C[3]        // 3  → total 5, not 8!
}

// RIGHT — must sum exactly
bitmap8 Good {
    A,          // 1
    B,          // 1
    C[3],       // 3
    Reserved[3] // 3  → total 8
}
```

## Known Transpiler Bugs

No known bugs as of v0.2.7.

## Transpilation Summary

| C-Next | Generated C |
|--------|-------------|
| `x <- 5` | `x = 5` |
| `if (x = 5)` | `if (x == 5)` |
| `x +<- 1` | `x += 1` |
| `void f(u32 x)` | `void f(uint32_t *x)` |
| `f(myVar)` | `f(&myVar)` |
| `scope S { void f() {} }` | `static void S_f(void) {}` |
| `scope S { public void f() {} }` | `void S_f(void) {}` + header |
| `S.f()` | `S_f()` |
| `u8[5] a <- [1,2,3,4,5]` | `uint8_t a[5] = {1,2,3,4,5}` |
| `string<64> s <- "hi"` | `char s[65] = "hi"` |
| `s.char_count` | `strlen(s)` |
| `s.capacity` | `64` |
| `flags[3] <- true` | bit-set expression |
| `val[0, 8]` | `(val >> 0) & 0xFF` |
| `atomic u32 x` | `volatile uint32_t x` + LDREX/STREX |
| `critical { ... }` | PRIMASK save/disable/restore |
| `#include "x.cnx"` | `#include "x.h"` |
