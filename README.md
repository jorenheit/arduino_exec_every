# exec-every (Arduino timing macros)

Header-only helpers for running code at regular intervals without timer interrupts.  
They are designed for use inside `loop()` when timer interrupts are unavailable, undesirable, or already in use.

The helpers avoid `delay()`, keep `loop()` responsive, and remove repetitive timestamp bookkeeping, while remaining fully **type-safe**.

## Overview

The library exposes **three macros**:

- `exec_every(interval, callback)` – run periodically, unconditionally  
- `exec_every_if(interval, condition, callback)` – run periodically, but only if a condition is true at the scheduled moment  
- `exec_throttled(interval, condition, callback)` – run at most once per interval, as soon as a condition becomes true

Although these are macros for convenience, **the implementation is fully type-safe**.  
Internally everything is based on templates and overload resolution:

- Callback signatures are checked at compile time
- Return types are deduced and enforced
- Conditions may be booleans or callables (optionally receiving `dt`)

Callbacks may be any callable objects, like function pointers, lambda's or other function-like objects with `operator()` overloaded.

---

## Installation

Include the header:

```cpp
#include "exec_every.h"   // whatever you named the header
```

---

## Key idea

Each macro call site maintains a private timer:

- It checks whether an `interval` (ms) has elapsed since the last run.
- It optionally checks a condition.
- If it runs, it calls your callback.
- The callback may optionally accept a `uint32_t dt` argument (elapsed time in ms since the previous run).
- A `Maybe<T>` object containing the value returned by the callback (if executed) is returned.

A callback can be any callable compatible with one of these signatures:

```cpp
void f();
void f(uint32_t dt);
T    f();
T    f(uint32_t dt);
```

Timers are independent per call site because the macros use `__COUNTER__` to generate a unique template tag per invocation.

---

## Return values and `Maybe<>`

All three macros return a `exec::Maybe<T>`:

- If the callback **did not run**, the returned `Maybe<T>` is **empty**. Validity of the object can be checked explicitly using its `valid()` member; it also converts implicitly to `bool` when used in a conditional expression.
- If the callback **did run**:
  - If the callback returns a value of type `T`, you get a `Maybe<T>` containing that value. This value can be retrieved by calling the `value()` member or by 'dereferencing' it using the `*` operator.
  - If the callback returns a reference, the `Maybe` object will return a reference to this same object and is therefore fully transparent.
  - If the callback returns `void`, you get a `Maybe<void>` that evaluates to `true` when it ran. This type cannot be dereferenced.

You can use it like this:

```cpp
auto result = exec_every(1000, []() -> int { return 42; });

if (result) {
  int v = *result;        // or result.value()
  // v == 42
}
```

For `void` callbacks:

```cpp
if (exec_every(1000, [] { /* ran */ })) {
  // This block executes only on iterations where the callback actually ran.
}
```

This allows follow-up logic to be expressed cleanly and without extra state variables.

---

## `exec_every(interval, callback)`

Runs the callback every `interval` milliseconds (as long as `loop()` keeps cycling).

**Typical use:** periodic housekeeping (blink LED, poll a sensor, print debug).

### Example: blink/toggle every 250 ms

```cpp
void loop() {
  exec_every(250, [] {
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  });

  // other loop work...
}
```

### Example: run every second and use `dt`

```cpp
void loop() {
  exec_every(1000, [] (uint32_t dt) {
    // dt will be ~1000 ms (plus loop jitter)
    // useful if you want to integrate/accumulate over real elapsed time
  });
}
```

---

## `exec_every_if(interval, condition, callback)`

Runs only when the interval expires *and* the condition is true **at that moment**.

Important detail: if the interval expires while the condition is false, **the timer is reset anyway** and the condition is checked again only after the next full interval.

**Typical use:** “sample only on schedule, but only if it’s allowed right then”.

### Example: try to log every 5 s, but only if serial is available

```cpp
void loop() {
  exec_every_if(5000, (bool)Serial, [] (uint32_t dt) {
    Serial.print("Logged. dt=");
    Serial.println(dt);
  });

  // other loop work...
}
```

If `Serial` is unavailable at the 5 s mark, that attempt is skipped and the next attempt will be 5 s later.

### Example: return a value only when it actually ran

```cpp
void loop() {
  auto m = exec_every_if(2000, analogRead(A0) > 512, []() -> int {
    // This is only called when interval expired AND condition is true
    return analogRead(A1);
  });

  if (m) {
    int sensor = *m;   // value from the callback
    // use sensor...
  }
}
```

---

## `exec_throttled(interval, condition, callback)`

Runs as soon as the interval has expired **and** the condition becomes true.

Important detail: if the interval has expired but the condition is false, **the timer keeps running** and the condition is checked on each subsequent pass through `loop()` until it becomes true (then it runs immediately).

**Typical use:** “run no more than once per interval, but don’t miss the first opportunity once ready”.

### Example: rate-limited condition
```cpp
bool linkIsUp();
bool linkStable(uint32_t dt) {
  static uint32_t stableTime = 0;
  if (linkIsUp()) stableTime += dt;
  else            stableTime = 0;
  return stableTime > 3000;   // link up for at least 3 seconds
}

void loop() {
  exec_throttled(500, linkStable, flushTelemetry());
}
```

Here:

- The interval limits how often flushing may occur
- The condition ensures the link has been stable long enough
- The callback runs immediately once both are satisfied

### Example: use the return value to confirm a one-shot action happened

```cpp
void loop() {
  auto didSend = exec_throttled(1000, canSendNow(), []() -> bool {
    return sendPacket();   // true on success
  });

  if (didSend) {
    bool ok = *didSend;    // result from sendPacket()
    // react to ok...
  }
}
```

---

## Notes / gotchas

- Uses `millis()` internally; wraparound is handled naturally by unsigned subtraction (`now - last`).
- State is stored in a `static uint32_t last` per macro call site (unique via `__COUNTER__`).
- Because that state is `static`, timing works as intended only if the call site is reached repeatedly (e.g. from `loop()`).
- `interval` is milliseconds; `dt` is the elapsed time since the previous run of that call site (includes loop jitter).
