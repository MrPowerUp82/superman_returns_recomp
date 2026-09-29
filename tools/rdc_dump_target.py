"""Dump the first RenderDoc color target at chosen event IDs as raw bytes.

Run in qrenderdoc --python with SR_RDC, SR_RDC_EVENTS and SR_RDC_OUT_DIR.
"""

import os
import traceback
from pathlib import Path

import renderdoc as rd


out_dir = Path(os.environ["SR_RDC_OUT_DIR"])
out_dir.mkdir(parents=True, exist_ok=True)
lines = []
try:
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ["SR_RDC"], "", None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(f"open failed: {status}")
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(f"replay failed: {status}")
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    for eid in [int(x) for x in os.environ["SR_RDC_EVENTS"].split(",")]:
        controller.SetFrameEvent(eid, False)
        target = controller.GetPipelineState().GetOutputTargets()[0]
        texture = textures[str(target.resource)]
        data = controller.GetTextureData(target.resource, rd.Subresource())
        path = out_dir / f"event_{eid}.raw"
        path.write_bytes(data)
        lines.append(
            f"{eid},{target.resource},{texture.width},{texture.height},"
            f"{texture.format.Name()},{len(data)},{path}"
        )
    controller.Shutdown()
    capture.Shutdown()
except Exception:
    lines.append(traceback.format_exc())
(out_dir / "index.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
os._exit(0)
