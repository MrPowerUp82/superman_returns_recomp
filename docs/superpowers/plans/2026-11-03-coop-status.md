# Per-player stamina handoff plan

Continue checkpoint11 inline. This milestone isolates the native data needed
for independent stamina widgets; it does not claim separate APT pixels.

**Native evidence:** HUD update `822B5DB0`, main return site `821513A4`; ACTR+104
power set, `82226C50` lookup of `StaminaPoints`, runtime magnitude +16 and
definition pointer +4 / maximum +16. The native HUD uses a minimum capacity
of 500 and a 10000-unit display scale. `82775EF8` enters HUD group 23 on the
render thread; split frame/view/generation already exist in `CurrentSplitHud`.

**Decision:** retain native APT updates. Read actors only on the simulation
thread, publish an immutable host pair under a short mutex, and select it by
the consumed split generation/view on the render thread. Never mutate actor
bins, power values, the HUD model or APT interpreter state. City health stays
shared. No native packet padding is repurposed.

- [x] Test native stamina normalization, invalid values/IDs, distinct actors,
  generation changes and view selection; observe red before implementation.
- [x] Add opt-in `sr_coop_status_probe`, implied by probe `-SplitPreview` and
  available as `-StatusProbe` with the owned camera dependency.
- [x] Read native P1/P2 stamina after the owned camera update; verify the
  original model's stamina/capacity/city cache stays byte-identical.
- [x] Select records at each split HUD entry; log actual actor/values/frame/
  generation and explicit `apt_override=false`.
- [x] Release/native/tools checks, owned runtime with explicit Lua power values,
  separate P2/P1 changes, leave/rejoin and disabled startup.
- [x] Bounded review and checkpoint12; commit this milestone without push.

Unknown: cloning the APT component/interpreter and its render-state lifetime.
The Windows REA provider does not admit this PowerPC target; existing generated
PPC disassembly and local APT archive evidence are used instead. UI evidence
remains ignored under `artifacts/coop-research/apt-hud`.

Ruling: the frontend HUD-update route did not publish during gameplay. Sample
after the existing owned-camera update instead, on its established simulation
thread with validated actor IDs. Read only the live model at 8296EE7C for the
preservation check. Leave native HUD updater dispatch untouched; APT callback
table 820C8F40 and frontend return821513A4 remain research entry points.
