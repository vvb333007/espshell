# Persistent Pointers

In **ESPShell**, certain data structures — once allocated — are **never truly freed**.  
Instead, deallocated entries are moved to an **“unused entries”** list, where they remain allocated but marked as reusable.

This design ensures that **pointers to these structures never become invalid**, even after the object is “freed.”

---

## Why Persistent Pointers?

### ✔ Higher performance
Fewer `malloc()` calls, less heap churn.

### ✔ Better stability
Pointers remain valid (memory is still allocated), dramatically reducing the risk of segmentation faults.

### ✔ Simpler code
No need for excessive pointer checks or mutexes around allocation/reallocation patterns.

---

## How It Works

These structures are managed through `_get()` / `_put()` pairs  
(e.g., `ha_get()` and `ha_put()`).

A structure may be handed out many times, but its underlying memory block  
**always stays allocated**, even if the object it represents is considered “freed.”

---

## Example

```c
struct alias {
    ...
    char name[0];     // always valid*
};

struct ifcond {
    ...
    struct alias *al; // always valid*
    ...
} *ifc;               // always valid*
```

\* **“Always valid”** means:  
- the **memory address** never goes stale (remains allocated),  
- but the **contents may correspond to a recycled object**.

Thus, accessing:

```c
ifc->al->name
```

is **always safe in the sense that it will not crash**, because every pointer in the chain points to persistent memory.

---

## Important Note

Persistent pointers do **not** magically prevent logical bugs:

- Dereferencing a pointer to a recycled object is still a **semantic bug**.
- The behavior may be incorrect if the object was reused.
- However, the program will **not crash**, making debugging far easier.

Persistent pointers allow ESPShell to remain robust under heavy internal churn while avoiding many categories of dangling-pointer crashes.
