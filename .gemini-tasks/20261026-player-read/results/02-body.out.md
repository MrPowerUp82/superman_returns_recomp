### 1. Pointer Provenance & Descriptor Topology (`82306BB0`)
* **`r30` (`this`)**: Actor / model instance pointer passed as `r3`. Saved to `340(r1)` and preserved across setup.
* **`r16` (Model / Mesh Group Header)**: `r30 -> +88 -> +12 -> +12`.
  * `r14`: `r16 + 64` (Skeleton / deformation node table descriptor, gated by `lbz 108(r16)`).
  * `r26`: `r16 + 12` (Base pointer to array of part/submesh pointer descriptors).
  * `r17`: `r16 + 8` (Total part / submesh count).
  * `r23`: `r16 + 80` (Part visibility / dirty mask byte-array, indexed by part index `r24`).
  * `r29`: `r16 + 68` (Transform / scale block; `52(r29)` uniform scale factored with `f13 = 5580(r8)`).
* **`r8` (Global / Render Context)**: Loaded from `268(r30)`.

---

### 2. Part Iteration & `8261CD90` Selection Inputs
Loop index `r24` iterates from `0` to `r17 - 1`:
* **Part Selector**: `r26` advances by `+4` each iteration (`addi r26, r26, 4`). The current submesh descriptor pointer is `*r26` (`lwz r3, 0(r26)`).
* **Culling Gates**:
  1. `lbzx r11, r24, r23`: Bit 0 must be non-zero (part marked active).
  2. `lwz r11, 260(r30)`: Bitmask test `(1 << r24) & r11 == 0` (part not suppressed).
  3. Chunk walker at `823071F0`: Traverses sub-buffers (`+116` to `+8(r11)`) and configures `lwz r11, 256(r30) -> 16(r3)` via `82609500`.
* **Observer Call (`82307290`)**:
  * Calls `bl 0x8261cd90`.
  * Input: `r3 = *r26` (direct submesh/part descriptor pointer).

---

### 3. Non-Volatile Register State at `82307298` (Return from `8261CD90`)
* **`r14`**: Deformation / skeleton structure base (`*(r16 + 64)`).
* **`r15`**: Mode flag (boolean derived from `*(r25 + 4)` & `*(r25 + 8)`).
* **`r16`**: Root mesh group descriptor header (`*(*(*(r30+88)+12)+12)`).
* **`r17`**: Submesh array length (immutable loop bound).
* **`r18`**: Constant `1`.
* **`r20`**: Constant `0`.
* **`r21`**: Buffer allocated via `82318650` (size `8000`).
* **`r22`**: Accumulator / output handle returned by `822F2A38` (post-pass draw-packet count).
* **`r23`**: Part flag buffer pointer (`r16 + 80`).
* **`r24`**: Current 0-based part index `i`.
* **`r25`**: Allocation success flag (`r18` if allocated, else `0`).
* **`r26`**: Array cursor pointing directly to `&parts[i]` (`base_r26 + i * 4`).
* **`r27`**: Chunk bounds offset (`*(r11 + 8)` from header).
* **`r28`**: Global state block (`-0x7D68(r11)`).
* **`r29`**: Last chunk address traversed in submesh data.
* **`r30`**: Instance `this` pointer.
* **`r31`**: Static dispatch table base (`-0x7D65(r11)`).

---

### 4. Proof vs. Inference
* **Proven**: Part index is sequential `0 .. r17-1`; `*r26` is the sole input parameter `r3` dispatched to `8261CD90`; clearing byte `stbx r20, r24, r23` at `82307298` consumes the dirty flag.
* **Inferred**: Binding of `*r26` to C687 (body) vs A5232 (cape) is determined by the asset-relative slot index `r24` matching static archetype table offsets rather than dynamic sorting.

---

### 5. Next Lifecycle & Caller Addresses
1. **`8261CD90`**: Callee inspecting `r3` (`*r26`) to construct low-level draw packets.
2. **`823072EC` (`826156D8`)**: Emits accumulated secondary command packets from buffer `r21`.
3. **`8261CDF8`**: Caller returning into `82307298` chain.
