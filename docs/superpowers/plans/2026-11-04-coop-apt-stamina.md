# APT stamina ownership investigation

Continue checkpoint12 (`0b19e50`) inline. The next visual change needs the
actual APT fill/notch objects and their render lifetime. Keep the current
status producer read-only until that boundary is established.

1. Decode local big-endian, 32-bit decoupled APT constants, timeline controls
   and action streams with checked offsets and explicit unsupported records.
   Preserve typed constants, exact byte offsets, function bodies and branches.
2. Test synthetic fixtures and malformed/truncated inputs without game assets.
   Run the decoder on Main_Hud, StaminaMeter and GenericMeter; retain local
   output under ignored artifacts, commit only the tool and evidence ledger.
3. Trace the render-side APT display boundary independently with a bounded
   read-only worker. Reconcile the asset methods with native state lifetime.
4. If a per-view render-local substitution is established, implement and
   validate distinct bar values plus leave/rejoin and diagnostic-off behavior.
   Otherwise record the precise remaining boundary; do not call shared APT
   setters on the render thread or claim independent widget pixels.
5. Run appropriate checks, update checkpoint/notes, commit a validated milestone
   without pushing. Do not commit extracted UI data or third-party tools.

Format reference: LibreSAGE/AptTools, commit
`7b6d499340181d15e484b58c0fc78b14215a5455`, `crates/apt/src/{actions,parse,constfile}.rs`.
Its little-endian layouts are a reference, not proof of this PPC build's
endianness or numeric semantics. Local bytes must settle those differences.

Outcome: steps 1–3 completed; 39 streams decoded across the three local pairs.
Step 4 remains gated on packet identity/retention evidence. The native display
tree changes during drawing; a private packet boundary is only a candidate.
Checkpoint13 records the precise continuation point. Tools passed 168 tests
and 71 subtests; extracted assets and third-party source stay ignored.
