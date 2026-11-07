# Same-frame live motion readback

The explicit live capture trigger is consumed by BeginFrame, normally opened by
the first draw. ExecOnSwap updates swap_number_ only after those draws. Comparing
the saved swap number at EndFrameAndPresent therefore misses the final output.

Keep the existing draw-side swap label for raw geometry diagnostics, but also
freeze frame_count_ + 1 when consuming the trigger. Select the final color and
NGX readbacks by that renderer frame ticket, which stays constant from BeginFrame
through submission. No adjacent-frame matching, routine readback, or production
rendering behavior changes. Validate with a live gameplay trigger: exactly one
output/NGX capture accompanies its MV capture, with the frame/swap mapping logged.

The same explicit capture also records the original pixel-owner texture and a
CSV mapping each replay draw to its shader, owner ID, correspondence, blend and
depth comparison. This distinguishes a matched draw from a valid visible pixel
and identifies later invalidators without weakening production coverage tests.
