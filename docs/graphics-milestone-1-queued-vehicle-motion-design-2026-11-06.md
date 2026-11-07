# M1: motion for source-identified queued vehicle batches

## Actual visible source

Build29488167, captured draw3633/present3634: depth attribution at raw606,229
identifies the visible car body as draw894, VS9A62E7C65051447A /
PS04CC5C7EC9FDFD7E,8844 vertices. Adjacent wheel/part draws use
315E6843ECE68A00/C66813A81FDBD7C7. These observations identify actual triangles,
not PS opacity. Corroborate with original PS ownership after specialization.

Both sources use c4 to split SV_VertexID into instance/mesh vertex, six float4
per instance starting c100, and camera c232..235. Body position uses the first
four rows; wheel position also rotates from c105.xy and c6
ObjectSpaceOffset_TurnDamp. Preserve all six rows and full actual VS inputs.
Post-VS previous clips already include that wheel deformation.

Saved runtime traces put both shaders in the unowned flush82786E88 through
8261CED0/8261B050, ending DrawVertices at8261B5A8. The derived renderable
823103C8/82021930 enqueues at82310C70/return82310C74. Its descriptor atSP160 has
rows6, owner+128 matrix, owner+16 flags, extra pointerSP192 and extra count2.
ModelSP136/count r14, slotr23/cursorr29 follow the already audited model chain.
The extra rows come from the actor simulation and include wheel pose.

## Exact queue provenance

Use manifest midasm hooks only, without changing arguments or guest control:

* Enqueue82310C70/82310C74 retains a source-qualified owner/token/part/descriptor
  scope with distinct origin9. Read the actual descriptor and model chain.
* Append82786F30/82786FE8 observes every relevant append, including unknown
  members. Group is r5 at entry; count offset r27=4*(group+3) at exit. The final
  hook runs before restoring its128-byte stack. A full batch can return without
  incrementing: never assume an append succeeded.
* Flush82786E84/82786E88 brackets the synchronous instanced call. Entry pointers
  are entry+0/+4, counts entry+12/+16, rows entry+20, group count entry+24 and
  packet VALUE at entry+32. Validate all passed arguments against this snapshot.
* At8261B58C, immediately before the fallback upload call820F4920 (return
  8261B590), only an active flush and firstconstant100 are relevant.
  Match its source pointer exactly to groupBuffer+16*6*firstMember and its row
  count to6*memberCount, at most126 rows (21 members). Verify frozen bytes.

The call-site hook is required: native FLOAT_COPY_VS can replace820F4920 and
return without executing its generated body. A hook inside that body would
miss the actual fast path. The source caller still executes before either
implementation. Only the observed fallback DrawVertices caller8261B5A8 consumes
this snapshot; alternate B050 paths remain unqualified.

Registry identity uses exact entry/group shape plus a fresh epoch when count0
starts a group. At append, require count-after=count-before+1 and copied
destination bytes equal the observed source six rows before recording that
slot. Unknown/malformed appends create holes or invalidate that group; they
must never preserve a known token in a reused slot. Changed buffer, packet,
row stride, unexpected count, group reuse, capacity or unreadable data fail
closed. Clear records after a flush; future count0 also starts a fresh epoch.
Never identify a member by nearest matrix, palette address or ordinal alone.

Enqueue can itself flush older batches before appending its current owner.
Therefore current owner TLS is explicitly NOT batch identity. Build each
upload snapshot only from the exact recorded member span, checking every
token/generation is still alive. Use ordered owner/part keys and ordered
generations as a batch identity; changed membership/order rejects that batch
for one frame. A runtime epoch is provenance, not a cross-frame identity key.

Carry a small flat immutable selection with member count, row stride, ordered
identity/generation hashes and full palette-word hash in each WorkCmd. Avoid
transient guest pointers and large per-draw arrays. The worker hashes actual
uploaded c100 words and requires equality, positive shader-driven fetch,
nonindexed first0, exact c4 stride/count relation and a conservative floating
interval proving no access outside the member palette. Both source hashes
and original PS pairs are explicit. Preserve actual VB/IB/fetch resource
generations, versions, topology and vertex order; no generic eligibility.

The source helper yields geometry inputs only. Grant the verified batch history
key after actual fetch/resource checks. Capture camera232 as projection input,
without replacing the independently audited global2C45 camera/depth seal.
Do not expand DXR whole-triangle opacity from these variable-alpha shaders.

## Tests and acceptance

Pure CPU tests cover exact append/commit, no increment, unknown holes, stale
counts, buffer/packet/epoch reuse, bounds, partial palette chunks, full batches,
dead/recycled generations, changed ordered members, palette mismatch, count
rounding, source and dynamic-fetch rejection. Shader specialization runs real
04CC/C668 corpus bodies through the unchanged structural guard and mixed-alpha
protocol. GPU/readback validation must show the actual car silhouette, valid
production motion and a camera-subtracted nonzero object component in motion.
Keep mixed/unverified pixels rejected. Then repack observed PSOs and repeat
three benchmark rounds and comparison captures outside diagnostic work.

## Evidência de execução posterior à implementação

Em logs/graphics_queued_vehicle_motion_20261106/capture, s3903: 21/21 draws de carroceria e52/52 de rodas correspondidos; 620 e55 pixels válidos de produção, respectivamente. A análise por proprietário original/câmera congelada encontrou84 pixels de carroceria acima de0,25px e17 de rodas acima de1px, depois de excluir mistura alfa. Não são apenas vetores de câmera.

Repetição em logs/graphics_m134_gameplay_20261106/capture, s4211, executável362b0a2170511216cc609203de9fc82be61fa2b6670c29cafe04b6c029b2b00e: 888 pixels de carroceria e100 de rodas com prova2, validade de produção e proprietário original não misturado. Residual da câmera:43 e9 pixels acima de1px; máximos1,734px/2,110px. A cena inclui Superman/capa, carro ao fundo e pedestre. s2235 teve zero pixels de carros visíveis e NÃO é considerado aprovação veicular. Superman/capa nessa primeira captura tiveram15688/68449 pixels válidos.

Comando reproduzível: python tools/graphics/inspect_rigid_camera_motion.py logs/graphics_m134_gameplay_20261106/capture --depth-format d24 --owner-vs 9A62E7C65051447A --owner-vs 315E6843ECE68A00. Ver owner_camera_inspection.json, owner_inspection.json e s004212_output_1280x720.png. Não há certificação de todas as variantes/LODs, transparências ou ausência universal de ghosting; casos desconhecidos preservam rejeição temporal. Benchmark final consta do relatório M134 separado.
