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
    """Sprint 6 push 3 expanded the catalogue from 9 to 12 tools."""
    r = sx.ai.default_v1_tools()
    assert len(r) == 12
    assert set(r.list()) == {
        "read_geometry_summary",
        "mesh",
        "set_bc",
        "solve",
        "screenshot_viewport",
        "query_field",
        "compute_field",
        "propose_pipeline",
        "query_mesh_quality",
        "set_material",
        "list_plugins",
        "apply_pipeline_diff",
        "export_results",
    }


def test_set_material_stages_session_state():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    ctx.session_state = {}
    policy = sx.ai.ConfirmationPolicy()
    policy.prompter = lambda tool, inputs: True

    out = sx.ai.dispatch_tool(r, "set_material", {
        "tag": "body",
        "model": "linear_elastic",
        "properties": {"E": 210e9, "nu": 0.3, "rho": 7850.0},
    }, ctx, policy)
    assert out.error is None, out.summary
    assert out.data["count"] == 1
    mats = ctx.session_state["materials"]
    assert len(mats) == 1
    assert mats[0]["tag"] == "body"
    assert mats[0]["model"] == "linear_elastic"


def test_list_plugins_on_empty_registry():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    ctx.registry = sx.Registry()
    policy = sx.ai.ConfirmationPolicy()
    out = sx.ai.dispatch_tool(r, "list_plugins", None, ctx, policy)
    assert out.error is None
    assert out.data["count_total"] == 0
    assert out.data["capabilities"] == []


def test_apply_pipeline_diff_adds_stage():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    policy.prompter = lambda tool, inputs: True

    out = sx.ai.dispatch_tool(r, "apply_pipeline_diff", {
        "base": {
            "version": 1,
            "stages": [
                {"id": "mesh", "plugin": "mesher.tetra.hello"},
            ],
        },
        "ops": [
            {"op": "add", "after": "mesh", "stage": {
                "id": "write", "plugin": "writer.vtu",
                "input": {"mesh": {"from": "mesh"}, "path": "/tmp/out.vtu"},
            }},
        ],
    }, ctx, policy)
    assert out.error is None, out.summary
    assert out.data["parsed_stages"] == 2
    assert "writer.vtu" in out.data["yaml"]


def test_apply_pipeline_diff_rejects_dangling():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    policy.prompter = lambda tool, inputs: True

    out = sx.ai.dispatch_tool(r, "apply_pipeline_diff", {
        "base": {
            "version": 1,
            "stages": [
                {"id": "mesh", "plugin": "mesher.tetra.hello"},
                {"id": "write", "plugin": "writer.vtu",
                 "input": {"mesh": {"from": "mesh"}}},
            ],
        },
        "ops": [
            {"op": "remove", "id": "mesh"},
        ],
    }, ctx, policy)
    assert out.error is not None
    assert out.error.code == "INVALID_ARGUMENT"


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


def test_compute_field_requires_capability_id():
    """Sprint 5 push 3 replaced the NOT_AVAILABLE stub with a real
    dispatcher path. Without `capability_id` we get INVALID_ARGUMENT;
    without an active session we get PLUGIN_NOT_FOUND or
    PRECONDITION_FAILED depending on which precondition trips first."""
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    policy.overrides = {"compute_field": sx.ai.Confirmation.Auto}
    out = sx.ai.dispatch_tool(r, "compute_field", {}, ctx, policy)
    assert out.error is not None
    assert out.error.code == "INVALID_ARGUMENT"


def test_query_field_requires_field_handle():
    r = sx.ai.default_v1_tools()
    ctx = sx.ai.ToolContext()
    policy = sx.ai.ConfirmationPolicy()
    out = sx.ai.dispatch_tool(r, "query_field", {}, ctx, policy)
    assert out.error is not None
    assert out.error.code == "PRECONDITION_FAILED"


def test_session_budget_threshold_callback(tmp_path):
    # Sprint 6 push 6 — the on_threshold callback is now first-class
    # from Python. It fires once per crossed (axis, threshold) pair.
    b = sx.ai.SessionBudget()
    b.max_total_tokens = 1000
    fired = []
    b.on_threshold = lambda pct, axis, cur: fired.append((pct, axis, cur.consumed_total))

    assert b.record(200, 0) == 200
    assert b.record(400, 0) == 600   # crosses 50%
    assert b.record(300, 0) == 900   # crosses 80%
    assert b.record(200, 0) == 1100  # crosses 100%
    # Repeated calls past 100% must not re-fire.
    assert b.record(500, 0) == 1600

    pcts_total = [p for (p, axis, _cur) in fired if axis == "total"]
    assert pcts_total == [50, 80, 100]

    # Clearing the callback (set to None) is well-defined.
    b.on_threshold = None
    # Re-cross from a fresh budget — no new firings into the cleared list.
    b2 = sx.ai.SessionBudget()
    b2.max_total_tokens = 100
    b2.on_threshold = None
    b2.record(50, 50)  # crosses every threshold; callback is None so nothing fires


def test_budget_config_round_trip(tmp_path):
    cfg_path = tmp_path / "budget.toml"
    cfg_path.write_text("""[budget]
max_input_tokens  = 200000
max_output_tokens =  50000
max_total_tokens  = 250000
""")
    cfg = sx.ai.parse_budget_config_file(cfg_path)
    assert cfg.max_input_tokens  == 200000
    assert cfg.max_output_tokens ==  50000
    assert cfg.max_total_tokens  == 250000

    b = sx.ai.SessionBudget()
    cfg.apply_to(b)
    assert b.max_total_tokens == 250000
    assert b.consumed_total   == 0   # apply_to leaves counters alone


def test_budget_config_default_path(tmp_path):
    p = sx.ai.default_budget_config_path(tmp_path)
    assert str(p).endswith(".souxmar/budget.toml") or \
           str(p).endswith(".souxmar\\budget.toml")


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
