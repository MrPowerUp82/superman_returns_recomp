# APT draw bridge runtime investigation

Continue checkpoint13 (`fc5bd80`). Observe before attempting any per-view APT
replacement. No native interpreter/tree/packet writes in this milestone.

- Add default-off `sr_coop_apt_probe`, requiring split preview and co-op input,
  excluded from benchmark mode. Probe `-AptProbe` supplies split dependencies.
- Scope host-only counters to actual `82775EF8` HUD entries; record consumed
  frame/generation/view. Sample first three and every 120th split frame.
- Observe `8268C568` node/affine data and `826ACD18` carrier scope. At
  `826D1648`, record object/vtable method, caller, source relation, original
  packet readback and transform bytes before/after the native consumer.
  Cap packet/node details to 256 each per sampled HUD pass; log total counts.
- Implement/test a report requiring both HUD passes in composed frames and
  matching native packet/transform/source readbacks; preserve diagnostic limits.
- Build Release. Run the owned isolated save with distinct explicit native
  stamina values, leave/rejoin, and diagnostic-off startup. Archive only ignored
  runtime evidence. Identify packet/asset correlation and remaining lifetime gaps.
- Review, update checkpoint14/ledger/notes, commit without push.

Parallel read-only native work: source-carrier/asset identity, independent of
the parent-owned runtime probe and evidence report.

Completed: Release build; owned PID20652 distinct values and leave/rejoin;
9422 packet observations / 62 HUD passes / 31 composed frames, both bindings;
seven GenericMeter shape matches; bounded synchronous-consumer analysis;
diagnostic-off PID30764; 173 tools tests / 89 subtests. Review's report join and
generation validation findings reproduced red and fixed. Checkpoint14 records
the next boundary: unique stamina-instance ancestry before private substitution.
