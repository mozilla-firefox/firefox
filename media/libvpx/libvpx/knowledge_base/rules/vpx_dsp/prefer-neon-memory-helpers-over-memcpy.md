---
name: libvpx/vpx_dsp/prefer-neon-memory-helpers-over-memcpy
description: In ARM NEON code, use mem_neon.h helper functions for unaligned loads and stores instead of passing vector variable addresses to memcpy
rubric: correctness
severity: should_fix
---

**Rule.** In ARM NEON implementations, use dedicated unaligned load and store
helper functions from vpx_dsp/arm/mem_neon.h (such as load_unaligned_u8_4x1(),
load_unaligned_u8(), load_unaligned_u16(), store_u8_4x1(), and
store_unaligned_u8()) to transfer data to or from vector registers. Do not pass
addresses of NEON vector variables directly to memcpy() (e.g., memcpy(&vec, ...)
or memcpy(dst, &vec, ...)), and avoid manual pointer-aliasing sequences or
intermediate vector zeroing.

**Why.** Taking the address of NEON vector types (such as uint8x8_t or
uint8x16_t) and passing them to memcpy() bypasses type safety, causes compiler
compatibility and code-generation issues on toolchains like MSVC on Windows, and
forces vector values onto the stack rather than keeping them in SIMD registers.
Dedicated mem_neon.h helper functions cleanly encapsulate unaligned memory
accesses, prevent redundant vector zeroing or variable address-of operations,
and allow compilers to optimize register allocation directly.

## Examples

Bad:

```cpp
// Bad: Taking address of vector variable to memcpy unaligned load or store.
uint8x8_t az = vdup_n_u8(0);
memcpy(&az, above - 1, 4);

// Bad: Direct memcpy from vector variable address to memory.
memcpy(dst, &az, 4);
```

Good:

```cpp
// Good: Use mem_neon.h unaligned load helper directly.
const uint8x8_t az = load_unaligned_u8_4x1(above - 1);

// Good: Use mem_neon.h store helper directly.
store_u8_4x1(dst, az);
```
