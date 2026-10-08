"""Consistency of port/src/native_renderer/game_profile.h with
docs/data/xdk_match.tsv: every candidate that is still UNCONFIRMED must be
the TSV's best address (or one of its ambiguous candidates) for the XDK
function of its role, and its comment must quote the TSV status and score.
Once the owner confirms a role, it may legitimately differ from the TSV."""
import csv
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PROFILE = ROOT / "port" / "src" / "native_renderer" / "game_profile.h"
TSV = ROOT / "docs" / "data" / "xdk_match.tsv"

# Role -> function name in the TSV (None: the candidate does not come from it).
ROLE_TO_TSV = {
    "DRAW_VERTICES": "D3DDevice_DrawVertices",
    "DRAW_INDEXED_VERTICES": "D3DDevice_DrawIndexedVertices",
    "DRAW_VERTICES_UP": "D3DDevice_DrawVerticesUP",
    "BEGIN_VERTICES": "D3DDevice_BeginVertices",
    "END_VERTICES": None,  # layout guess, see the profile comment
    "RESOLVE": "D3DDevice_Resolve",
    "BEGIN_TILING": "D3DDevice_BeginTiling",
    "END_TILING": "D3DDevice_EndTiling",
    "CLEAR": "D3DDevice_Clear",
    "RING_MAKE_SPACE": "D3D_RingMakeSpace(KickOff+wrap)",
    "RING_ALLOC_LARGE": "D3D_RingAllocLargeSegment",
    "RESERVE_INLINE_CONSTANTS": "D3D_ReserveInlineConstants(SET_CONSTANT)",
    "LOAD_SHADER_LITERALS": "D3D_LoadShaderLiterals(LOAD_ALU_CONSTANT)",
    "GPU_BEGIN_SHADER_CONSTANT_F4": "D3DDevice_GpuBeginShaderConstantF4",
    "VERTEX_BUFFER_UNLOCK": "D3DVertexBuffer_Unlock",
    "INDEX_BUFFER_UNLOCK": "D3DIndexBuffer_Unlock",
    "CREATE_SHADER_A": "D3DDevice_CreateShaderA(container)",
    "CREATE_SHADER_B": "D3DDevice_CreateShaderB(container)",
    "SWAP": None,  # project evidence (frame_stats.cpp), the TSV has no candidate
    "BLOCK_ON_FENCE": "D3D_BlockOnFence(dev,fence)",
    "POLL_GPU_PROGRESS": "D3D_PollGpuProgress(BlockOnFence poll)",
    "RESOURCE_UNLOCK": None,  # shared target of the two confirmed buffer unlock wrappers
    "FRAME_HANDOFF": None,  # statically inspected dispatch, runtime targets in checkpoint14
}


def profile_roles():
    text = PROFILE.read_text()
    roles = {}
    for m in re.finditer(r"#define SR_ADDR_([A-Z0-9_]+) ([0-9A-F]{8})\s*//\s*(.*)", text):
        role, addr, comment = m.groups()
        confirmed = re.search(rf"#define SR_CONFIRMED_{role} (\d)", text).group(1) == "1"
        roles[role] = {"address": addr, "comment": comment, "confirmed": confirmed}
    return roles


def tsv_rows():
    with TSV.open() as f:
        return {row["name"]: row for row in csv.DictReader(f, delimiter="\t")}


def test_every_role_is_mapped():
    assert set(profile_roles()) == set(ROLE_TO_TSV)


def test_unconfirmed_candidates_come_from_the_tsv():
    rows = tsv_rows()
    for role, info in profile_roles().items():
        name = ROLE_TO_TSV[role]
        if info["confirmed"] or name is None:
            continue
        row = rows[name]
        allowed = {row["best_addr"], *row["candidates"].split()}
        assert info["address"] in allowed, (role, info["address"], row["best_addr"])
        assert "UNCONFIRMED" in info["comment"], role
        # The comment quotes the TSV status and score, e.g. "missing (0.060)".
        assert f"{row['status']} ({row['score']})" in info["comment"], (role, info["comment"])


def test_unconfirmed_roles_keep_the_marker():
    for role, info in profile_roles().items():
        if not info["confirmed"]:
            assert "UNCONFIRMED" in info["comment"], role
