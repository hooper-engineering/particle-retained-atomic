# Particle Retained Atomic Library

The Particle Retained Atomic library provides a *transactional* and *atomic* interface to retained RAM that reduces the chance of inconsistent program state due to partial writes or crashes during state updates.

*Author is not affiliated with Particle&trade; or Particle.io. It is merely compatible with Particle devices.*

## License

MIT License: allows commercial use but **requires attribution**.

The MIT license notice and copyright information must be included with source, compiled code, or finished goods containing this library.

<details><summary>MIT License notice</summary>

```
Copyright 2019 Hooper Engineering, LLC

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
```
</details>

## Motivation

### Problem

Sometimes it is necessary to store application state that will persist across multiple executions of your program. This carries the risk that that the application may be reset, crash, or be corrupted while in the process of updating multiple variables comprising the system state.

When the application restarts, the inconsistent state variables may cause unanticipated program states or unexpected execution paths resulting in difficult to reproduce bugs.

For example,

```cpp
retained time_t eventTimestamp;
retained float  eventMeasurement;
float myMeasurement;

void eventFunction() {
	eventTimestamp = Time.now();
	myMeasurement = readSensor();
	eventMeasurement = myMeasurement;
}

```

could crash during `readSensor()`, leaving `eventTimestamp` written but `eventMeasurement` at the previous value.

The risk can be reduced by storing these values closer together in code, but the risk cannot be eliminated this way.

### Solution

`ParticleRetainedAtomic` allows you to maintain state in a way that is *transactional* and *atomic*.

That is, you can write new values to the retained memory, but it is not *committed* until you explicitly say so. In this way, you can update complex state variables in chunks without worrying about getting into an inconsistent state.

If the application resets before a commit occurs, the new state is simply thrown away, starting at the previously committed state.

## Usage

### Declaration

First, figure out what globals need to be carried from one execution to another and define a `typedef struct` that contains all of the data you wish to store, and declare its init value:

```cpp
#include "ParticleRetainedAtomic.h"

typedef struct {
  float lastReportTemperatureC;   // last reported tempererature Celcius
  float lastReportBaroKpa;        // last reported barometric pressure (kPa)
  time_t lastReportTime;          // last reported time (Unix time)
  uint32_t reconnectCount;        // number of reconnection attempts
  bool hasGoodReading;            // a good measurement has been taken
  
} retainedData_t;

// This is the value that will be set when the library first runs or is
// unable to restore the state due to a problem
const retainedData_t PRAInitVals = {-1000, -1000, 0, 0, false};
```

Next declare **two** save areas of your custom struct type as `retained` and a `ParticleRetainedAtomicData_t` which maintains state for the `ParticleRetainedAtomic` object:

```cpp
retained retainedData_t saveArea1, saveArea2;   // save pages
retained ParticleRetainedAtomicData_t PRAData;  // checksums
```
Finally, declare the `ParticleRetainedAtomic` object:

```

ParticleRetainedAtomic<retainedData_t>
    gAppState(saveArea1,
              saveArea2,
              PRAData,
              PRAInitVals);
```

Since this is intended to hold application state, it often makes the most sense to declare all of the above in the global scope. It's possible to split state among different sections of code and declare different `ParticleRetainedAtomic` objects, but ensure that you declare all three `retained` objects separately for each new usage.

While it is possible to declare `ParticleRetainedAtomic<T>` objects in the function scope, this usually would not make any sense. It would need to be re-constructed each time the function goes out of and back into scope, which is wasteful.

Note that the passed in structs *must* be global scope: the `retained` keyword *only* works there.

### Operation

`ParticleRetainedAtomic<T>` makes use of the `->` operator to give you access to the underlying retained structures that were passed in at the declaration:

```cpp
gAppState->lastReportTemperatureC = getTemp();
gAppState->lastReportBaroKpa = getPres();
gAppState->lastReportTime = Time.now();
gAppState->hasGoodReading = true;
```

You do not need to know which retained area the library is currently working with &mdash; it will automatically direct the new data to the correct persistent data area.

Now that the complete state has been updated, you can call `.save()` and it will be committed:

```cpp
gAppState.save();
```

When the application restarts, these values will be transparently restored into `gAppState` for use.

## Example

```cpp
#include "ParticleRetainedAtomic.h"

// Persistent state:
typedef struct {
  float lastReportTemperatureC;   // last reported tempererature Celcius
  float lastReportBaroKpa;        // last reported barometric pressure (kPa)
  time_t lastReportTime;          // last reported time (Unix time)
  uint32_t reconnectCount;        // number of reconnection attempts
  bool hasGoodReading;            // a good measurement has been taken
  
} retainedData_t;

const retainedData_t PRAInitVals = {-1000, -1000, 0, 0, false};
retained retainedData_t saveArea1, saveArea2;   // save pages
retained ParticleRetainedAtomicData_t PRAData;  // checksums

ParticleRetainedAtomic<retainedData_t> gAppState(saveArea1,
                                                 saveArea2,
                                                 PRAData,
                                                 PRAInitVals);

void setup() {
  // gAppState already contains either init values or the last saved values
  // You can access these stored values like so:
  time_t lastEventTime = gAppState->lastReportTime;
  // hasGoodReading initializes to false, so we know there is a good reading!
  if (gAppState->hasGoodReading == true) {
    printLastGoodValue(lastEventTime,
                       gAppState->lastReportTemperatureC,
                       gAppState->lastReportBaroKpa);
  }
}

// Update saved state
// Note that if a crash happens in this function,
// the saved state will revert to the last successful save on reboot
void myEvent() {
    // Write new values like so:
    gAppState->lastReportTemperatureC = getTemp();
    gAppState->lastReportBaroKpa = getPres();
    gAppState->lastReportTime = Time.now();
    gAppState->hasGoodReading = true;
    // then commit the changes all at once:
    gAppState.save();
}
```

## Other notes

The library overrides the `->` operator to give you access to the struct type it is templated as. Further, it directs you to the proper location in retained memory at all times, which changes with every `.save()`. The `->` operator dereferences a typed pointer to the proper save structure, meaning that the compiler should always check types against it correctly. Even though it looks funny, the compiler understands what is going on here without magic.

This unusual convention is intended to reduce redundant code and make it more understandable, but there are a couple of things you should not do: One is access the `retained` structures directly. This memory should be passed to the `ParticleRetainedAtomic` constructor and left alone. The other thing is keeping a pointer to one of the structure items, i.e.

```cpp
time_t* pointer_to_the_unknown = &(gAppState->lastReportTime);
```

This will result in having a pointer to the desired location half the time and to something else the other half. If you write to that location directly at the wrong time, it will invalidate your entire committed state. Don't Do It.

That being said, the `->` usage shown in the examples is consistent and complete, so there shouldn't be a good reason to try anything else.

## Todo

(in no particular order)

- Better checksum/hashing algorithm
- Ability to 'pickle' state into EEPROM/flash
- Additional testing needed, especially for edge cases
- Create a callback option for initializing the struct
- Create a callable `.revert()` function
