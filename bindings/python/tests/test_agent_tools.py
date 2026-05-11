# SPDX-License-Identifier: Apache-2.0
"""Agent tool surface exercised from Python.

The tests below run against `sx.ai.default_v1_tools()` and don't need
real plugins for the pure-state tools (read_geometry_summary, set_bc,
screenshot_viewport). The `mesh` end-to-end test reuses the
`loaded_registry` fixture from test_end_to_end.py and is skipped if no
in-tree plugins are built.
"""

from __future__ import annotations

import pytest

import pysouxmar as sx


def test_default_registry_contains_v1_tools():
    """Sprint 5 push 2 expanded the catalogue from 5 to 8 tools."""
    r = sx.ai.default_v1_tools()
    assert len(r) == 8
    assert set(r.list()) == {
        "read_geometry_summary",
        "mesh",
        "set_bc",
        "solve",
        "screenshot_viewport",
        "query_field",
        "compute_field",
        "propose_pipeline",
    }


def test_tool_metadata_round_trips():
    r = sx.ai.default_v1_tools()
    t = r.find("solve")
    assert t is not None
    assert t.name == "solve"
    assert t.category == "Solve"
    assert t.confirmation == sx.ai.Confirmation.ConfirmAlways
    assert "solver." in t.description


def test_missing_tool_returns_not_found():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    out = sx.ai.dispatch_tool(r, "no_such_tool", None, ctx, policy)
    assert out.error is not None
    assert out.error.code == "NOT_FOUND"


def test_read_geometry_summary_reads_from_session_state():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    ctx.session_state = {
        "geometry": {
            "num_vertices": 8,
            "num_edges":    12,
            "num_faces":    6,
            "num_solids":   1,
        }
    }
    policy = sx.ai.ConfirmationPolicy()
    out = sx.ai.dispatch_tool(r, "read_geometry_summary", None, ctx, policy)
    assert out.error is None, out.summary
    assert out.data["num_vertices"] == 8
    assert out.data["num_solids"]   == 1


def test_read_geometry_summary_reports_not_available_without_data():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    out = sx.ai.dispatch_tool(r, "read_geometry_summary", None, ctx, policy)
    assert out.error is not None
    assert out.error.code == "NOT_AVAILABLE"


def test_set_bc_blocked_without_confirmation():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    ctx.session_state = {}
    policy = sx.ai.ConfirmationPolicy()  # no prompter
    out = sx.ai.dispatch_tool(r, "set_bc",
        {"tag": "inlet", "type": "dirichlet", "value": 0.0}, ctx, policy)
    assert out.error is not None
    assert out.error.code == "NOT_CONFIRMED"


def test_set_bc_runs_with_auto_override():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    ctx.session_state = {}
    policy = sx.ai.ConfirmationPolicy()
    policy.overrides = {"set_bc": sx.ai.Confirmation.Auto}

    out = sx.ai.dispatch_tool(r, "set_bc",
        {"tag": "inlet", "type": "dirichlet", "value": 0.0}, ctx, policy)
    assert out.error is None, out.summary
    assert out.data["count"] == 1

    # session_state reads back the mutation.
    assert ctx.session_state["boundary_conditions"][0]["tag"] == "inlet"


def test_screenshot_viewport_not_available_in_headless_python():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    policy.overrides = {"screenshot_viewport": sx.ai.Confirmation.Auto}
    out = sx.ai.dispatch_tool(r, "screenshot_viewport", None, ctx, policy)
    assert out.error is not None
    assert out.error.code == "NOT_AVAILABLE"


def test_solve_requires_mesh_first():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    policy.overrides = {"solve": sx.ai.Confirmation.Auto}
    out = sx.ai.dispatch_tool(r, "solve",
        {"capability_id": "solver.linear"}, ctx, policy)
    assert out.error is not None
    assert out.error.code in ("INTERNAL", "PRECONDITION_FAILED")


def test_mesh_tool_against_real_plugin(loaded_registry):
    """End-to-end: invoke the `mesh` tool against the in-tree hello-mesher.
    Skips cleanly if no built plugins are found (via the loaded_registry
    fixture in conftest.py)."""
    registry, _handles = loaded_registry
    dispatcher = sx.RegistryDispatcher(registry)

    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    ctx.registry   = registry
    ctx.dispatcher = dispatcher
    policy = sx.ai.ConfirmationPolicy()

    out = sx.ai.dispatch_tool(r, "mesh",
        {"capability_id": "mesher.tetra.hello"}, ctx, policy)
    assert out.error is None, out.summary
    assert out.data["num_nodes"] == 4
    assert out.data["num_cells"] == 1


def test_value_yaml_roundtrip():
    obj = {"a": 1, "b": "hello", "c": [1, 2, 3], "d": {"from": "stage1"}}
    yaml_str = sx.emit_value_yaml(obj)
    back = sx.parse_value_yaml(yaml_str)
    assert back["a"] == 1.0
    assert back["b"] == "hello"
    assert back["c"] == [1.0, 2.0, 3.0]
    assert isinstance(back["d"], sx.StageRef)
    assert back["d"].stage_id == "stage1"


# ---- Sprint 5 push 2: new tools, audit log, session budget --------------

def test_propose_pipeline_round_trips_through_parser():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()

    spec = {
        "version": 1,
        "stages": [
            {"id": "mesh",  "plugin": "mesher.tetra.hello"},
            {"id": "write", "plugin": "writer.vtu",
             "input": {"mesh": {"from": "mesh"}, "path": "out.vtu"}},
        ],
    }
    out = sx.ai.dispatch_tool(r, "propose_pipeline", spec, ctx, policy)
    assert out.error is None, out.summary
    assert out.data["parsed_stages"] == 2.0
    assert "mesher.tetra.hello" in out.data["yaml"]


def test_propose_pipeline_rejects_missing_stages():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    out = sx.ai.dispatch_tool(r, "propose_pipeline", {"version": 1}, ctx, policy)
    assert out.error is not None
    assert out.error.code == "INVALID_ARGUMENT"


def test_compute_field_stub_reports_not_available():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    policy.overrides = {"compute_field": sx.ai.Confirmation.Auto}
    out = sx.ai.dispatch_tool(r, "compute_field", {}, ctx, policy)
    assert out.error is not None
    assert out.error.code == "NOT_AVAILABLE"


def test_query_field_requires_field_handle():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    out = sx.ai.dispatch_tool(r, "query_field", {}, ctx, policy)
    assert out.error is not None
    assert out.error.code == "PRECONDITION_FAILED"


def test_session_budget_threshold_callback(tmp_path):
    # The on_threshold callback isn't bound for the v1 surface (per the
    # README), so we exercise the threshold accounting via consumed_total
    # and the threshold-firing side effects we CAN observe (the
    # fired_thresholds_ bitset isn't exposed either, but record() returns
    # the new consumed_total and is idempotent on repeated crossings).
    b = sx.ai.SessionBudget()
    b.max_total_tokens = 1000

    assert b.record(200, 0) == 200
    assert b.record(400, 0) == 600   # crosses 50%
    assert b.record(300, 0) == 900   # crosses 80%
    assert b.record(200, 0) == 1100  # crosses 100%
    # Repeated calls past 100% must not crash.
    assert b.record(500, 0) == 1600


def test_audit_log_writes_lines_per_dispatch(tmp_path):
    log_path = tmp_path / "audit.log"
    log = sx.ai.AuditLog(log_path)

    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    ctx.audit_log = log
    policy = sx.ai.ConfirmationPolicy()

    # Three calls: NOT_FOUND, then NOT_AVAILABLE × 2 (the second from the
    # compute_field stub). AuditLog::append flushes after every write so
    # we don't need to close the log before reading.
    sx.ai.dispatch_tool(r, "no_such_tool",      {}, ctx, policy)
    sx.ai.dispatch_tool(r, "read_geometry_summary", {}, ctx, policy)
    policy.overrides = {"compute_field": sx.ai.Confirmation.Auto}
    sx.ai.dispatch_tool(r, "compute_field",     {}, ctx, policy)

    contents = log_path.read_text()
    lines = [line for line in contents.split("\n") if line.strip()]
    assert len(lines) == 3
    assert "tool: no_such_tool" in contents
    assert "outcome: not_found"  in contents
    assert "outcome: fail"       in contents      # compute_field NOT_AVAILABLE
    assert "tool: read_geometry_summary" in contents


def test_audit_log_default_path_honors_project_root(tmp_path):
    p = sx.ai.AuditLog.default_path(tmp_path)
    assert str(p).endswith(".souxmar/chat/audit.log") or \
           str(p).endswith(".souxmar\\chat\\audit.log")
