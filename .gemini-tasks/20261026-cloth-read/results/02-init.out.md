### 1. Resource Construction & Vertex/Topology Mapping (`822F3638`)

Function `822F3638(owner r31, source r30)` configures the mesh buffers depending on mode `*(r30 + 4792)`:

#### **Mode 0: Procedural Grid Generation** (`*(r30 + 4792) == 0` at `0x822F3C54`)
* **Grid Bounds & Vertex Count**:
  * Width: $W = \text{*(r30 + 4836)}$ (`r30`), Height: $H = \text{*(r30 + 4840)}$ (`r27`).
  * Vertex Count: `owner + 22544` $= W \times H$.
  * Quad Count: `owner + 22548` $= (W - 1) \times (H - 1)$.
* **Index Buffer (`owner + 28236`)**:
  * Allocated as 16-bit indices (`li r5, 1; li r4, 8; mullw ... rlwinm r3, r11, 1, 0, 30`).
  * Quad loop indices (`0x822F3D70`): For row $y \in [0, H-2]$, col $x \in [0, W-2]$, emitting 4 indices per quad:
    $$\{ (y \cdot W + x), (y \cdot W + x + 1), ((y + 1) \cdot W + x + 1), ((y + 1) \cdot W + x) \}$$
    *(Emitted in code via offsets `+65535` (i.e. $-1$ base adjustment), `0`, `+W`, `+W - 1`).*
* **Static UV Stream 2 (`owner + 28232`)**:
  * Size: `VertexCount * 8` bytes (`float2 UV`, 2 floats).
  * Evaluated as: $U = x / (W - 1)$, $V = y / (H - 1)$ for row $y \in [0, H-1]$, col $x \in [0, W-1]$.
  * Flat indexing: Vertex $i = y \cdot W + x$ maps directly to row-major $(x, y)$.

#### **Mode 1: Pre-Baked Buffer Copy** (`*(r30 + 4792) == 1` at `0x822F3AFC`)
* Reads descriptor pointer `r11 = *(r30 + 27752)`:
  * Dynamic ring vertex count: `*(r11 + 4) * *(r11 + 8)` stored at `owner + 22544`.
  * Index count: `*(r11 + 28) >> 2` stored at `owner + 22548`.
  * Stream 2 (UV): Copied from `*(r11 + 24)` via `memcpy` (`0x825F6980`).
  * Static IB: Copied from `*(r11 + 36)` via `memcpy` (`0x825F6980`).

---

### 2. Stream Mapping & Topology Proof vs. Inference

* **CPU Simulation $\to$ Dynamic Ring Streams (0, 1, 3)**:
  * **Proof**: In both Mode 0 and Mode 1, dynamic ring buffers are sized strictly by `VertexCount` $\times \text{stride}$ (allocated 3 ring buffers at `owner + 28208` via `0x820F3ED8`).
  * **Proof**: Flat vertex index $i \in [0, \text{VertexCount}-1]$ directly indexes dynamic streams 0, 1, and 3 and static UV stream 2 with a 1:1 identity mapping (row-major order $y \cdot W + x$). No dynamic index remap table exists in `822F3638`.
* **Disambiguating Dynamic Ring Slots**:
  * **Proof**: Buffers are only recreated when `822F3638` executes. `822C9318` initializes instance vectors (`0x822C6A40`, identity/matrix vectors) and sets up state at offsets `15728..21104`, but does not touch GPU buffer handles `28208..28236`.
  * **Proof**: `owner + 28260 (mod 3)` indexes the triple-buffered dynamic ring. Because `VertexCount`, static UV (`28232`), and static IB (`28236`) are immutable between `822F3638` calls, **owner constructor generation + (Static UV + IB content/pointer)** is sufficient to identify topology and vertex layout without a separate topology generation.

---

### 3. Remaining Unprovided Callees
* `0x820F3ED8`, `0x820F4040`, `0x820F40F0`, `0x820F4150`, `0x820F3FA0`, `0x820F4000`: D3D/GPU resource allocator, lock/unlock functions.
* `0x822E7148`: Conditional sub-allocator triggered when `*(owner + 28248) != 0`.
* The physics simulation update function feeding ring slots prior to `82301510` / `822F3FB0`.
