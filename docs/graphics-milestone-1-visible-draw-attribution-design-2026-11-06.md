# M1: identify the actual visible vehicle draws

Source families and matched draw counts have repeatedly identified static props,
not the visible car body. Original-PS ownership now shows lamp posts/benches for
the newly audited families. Stop assigning actor classes from those counts.

Add an explicit live-capture-only diagnostic replay in a separate TemporalMotion
instance. Replay every captured post-VS triangle in original order against the
same frozen scene depth, with the original cull/bias/scissor and EQUAL depth.
Use each current clip as its own previous clip solely to satisfy resource
validation; this replay grants no correspondence. Disable its ownership filter
and encode draw ordinal+1 into RG16F as two exact bytes (low8, high8), with an
R8 diagnostic coverage flag. Zero remains no visible triangle. Add a default-off
input flag to the private diagnostic API; normal rendering and NGX inputs never
consume these outputs. Reuse root constant coverage_enabled=2 as an explicit
diagnostic branch and coverage_id as its tag, preserving the normal0/1 protocol.

This is a depth-matched triangle attribution, not original PS coverage or proof
of opacity. Alpha holes, equal-depth overlaps, contributors without depth writes
and depth bias can make it an upper bound. Inspect the presented image and exact
CSV row before associating a car; original-PS silhouettes corroborate where
available. Do not use diagnostic IDs, frame ordinals or nearest transforms as
production identity. Production history remains unchanged.

Capture tagged buffers only with the existing explicit trigger and debug flag,
alongside the retained scene CSV and same-frame presentation ticket. Validate
the byte encoding and overlap order with the real GPU fixture (including ID257),
and run ordinary motion regressions to ensure default behavior is unchanged.
Retain the raw buffers and a deterministic inspection report/image mapping tags
to the source VS/PS and existing motion proof. This should resolve the vehicle
source question before any further moving-instance eligibility is expanded.
