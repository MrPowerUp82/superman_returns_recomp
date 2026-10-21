import copy
import pytest
import texture_tracking_report as report


def runs():
    return [dict(name=str(i),mode=mode,order=i+1,exe_sha256="a",runtime_sha256="b",args=["--sr_native_fps_limit=60","--sr_renderer=native","--sr_native_api=d3d12","--sr_native_render_scale=1","--sr_native_msaa_samples=1","--sr_lsfg=false","--vsync=true","--window_width=1280","--window_height=720"],scene="new-game-midtown",profile=False,concurrent_work=False,hud_start=True,hud_end=True,image_alignment="aligned",image_gate_pass=True,scenarios=[dict(name=s,duration_s=20,fps=[20.0]*10) for s in ("idle","forward")]) for i,mode in enumerate(("off","on","on","off"))]


def stages(on=70):
    return [dict(exe_sha256="a",runtime_sha256="b",args=runs()[0]["args"],scene="new-game-midtown",profile=True,concurrent_work=False,mode=m,validation_ns=100 if m=="off" else on,runtime_ns=0,runtime_accounted=True,resource_bytes=1000,resource_checks=10) for m in ("off","on","on","off")]


@pytest.mark.parametrize("field,value",[("exe_sha256","other"),("runtime_sha256","other"),("args",["other"]),("scene","title"),("profile",True),("concurrent_work",True),("hud_end",False),("image_alignment","different_camera"),("image_gate_pass",False)])
def test_report_rejects_mismatched_binary_scene_and_instrumentation(field,value):
    data=runs();data[1][field]=value
    out=report.analyze(dict(runs=data,stages=stages()))
    assert not out["recommend_on"]
    assert out["reasons"]


def test_report_rejects_short_sample():
    data=runs();data[0]["scenarios"][0]["duration_s"]=2
    assert not report.analyze(dict(runs=data,stages=stages()))["recommend_on"]


def test_report_requires_repeated_thirty_percent_stage_gain():
    assert report.analyze(dict(runs=runs(),stages=stages(70)))["recommend_on"]
    assert not report.analyze(dict(runs=runs(),stages=stages(71)))["recommend_on"]
    assert not report.analyze(dict(runs=runs(),stages=stages(70)[:2]))["recommend_on"]


def test_bytes_without_accounted_runtime_cost_do_not_pass():
    data=stages(1);data[1]["runtime_accounted"]=False
    assert not report.analyze(dict(runs=runs(),stages=data))["recommend_on"]
    assert not report.analyze(dict(runs=runs(),stages=[]))["recommend_on"]


def test_repeated_fps_regression_rejects_promotion():
    data=runs()
    for r in data[1:3]:r["scenarios"][0]["fps"]=[10.0]*10
    assert not report.analyze(dict(runs=data,stages=stages()))["recommend_on"]


def test_camera_mismatch_is_inconclusive_not_pass():
    data=runs();data[1]["image_alignment"]="unknown"
    out=report.analyze(dict(runs=data,stages=stages()))
    assert out["status"]=="inconclusive"
    assert not out["recommend_on"]


def test_different_resource_workload_does_not_prove_gain():
    data=stages();data[1]["resource_bytes"]=10
    assert not report.analyze(dict(runs=runs(),stages=data))["recommend_on"]


def test_stage_runs_must_match_acceptance_binary():
    data=stages();data[1]["exe_sha256"]="unrelated"
    assert not report.analyze(dict(runs=runs(),stages=data))["recommend_on"]


@pytest.mark.parametrize("duration",[float("nan"),float("inf"),None,100])
def test_invalid_duration_never_promotes(duration):
    data=runs();data[0]["scenarios"][0]["duration_s"]=duration
    assert not report.analyze(dict(runs=data,stages=stages()))["recommend_on"]
