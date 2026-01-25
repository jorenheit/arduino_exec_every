# exec-every (Arduino task scheduling framework)

This is a header-only library for scheduling callbacks at regular interval without using interrupts. The library is non-blocking, light-weight, low overhead and fully type-safe. The provided functionality is designed for use inside frequently called functions like `loop()` when timer interrupts are unavailable, undesirable, or already in use. Under most circumstances, no object management is required from the user; just call the scheduling function and you're done.

## Overview

The library exposes these three main functions (macro's):

- `exec_every(interval, callback)` – run periodically, unconditionally  
- `exec_every_if(interval, condition, callback)` – run periodically, but only if a condition is true at the scheduled moment  
- `exec_throttled(interval, condition, callback)` – run at most once per interval, as soon as a condition becomes true

Additionally, there are the `*_with` versions, that allow you to use a custom replacement for the default `millis()` function. This must be function with signature `uint32_t()`:
- `exec_every_with(millisFn, interval, callback)` 
- `exec_every_if_with(millisFn, interval, condition, callback)` 
- `exec_throttled_with(millisFn, interval, condition, callback)` 

## Quick Usage Example
Simply call, for example, `exec_every` in the main loop, providing the interval in milliseconds and a callback (commonly a lambda, but could be anything that can be called). The code below will print "Ping!" to the serial device once every second, starting after one second:

```cpp
void loop() {
  exec_every(1000, [](){ Serial.println("Ping!"); });
}
```
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

A condition can be a `bool`, a type convertible to `bool` or a callable that returns `bool`, optionally accepting a `uint32_t`:
```cpp
bool c();
bool c(uint32_t dt);
```

## Return value: `Maybe<>`

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

## Notes / gotchas

- Uses `millis()` internally; wraparound is handled naturally by unsigned subtraction (`now - last`).
- State is stored in a `static uint32_t last` per macro call site (unique via `__COUNTER__`).
- Because that state is `static`, timing works as intended only if the call site is reached repeatedly (e.g. from `loop()`).
- `interval` is milliseconds; `dt` is the elapsed time since the previous run of that call site (includes loop jitter).
- Different values for `interval` may be passed in different iterations for dynamic timing.- 

---

## Advanced Use

### Handles

Every call to `exec_every`, `exec_every_if`, or `exec_throttled` creates a *persistent call site* with its own internal timer.
This call site is represented internally by a **handle**.

You normally do not need to interact with handles directly.  
However, they can be useful when you want to *manually control* a scheduled callback.

You can obtain the handle associated with a `Maybe<T>` like this:

```cpp
auto result = exec_every(1000, readTemperature);
exec::Handle h = exec::getHandle(result);
```

The handle uniquely identifies the internal timer and callback associated with that call site.

---

### Resetting a call site

A handle can be reset explicitly:

```cpp
exec::reset(h);
```

This resets the internal timer as if the callback had just run.
The next execution will only occur after a full interval has elapsed again.

This is useful when:
- external events invalidate accumulated timing
- configuration changes require restarting a schedule
- you want to realign periodic behavior

---

### Forcing execution with `Maybe::force()`

Sometimes you want to run the callback **outside the scheduler**, for example:
- to obtain an initial value immediately
- to re-evaluate logic on demand
- to reuse the same callback logic without duplicating code

Every `Maybe<T>` returned by the `exec_every` family provides a `force()` member.
Calling `force()` executes the callback immediately and **returns its value directly**.
Afterwards, the `Maybe` is guaranteed to be valid, containing that same value.

```cpp
auto m = exec_every(1000, readTemperature);

// Run immediately and get the value
int temperature = m.force();
```

For `void` callbacks:

```cpp
auto m = exec_every(1000, blinkLed);

// Run immediately
m.force();
```

Important notes:
- `force()` does **not** reset the timer unless the callback itself does so.
- Subsequent scheduled executions continue normally.
- `force()` preserves reference return types transparently.

---

### Using `force()` to run the callback immediately 
The `exec_every` family will schedule the first execution of the callback after the specified interval has expired. If you need to run the callback immediately, you have to do this explicitly using some additional logic that makes use of the `force()` member. For example:

```cpp
  void logTemp(int);
  int readTemp();

  // schedule a temp-logger
  auto m = exec_every(1000, []() {
    logTemp(millis(), readTemp());
  });

  // log at immediately
  static bool firstTime = true;
  if (firstTime) {
    m.force(); 
    firstTime = false;
  }
```
---

### Dynamic Callbacks
The callback function should not change across iterations. On the first call to `exec_every`, the callback is stored in the handle and is not updated on subsequent calls. For dynamic callbacks, simply provide a wrapper that handles the dynamic dispatch:

```cpp
int a();
int b();
static int (*current)() = a;

int dispatch() { 
  return current();
}

void loop() {
  current = someCondition ? a : b;
  exec_every(1000, dispatch);
}
```

### When to use Advanced features

Most users will never need these tools.

Use handles and `force()` only when:
- you need deterministic control over timing
- you want to reuse scheduled callbacks manually
- external events must override the scheduler

For normal periodic logic, the basic macros are sufficient.
