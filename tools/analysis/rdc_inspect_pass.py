"""Inspect render targets and draw parameters for selected RenderDoc events.

Run in qrenderdoc --python with SR_RDC and SR_RDC_OUT in the environment.
Optional SR_RDC_EVENTS is a comma-separated list of event IDs.
"""

import os
import traceback
from collections import Counter

import renderdoc as rd


def main():
    capture = rd.OpenCaptureFile()
    status = capture.OpenFile(os.environ["SR_RDC"], "", None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(f"open failed: {status}")
    status, controller = capture.OpenCapture(rd.ReplayOptions(), None)
    if status != rd.ResultCode.Succeeded:
        raise RuntimeError(f"replay failed: {status}")

    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    requested = {
        int(x) for x in os.environ.get("SR_RDC_EVENTS", "").split(",") if x.strip()
    }
    if not requested:
        requested = {250, 257, 4844, 4851, 8972, 17094, 23535, 28128}

    actions = {}

    def walk(action):
        actions[action.eventId] = action
        for child in action.children:
            walk(child)

    for root in controller.GetRootActions():
        walk(root)
    index_count = os.environ.get("SR_RDC_INDEX_COUNT")
    if index_count:
        matching = [a.eventId for a in actions.values()
                    if a.flags & rd.ActionFlags.Drawcall and
                    a.numIndices == int(index_count)]
        requested = set(sorted(matching)[:16])

    def target_info(target):
        rid = str(target.resource)
        tex = textures.get(rid)
        if tex:
            return f"{rid} {tex.width}x{tex.height} fmt={tex.format.Name()}"
        return f"{rid} (no texture description)"

    lines = []
    if os.environ.get("SR_RDC_API_HELP"):
        lines.append("GetTextureData: " + str(controller.GetTextureData.__doc__))
        lines.append("texture symbols: " + str([x for x in dir(rd) if 'Texture' in x or 'Subresource' in x]))
        controller.SetFrameEvent(min(requested), False)
        lines.append("target fields: " + str(dir(controller.GetPipelineState().GetOutputTargets()[0])))
        lines.append("usage methods: " + str([x for x in dir(controller) if 'Usage' in x]))
        lines.append("GetUsage: " + str(controller.GetUsage.__doc__))
    for eid in sorted(requested):
        action = actions.get(eid)
        if not action:
            lines.append(f"eid {eid}: no action")
            continue
        controller.SetFrameEvent(eid, False)
        pipe = controller.GetPipelineState()
        lines.append(
            f"eid {eid}: {action.GetName(controller.GetStructuredFile())} "
            f"indices={action.numIndices} instances={action.numInstances}"
        )
        lines.append("  color: " + "; ".join(target_info(t) for t in pipe.GetOutputTargets()))
        lines.append("  color view formats: " + "; ".join(t.format.Name() for t in pipe.GetOutputTargets()))
        lines.append("  depth: " + target_info(pipe.GetDepthTarget()))
        depth_state = pipe.GetDepthTestState()
        blend = pipe.GetColorBlends()[0]
        lines.append("  depth test: " + str({x: str(getattr(depth_state, x)) for x in dir(depth_state)
                                               if not x.startswith("_") and not callable(getattr(depth_state, x))}))
        lines.append("  blend 0: " + str({x: str(getattr(blend, x)) for x in dir(blend)
                                         if not x.startswith("_") and not callable(getattr(blend, x))}))
        for channel in ("colorBlend", "alphaBlend"):
            equation = getattr(blend, channel)
            lines.append("  " + channel + ": " + str({x: str(getattr(equation, x)) for x in dir(equation)
                                                if not x.startswith("_") and not callable(getattr(equation, x))}))
        if os.environ.get("SR_RDC_API_HELP"):
            lines.append("  pipe methods: " + str([x for x in dir(pipe) if 'Blend' in x or 'Depth' in x or 'State' in x]))
        lines.append("  VS: " + str(pipe.GetShader(rd.ShaderStage.Vertex)))
        lines.append("  PS: " + str(pipe.GetShader(rd.ShaderStage.Pixel)))
        if os.environ.get("SR_RDC_RESOURCE_USAGE"):
            usage = controller.GetUsage(pipe.GetOutputTargets()[0].resource)
            counts = Counter(str(item.usage) for item in usage)
            lines.append("  target usage: " + str(counts))
            reads = [item for item in usage
                     if str(item.usage) in ("ResourceUsage.PS_Resource", "ResourceUsage.CS_Resource")]
            lines.append("  first shader reads: " + str([(item.eventId, str(item.usage))
                                                       for item in reads[:12]]))

    controller.Shutdown()
    capture.Shutdown()
    return "\n".join(lines) + "\n"


try:
    result = main()
except Exception:
    result = traceback.format_exc()
with open(os.environ["SR_RDC_OUT"], "w", encoding="utf-8") as output:
    output.write(result)
os._exit(0)
