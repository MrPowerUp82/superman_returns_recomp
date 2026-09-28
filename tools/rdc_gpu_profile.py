"""GPU time breakdown of a RenderDoc capture.

Run inside RenderDoc's embedded Python (qrenderdoc has renderdoc bound):
  qrenderdoc.exe --python tools/rdc_gpu_profile.py
with SR_RDC=<capture.rdc> and SR_RDC_OUT=<report.txt> in the environment.

Replays the frame, fetches EventGPUDuration for every action and writes:
  * total frame GPU time
  * time grouped by the enclosing debug marker path (depth 1 and 2)
  * time grouped by action kind (draw, dispatch, copy/resolve, clear)
  * the 40 most expensive actions with their marker path
"""

import os
import sys
from collections import defaultdict

import renderdoc as rd

capture_path = os.environ["SR_RDC"]
out_path = os.environ["SR_RDC_OUT"]
lines = []


def emit(text=""):
    lines.append(text)


def finish(code=0):
    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    os._exit(code)


try:
    cap = rd.OpenCaptureFile()
    status = cap.OpenFile(capture_path, "", None)
    if status != rd.ResultCode.Succeeded:
        emit(f"open failed: {status}")
        finish(1)
    status, controller = cap.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        emit(f"replay failed: {status}")
        finish(1)

    if rd.GPUCounter.EventGPUDuration not in controller.EnumerateCounters():
        emit("EventGPUDuration counter not available")
        finish(1)
    durations = {
        r.eventId: r.value.d
        for r in controller.FetchCounters([rd.GPUCounter.EventGPUDuration])
    }

    by_marker1 = defaultdict(float)
    by_marker2 = defaultdict(float)
    by_kind = defaultdict(float)
    count_kind = defaultdict(int)
    actions = []

    def kind_of(action):
        f = action.flags
        if f & rd.ActionFlags.Drawcall:
            return "draw"
        if f & rd.ActionFlags.Dispatch:
            return "dispatch"
        if f & (rd.ActionFlags.Copy | rd.ActionFlags.Resolve):
            return "copy/resolve"
        if f & rd.ActionFlags.Clear:
            return "clear"
        return "other"

    def walk(action, path):
        name = action.GetName(controller.GetStructuredFile())
        if action.children:
            for child in action.children:
                walk(child, path + [name])
            return
        t = durations.get(action.eventId, 0.0)
        kind = kind_of(action)
        by_kind[kind] += t
        count_kind[kind] += 1
        by_marker1[path[0] if path else "(none)"] += t
        by_marker2[" > ".join(path[:2]) if path else "(none)"] += t
        actions.append((t, action.eventId, kind, " > ".join(path[-3:]), name))

    for root in controller.GetRootActions():
        walk(root, [])

    total = sum(durations.values())
    emit(f"capture: {capture_path}")
    emit(f"actions: {len(actions)}  total GPU time: {total * 1000:.2f} ms")
    emit("")
    emit("by kind:")
    for k, t in sorted(by_kind.items(), key=lambda kv: -kv[1]):
        emit(f"  {t * 1000:8.2f} ms {100 * t / total:5.1f}%  {count_kind[k]:6d}  {k}")
    for title, table in (("by marker (depth 1):", by_marker1), ("by marker (depth 2):", by_marker2)):
        emit("")
        emit(title)
        for k, t in sorted(table.items(), key=lambda kv: -kv[1])[:40]:
            emit(f"  {t * 1000:8.2f} ms {100 * t / total:5.1f}%  {k}")
    emit("")
    emit("top actions:")
    for t, eid, kind, path, name in sorted(actions, reverse=True)[:40]:
        emit(f"  {t * 1000:7.3f} ms  eid {eid:6d}  {kind:12s} {path} :: {name[:80]}")
    controller.Shutdown()
    cap.Shutdown()
    finish(0)
except Exception as e:  # report instead of leaving the UI open
    import traceback

    emit("error: " + repr(e))
    emit(traceback.format_exc())
    finish(1)
