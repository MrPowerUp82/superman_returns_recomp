# M1: attribute visible rigid motion without changing production coverage

Current gameplay proves skin/cape pixel ownership and matched rigid draws. A
rigid shader or renderable token does not establish that a moving car received
valid pixels. This remaining acceptance question needs visible-pixel evidence.

On an explicit live capture, and only with the existing debug observed-motion
flag, replay the same geometry/depth/owner tests into a separate TemporalMotion
instance. Keep correspondence only for already verified rigid instances;
retain every other draw as an invalidator. Never turn an invalid draw valid.
The resulting rigid-only validity/velocity readbacks are diagnostic outputs,
never DLSS/denoiser inputs, identity certificates, or production readiness.

Record instance/generation/proof columns beside the current draw CSV. Also
freeze the actual DepthMotionConstants supplied to NGX input composition in
a versioned binary diagnostic: the exact current inverse/previous matrices,
dimensions, mode, jitter and clear value. This allows an offline reconstruction
of the existing camera-only formula from the same frozen depth. Compare the
rigid pixels with that camera reference and inspect residual motion on visible
vehicles, rather than interpreting all screen movement as object movement.

All work is gated by the explicit live frame ticket, nonempty dump directory
and debug flag. Production output resources and their lineage remain untouched.
Diagnostic initialization/readbacks can create PSOs and stall the GPU; these
runs must be separate from performance/cache acceptance windows. The completed
benchmark matrices retain their exact pre-diagnostic executable hashes.
