"""Inspect mesh inputs and post-VS data with qrenderdoc --python.

SR_RDC selects the capture, SR_RDC_OUT the JSON report. No game files needed.
"""
import json
import os
import struct
import traceback

import renderdoc as rd


def fields(obj):
    return {name: str(getattr(obj, name)) for name in dir(obj)
            if not name.startswith("_") and not callable(getattr(obj, name))}


def main():
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ["SR_RDC"], "", None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(str(status))
    actions = []

    def walk(action):
        if action.flags & rd.ActionFlags.Drawcall and action.numIndices > 300:
            actions.append(action)
        for child in action.children:
            walk(child)

    for root in controller.GetRootActions():
        walk(root)
    report = []
    for action in sorted(actions, key=lambda a: -a.numIndices)[:12]:
        controller.SetFrameEvent(action.eventId, False)
        pipe = controller.GetPipelineState()
        item = {"event": action.eventId, "action": fields(action),
                "attributes": [fields(a) for a in pipe.GetVertexInputs()],
                "buffers": [fields(b) for b in pipe.GetVBuffers()],
                "indices": fields(pipe.GetIBuffer())}
        post = controller.GetPostVSData(0, 0, rd.MeshDataStage.VSOut)
        item["post_vs"] = fields(post)
        if post.vertexResourceId != rd.ResourceId.Null():
            raw = bytes(controller.GetBufferData(post.vertexResourceId,
                        post.vertexByteOffset, post.vertexByteStride * 16))
            item["positions"] = [struct.unpack_from("<4f", raw, i)
                                 for i in range(0, len(raw) - 15, post.vertexByteStride)]
        report.append(item)
    controller.Shutdown()
    capture.Shutdown()
    return report


try:
    result = main()
except Exception:
    result = {"error": traceback.format_exc()}
with open(os.environ["SR_RDC_OUT"], "w", encoding="utf-8") as output:
    json.dump(result, output, indent=2)
os._exit(0)
