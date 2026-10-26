### Physics Source Analysis: `822D85B0` & Getters `822C0B08/10/18`

#### 1. Buffer Offsets & Getters
* **Getter `822C0B08` (`owner + 15500`)**: Returns pointer to an array of 16-byte elements (indexed via `r10 << 4`).
* **Getter `822C0B10` (`owner + 15508`)** & **`822C0B18` (`owner + 15512`)**: Expose distinct internal cloth buffer pointers (not written directly in `822D85B0`).
* **Internal Inline Buffer (`owner + 14608`)**: Array of 16-byte elements written sequentially (`r9 += 16`) in the final loop.
* **Flag Array (`owner + 15544`)**: Byte array indexed by `r7 = *(node + 156)`. Written with `1` (`stbx r25, r8, r7`).
* **Remap Table (`owner + 15576`)**: Array of `uint16_t` indexed up to count at `owner + 15676`.

---

#### 2. Count Dependencies & Loops in `822D85B0`
1. **Remap Setup Loop (`822D8688`)**:
   * Bound by count at `owner + 15676`.
   * Reads/updates `uint16_t` table at `owner + 15576` matching values against array from `822EB770`.
2. **Pose / Pinning Loops (`822D873C` & `822D87B4`)**:
   * Bound by count `N = *(owner + 15672)`. Stride = 304 bytes per node (`r11 += 304`).
   * Evaluates condition `*(node + 148) == 0`.
   * **Write target**: Writes float4 to `(owner + 15500)[index]` where `index = *(node + 156)`.
   * **Proof**: Indexed via explicit indirection `*(node + 156)`, **not** sequential loop index `r9`.
3. **Trailing Scratch / Integration Loop (`822D8CC0`)**:
   * Iterates `r7 = 0` to `N = *(owner + 15672)`, step 1.
   * Node stride: `r11 += 304` (base `owner + 304`).
   * Output buffer: `owner + 14608`, written sequentially (`r9 += 16`).
   * **Conditional Writes**: If filters (`node - 108 == 0`, `node - 112 != 1`, `node - 32 != 0, 1`) fail, branch skips `loc_822D8D70` (`stvx v0, r0, r9`), leaving gaps in `owner + 14608`.

---

#### 3. Scratch vs. Render & Order Stability
* **`owner + 14608`**: **Integration / scratch buffer**. Gaps occur when conditions fail; it does not map 1:1 to a compacted render VB without a separate gathering step.
* **`owner + 15500` (from `822C0B08`)**: **Pose / pin constraint buffer**. Indexed randomly via `*(node + 156)`.
* **Ordering Determination**: Neither loop enforces monotonic `0..V-1` vertex index order:
  * Pose writes depend on arbitrary sparse slot indices stored at `*(node + 156)`.
  * Integration writes sequentially to `14608`, but skips elements conditionally without compacting indices.

---

#### 4. Unprovided Callees Requiring Audit
* **`822D78F8`** (called at `822D8C68`): Takes `(this, *(this + 15732), arg3)` right before the integration loop; performs internal simulation/relaxation passes.
* **`822F3FB0` / `82301510`**: Must be audited to confirm whether the ring VBs (indexed by `mod 3`) consume `owner + 15500` directly via `*(node + 156)` or pull from an intermediate packing function converting cloth nodes to render streams.
