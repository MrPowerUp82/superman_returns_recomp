### Technical Investigation: Cloth Render-Object & Stream Correspondence

#### 1. Pointer Provenance & Owned Simulation Sources
* **`this` Pointer (`r30` in `82301510`, `r31` in `822F3FB0`)**:
  * Represents the persistent Cape Actor/Render Object instance (`size >= 28264` bytes).
  * Vtable slot 2 points directly to `82301510` (registered in `.rdata` at `82021DA8`).
* **Simulation Sub-Object (`r30 + 80`)**:
  * Passed to `822D85B0(r3 = this + 80, ...)` at `82301738` alongside transient scratch pointers (`r1+2048`, `r1+7456`, `r1+2080`).
  * In `822F3FB0`, `this + 80` is queried via `822C0B08`, `822C0B10`, and `822C0B18` to retrieve dynamic per-sub-stream vertex CPU buffers copied into VRAM via `825F6980`.
* **State / Material Host (`this + 28240`)**:
  * `*(this + 28240)` hosts an interface/context invoked across draw pass setup (`vtable[60]` at `822F4138`, `vtable[65]` pass count at `822F428C`, `vtable[66]` begin pass at `822F42E8`, `vtable[68]` at `822F43E8`).

---

#### 2. Ring Buffering vs. Stable Object Slots
* **Mod-3 Ring Index (`this + 28260`)**:
  * Incremented modulo 3 at `82301990` (`mulhw 21846`, remainder mod 3) after draw dispatch.
  * In `822F3FB0`, dynamically drives three triple-buffered dynamic vertex buffer bindings:
    * **Stream 0**: `*(this + (7049 + ring) * 4)` = `*(this + 28196 + ring * 4)` (stride = 16 bytes).
    * **Stream 1**: `*(this + (7052 + ring) * 4)` = `*(this + 28208 + ring * 4)` (stride = 16 bytes).
    * **Stream 3**: `*(this + (7055 + ring) * 4)` = `*(this + 28220 + ring * 4)` (stride = 16 bytes).
* **Static Bindings**:
  * **Stream 2 (VB)**: `*(this + 28232)` (stride = 8 bytes, constant across rings).
  * **Index Buffer**: `*(this + 28236)` via `820F2C08`.
  * **Topology Draw Call**: `820FC000(device, primitive_type=13 [D3DPT_TRIANGLELIST], base_vertex=0, count = *(this + 22548) << 2)`.

---

#### 3. Vertex Ordering, Topology, and Stable Particle Identity
* **Order & Count**:
  * Total vertex count is `*(this + 22544)`. Buffer payload size uploaded to VRAM is `count << 4` (16 bytes/vertex).
  * Vertex indices in Streams 0, 1, 2, and 3 map 1:1 with flat particle simulation indices `[0 .. N-1]` in the cloth sub-object at `this + 80`.
* **Topology Source**:
  * Static triangle list index buffer (`this + 28236`) indexes directly into this flat `0..N-1` order.
* **Stable Particle Identity**:
  * **Proof**: VRAM addresses and VB handles swap every frame through `this + 28260` (ring buffer), but Stream 2 (`this + 28232`, UVs/static data) and the IB (`this + 28236`) remain stationary.
  * **Inference**: Cloth particle $i$ consistently maps to vertex index $i$ in ring slots $0, 1, 3$. The pair `(this, particle_index_i)` provides invariant per-vertex identity across deformations without relying on buffer versions or memory addresses.

---

#### 4. Missing Proof & Next Caller/Lifecycle Targets
* **Missing Proof**: Layout of internal simulation structures inside `this + 80` (populated in `822D85B0`), and static init of Streams 0–3 / IB in the constructor.
* **Next Lifecycle Targets**:
  1. `822D85B0`: Cloth physics step and scratch update.
  2. `822E8BD0`: Object constructor allocating dynamic ring descriptors and binding `this + 28232/28236`.
  3. `823017FC`: Immediate caller orchestrating post-draw transform attachments (`822F4718`, `822F4878`).
