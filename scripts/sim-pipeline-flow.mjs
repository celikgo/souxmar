#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
//
// Headless simulator for the workbench's pipeline-editor panels.
// Inlines the pure regex/parser transforms used by MeshingPanel,
// SolversPanel, MaterialsPanel, BoundaryConditionsPanel,
// ManufacturingPanel and MarinePanel, then replays a representative
// "user clicks through the panels" sequence against the in-tree
// example pipelines.
//
// Treat divergences from the production code as a regression signal —
// the simulator is byte-for-byte the same algorithm as what runs in
// the browser, but lives in a separate file by necessity (Node cannot
// import the .tsx panel sources without a TypeScript+JSX runner).
//
// Usage:  node scripts/sim-pipeline-flow.mjs

import { readFileSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const REPO = dirname(dirname(fileURLToPath(import.meta.url)));

// ---------------------------------------------------------------------------
// MeshingPanel — replaceMesherPlugin.

function replaceMesherPlugin(yaml, newCap) {
  const lines = yaml.split("\n");
  for (let i = 0; i < lines.length; i++) {
    const m = /^(\s*plugin:\s*)(['"]?)mesher\.[\w.\-]+(['"]?)(\s*(#.*)?)$/.exec(lines[i]);
    if (m) {
      const q = m[2] || "", tail = m[4] || "";
      lines[i] = `${m[1]}${q}${newCap}${q}${tail}`;
      return lines.join("\n");
    }
  }
  return yaml;
}

// ---------------------------------------------------------------------------
// SolversPanel — replaceSolverPlugin + insertSolverStage.

function replaceSolverPlugin(yaml, newCap) {
  const lines = yaml.split("\n");
  for (let i = 0; i < lines.length; i++) {
    const m = /^(\s*plugin:\s*)(['"]?)solver\.[\w.\-]+(['"]?)(\s*(#.*)?)$/.exec(lines[i]);
    if (m) {
      const q = m[2] || "", tail = m[4] || "";
      lines[i] = `${m[1]}${q}${newCap}${q}${tail}`;
      return lines.join("\n");
    }
  }
  return insertSolverStage(lines, newCap).join("\n");
}

function insertSolverStage(lines, newCap) {
  let meshId = "mesh";
  for (let i = 0; i < lines.length; i++) {
    const m = /^\s*-\s*id\s*:\s*['"]?([\w\-]+)['"]?\s*(#.*)?$/.exec(lines[i]);
    if (!m) continue;
    for (let j = i + 1; j < lines.length && j < i + 6; j++) {
      const pm = /^\s*plugin\s*:\s*['"]?(mesher\.[\w.\-]+)['"]?/.exec(lines[j]);
      if (pm) { meshId = m[1]; break; }
      if (/^\s*-\s*id\s*:/.test(lines[j])) break;
    }
  }

  let writerIdx = -1;
  for (let i = 0; i < lines.length; i++) {
    if (!/^\s*-\s*id\s*:/.test(lines[i])) continue;
    for (let j = i + 1; j < lines.length && j < i + 6; j++) {
      if (/^\s*plugin\s*:\s*['"]?writer\.[\w.\-]+/.test(lines[j])) { writerIdx = i; break; }
      if (/^\s*-\s*id\s*:/.test(lines[j])) break;
    }
    if (writerIdx >= 0) break;
  }

  let stageIndent = "  ";
  for (const ln of lines) {
    const m = /^(\s*)-\s*id\s*:/.exec(ln);
    if (m) { stageIndent = m[1]; break; }
  }
  const childIndent = stageIndent + "  ";
  const grandchildIndent = childIndent + "  ";

  const stageBlock = [
    "",
    `${stageIndent}- id: solve`,
    `${childIndent}plugin: ${newCap}`,
    `${childIndent}input:`,
    `${grandchildIndent}mesh: { from: ${meshId} }`,
  ];

  if (writerIdx >= 0) {
    return [...lines.slice(0, writerIdx), ...stageBlock, "", ...lines.slice(writerIdx)];
  }
  return [...lines, ...stageBlock];
}

// ---------------------------------------------------------------------------
// MaterialsPanel — applyMaterial (subset of catalog).

const ALU_6061 = {
  id: "aluminum-6061-t6",
  name: "Aluminum 6061-T6",
  youngs_modulus: 69e9, poisson_ratio: 0.33, density: 2700,
  yield_strength: 276e6, thermal_conductivity: 167,
  specific_heat: 896, thermal_expansion: 23.6e-6,
};

const PROPERTY_KEYS = [
  "youngs_modulus", "poisson_ratio", "density", "yield_strength",
  "thermal_conductivity", "specific_heat", "thermal_expansion",
];

function applyMaterial(yaml, m) {
  const lines = yaml.split("\n");
  const inputIdx = findSolverInputLine(lines);
  if (inputIdx < 0) {
    return yaml + (yaml.endsWith("\n") ? "" : "\n") +
      `# TODO: no solver stage with an input: block found; cannot apply ${m.name}.\n`;
  }

  const inputIndent = leadingWs(lines[inputIdx]);
  let childIndent = null;
  for (let j = inputIdx + 1; j < lines.length; j++) {
    const ln = lines[j];
    if (ln.trim() === "" || ln.trim().startsWith("#")) continue;
    const ind = leadingWs(ln);
    if (ind.length <= inputIndent.length) break;
    childIndent = ind;
    break;
  }
  if (childIndent === null) childIndent = inputIndent + "  ";

  let blockEnd = lines.length;
  for (let j = inputIdx + 1; j < lines.length; j++) {
    const ln = lines[j];
    if (ln.trim() === "") continue;
    if (ln.trim().startsWith("#")) continue;
    const ind = leadingWs(ln);
    if (ind.length <= inputIndent.length) { blockEnd = j; break; }
  }

  const props = {
    youngs_modulus: m.youngs_modulus, poisson_ratio: m.poisson_ratio,
    density: m.density, yield_strength: m.yield_strength,
    thermal_conductivity: m.thermal_conductivity,
    specific_heat: m.specific_heat, thermal_expansion: m.thermal_expansion,
  };
  const present = new Set();
  for (let j = inputIdx + 1; j < blockEnd; j++) {
    const ln = lines[j];
    const km = /^(\s*)([A-Za-z_][\w\-]*)(\s*:\s*)([^#\n]*?)(\s*(#.*)?)$/.exec(ln);
    if (!km) continue;
    const key = km[2];
    if (key in props) {
      const indent = km[1], sep = km[3], tail = km[5] ?? "";
      lines[j] = `${indent}${key}${sep}${fmt(props[key])}${tail}`;
      present.add(key);
    }
  }

  const toAppend = [];
  toAppend.push(`${childIndent}# material: ${m.name}  (applied by Materials panel)`);
  for (const k of PROPERTY_KEYS) {
    if (present.has(k)) continue;
    toAppend.push(`${childIndent}${k}: ${fmt(props[k])}`);
  }
  lines.splice(blockEnd, 0, ...toAppend);
  return lines.join("\n");
}

function findSolverInputLine(lines) {
  for (let i = 0; i < lines.length; i++) {
    const m = /^(\s*)plugin\s*:\s*['"]?solver\.[\w.\-]+['"]?\s*(#.*)?$/.exec(lines[i]);
    if (!m) continue;
    const indent = m[1];
    for (let j = i + 1; j < lines.length; j++) {
      const ln = lines[j];
      if (ln.trim() === "") continue;
      const ind = leadingWs(ln);
      if (ind.length < indent.length) break;
      if (/^\s*-\s+id\s*:/.test(ln) && ind.length <= indent.length) break;
      if (/^\s*input\s*:/.test(ln)) return j;
    }
  }
  return -1;
}

function leadingWs(s) {
  const m = /^(\s*)/.exec(s);
  return m ? m[1] : "";
}

function fmt(v) {
  if (Number.isInteger(v) && Math.abs(v) < 1e7) return String(v);
  if (Math.abs(v) >= 1e4 || (v !== 0 && Math.abs(v) < 1e-3)) {
    return v.toExponential().replace(/e\+?/, "e");
  }
  return String(v);
}

// ---------------------------------------------------------------------------
// BoundaryConditionsPanel — writeLoadsToYaml + flowMap (subset of kinds).

function writeLoadsToYaml(yaml, bcs) {
  const lines = yaml.split("\n");
  const loadsIdx = findLoadsLine(lines);

  if (loadsIdx >= 0) {
    const baseIndent = leadingWs(lines[loadsIdx]);
    let blockEnd = lines.length;
    for (let i = loadsIdx + 1; i < lines.length; i++) {
      const ln = lines[i];
      if (ln.trim() === "" || ln.trim().startsWith("#")) continue;
      const ind = leadingWs(ln);
      if (ind.length <= baseIndent.length) { blockEnd = i; break; }
    }
    const childIndent = baseIndent + "  ";
    const block = [
      `${baseIndent}loads:`,
      ...(bcs.length === 0 ? [`${childIndent}# (no BCs)`] : bcs.map(b => `${childIndent}- ${flowMap(b)}`)),
    ];
    return [...lines.slice(0, loadsIdx), ...block, ...lines.slice(blockEnd)].join("\n");
  }

  const inputIdx = findSolverInputLine(lines);
  if (inputIdx < 0) return yaml;
  const inputIndent = leadingWs(lines[inputIdx]);
  const childIndent = inputIndent + "  ";
  let blockEnd = lines.length;
  for (let i = inputIdx + 1; i < lines.length; i++) {
    const ln = lines[i];
    if (ln.trim() === "" || ln.trim().startsWith("#")) continue;
    const ind = leadingWs(ln);
    if (ind.length <= inputIndent.length) { blockEnd = i; break; }
  }
  const block = [
    `${childIndent}loads:`,
    ...bcs.map(b => `${childIndent}  - ${flowMap(b)}`),
  ];
  return [...lines.slice(0, blockEnd), ...block, ...lines.slice(blockEnd)].join("\n");
}

function findLoadsLine(lines) {
  for (let i = 0; i < lines.length; i++) {
    if (/^\s+loads\s*:\s*(#.*)?$/.test(lines[i])) return i;
  }
  return -1;
}

function flowMap(b) {
  const parts = [];
  if ("face" in b) parts.push(`face: '${b.face}'`);
  parts.push(`kind: ${b.kind}`);
  switch (b.kind) {
    case "force": case "displacement": case "gravity":
      parts.push(`vector: [${b.vector.join(", ")}]`);
      break;
    case "pressure":
      parts.push(`magnitude: ${b.magnitude}`);
      break;
    case "thermal.temperature": case "thermal.flux":
      parts.push(`value: ${b.value}`);
      break;
    case "thermal.convection":
      parts.push(`h: ${b.h}`);
      parts.push(`t_ambient: ${b.t_ambient}`);
      break;
  }
  return `{${parts.join(", ")}}`;
}

// ---------------------------------------------------------------------------
// YamlViewer — insertPipelineStages + the upstream-handle resolvers that
// ManufacturingPanel and MarinePanel share. Whole-stage insertion, not a
// plugin-line swap: analysis stages land before the first writer stage,
// writer stages land at the end, ids are deduped, and the `$MESH` /
// `$STAGE<n>` tokens are substituted after id resolution.

function findLastStageIdMatching(lines, pluginRe) {
  let found = null;
  for (let i = 0; i < lines.length; i++) {
    const m = /^\s*-\s*id\s*:\s*['"]?([\w\-]+)['"]?\s*(#.*)?$/.exec(lines[i]);
    if (!m) continue;
    for (let j = i + 1; j < lines.length && j < i + 6; j++) {
      if (pluginRe.test(lines[j])) { found = m[1]; break; }
      if (/^\s*-\s*id\s*:/.test(lines[j])) break;
    }
  }
  return found;
}

function findMeshStageIdIn(lines) {
  return (
    findLastStageIdMatching(lines, /^\s*plugin\s*:\s*['"]?mesher\./) ??
    findLastStageIdMatching(lines, /^\s*plugin\s*:\s*['"]?reader\./)
  );
}

function findMeshStageId(yaml) {
  return findMeshStageIdIn(yaml.split("\n"));
}

function findFieldStageId(yaml) {
  return findLastStageIdMatching(yaml.split("\n"), /^\s*plugin\s*:\s*['"]?(?:solver|postproc)\./);
}

function hasMeshStage(yaml) {
  return findMeshStageId(yaml) !== null;
}

function findFirstWriterStageIdx(lines) {
  for (let i = 0; i < lines.length; i++) {
    if (!/^\s*-\s*id\s*:/.test(lines[i])) continue;
    for (let j = i + 1; j < lines.length && j < i + 6; j++) {
      if (/^\s*plugin\s*:\s*['"]?writer\.[\w.\-]+/.test(lines[j])) return i;
      if (/^\s*-\s*id\s*:/.test(lines[j])) break;
    }
  }
  return -1;
}

function collectStageIds(lines) {
  const out = new Set();
  for (const ln of lines) {
    const m = /^\s*-\s*id\s*:\s*['"]?([\w\-]+)['"]?\s*(#.*)?$/.exec(ln);
    if (m) out.add(m[1]);
  }
  return out;
}

function uniqueStageId(preferred, taken) {
  if (!taken.has(preferred)) return preferred;
  for (let n = 2; n < 1000; n++) {
    const candidate = `${preferred}-${n}`;
    if (!taken.has(candidate)) return candidate;
  }
  return `${preferred}-x`;
}

function discoverStageIndent(lines) {
  for (const ln of lines) {
    const m = /^(\s*)-\s*id\s*:/.exec(ln);
    if (m) return m[1];
  }
  return "  ";
}

function insertPipelineStages(yaml, specs) {
  if (specs.length === 0) return yaml;
  const lines = yaml.split("\n");
  const meshId = findMeshStageIdIn(lines);
  if (meshId === null) {
    return yaml + (yaml.endsWith("\n") ? "" : "\n") +
      "# TODO: no mesh-producing stage (mesher.* / reader.*) found; cannot wire " +
      `${specs.map(s => s.plugin).join(", ")}.\n`;
  }

  const taken = collectStageIds(lines);
  const ids = specs.map(s => {
    const id = uniqueStageId(s.id, taken);
    taken.add(id);
    return id;
  });

  const stageIndent = discoverStageIndent(lines);
  const childIndent = stageIndent + "  ";
  const grandchildIndent = childIndent + "  ";

  const resolve = value =>
    value
      .replace(/\$MESH\b/g, meshId)
      .replace(/\$STAGE(\d+)\b/g, (whole, n) => ids[Number(n)] ?? whole);

  const analysis = [];
  const outputs = [];
  specs.forEach((spec, i) => {
    const block = [
      `${stageIndent}- id: ${ids[i]}${spec.note ? `  # ${spec.note}` : ""}`,
      `${childIndent}plugin: ${spec.plugin}`,
    ];
    if (spec.input.length > 0) {
      block.push(`${childIndent}input:`);
      for (const { key, value } of spec.input) {
        block.push(`${grandchildIndent}${key}: ${resolve(value)}`);
      }
    }
    const sink = spec.plugin.startsWith("writer.") ? outputs : analysis;
    sink.push("", ...block);
  });

  const writerIdx = findFirstWriterStageIdx(lines);
  if (writerIdx < 0) {
    return [...lines, ...analysis, ...outputs].join("\n");
  }
  return [
    ...lines.slice(0, writerIdx),
    ...analysis,
    ...(analysis.length > 0 ? [""] : []),
    ...lines.slice(writerIdx),
    ...outputs,
  ].join("\n");
}

// ---------------------------------------------------------------------------
// ManufacturingPanel — stage builders (contract §3.3–§3.14). Two of the
// panel's curated materials are enough to exercise both families.

const AM_316L = {
  id: "316L", family: "metal",
  youngs_modulus: 1.9e11, poisson_ratio: 0.28, density: 7990,
  yield_strength: 5.0e8, thermal_conductivity: 15.0, specific_heat: 500,
  cte: 1.6e-5, transition_c: 1400,
};

const AM_PEKK = {
  id: "PEKK", family: "polymer",
  youngs_modulus: 3.9e9, poisson_ratio: 0.38, density: 1300,
  yield_strength: 1.0e8, thermal_conductivity: 0.25, specific_heat: 1700,
  cte: 4.7e-5, transition_c: 162,
  nozzle_c: 400, bed_c: 130, chamber_c: 90,
};

const METAL_DEFAULTS = {
  laser_power: 200, scan_speed: 0.8, hatch_spacing: 1.1e-4,
  layer_height: 0.001, deposition_rate: 3.0,
};

const POLYMER_DEFAULTS = {
  nozzle_temperature: 250, bed_temperature: 100, chamber_temperature: 40,
  layer_time: 20, layer_height: 2.0e-4, road_width: 4.0e-4,
};

// ManufacturingPanel.pickMaterial seeds the extrusion set-points from
// the polymer's recommended window.
function seedPolymer(params, m) {
  return {
    ...params,
    nozzle_temperature:  m.nozzle_c ?? params.nozzle_temperature,
    bed_temperature:     m.bed_c ?? params.bed_temperature,
    chamber_temperature: m.chamber_c ?? params.chamber_temperature,
  };
}

// ManufacturingPanel / MarinePanel `num()` — as MaterialsPanel's fmt()
// above, but the mantissa always keeps a decimal point so `2.0e-4` is a
// float to every YAML parser (a bare `2e-4` is a string under a strict
// YAML 1.1 resolver) and matches the contract's own notation.
function amNum(v) {
  if (Number.isInteger(v) && Math.abs(v) < 1e7) return String(v);
  if (Math.abs(v) >= 1e4 || (v !== 0 && Math.abs(v) < 1e-3)) {
    const [mantissa, exponent] = v.toExponential().replace(/e\+?/, "e").split("e");
    return `${mantissa.includes(".") ? mantissa : `${mantissa}.0`}e${exponent}`;
  }
  return String(v);
}

function vec3(v) {
  return `[${v[0]}, ${v[1]}, ${v[2]}]`;
}

function numOr(v, fallback) {
  return v !== undefined && Number.isFinite(v) ? v : fallback;
}

function thermalStages(ctx) {
  const m = ctx.material, p = ctx.params;
  const shared = [
    ["laser_power",          numOr(p.laser_power, 200)],
    ["scan_speed",           numOr(p.scan_speed, 0.8)],
    ["hatch_spacing",        numOr(p.hatch_spacing, 1.1e-4)],
    ["layer_height",         numOr(p.layer_height, 0.001)],
    ["process_layer_height", 3.0e-5],
    ["absorptivity",         0.35],
    ["preheat_temperature",  80],
    ["thermal_conductivity", m.thermal_conductivity],
    ["density",              m.density],
    ["specific_heat",        m.specific_heat],
    ["melt_temperature",     m.transition_c],
  ];
  return [
    {
      id: "am-thermal", plugin: "solver.am.thermal.lpbf",
      note: `${ctx.process} thermal history — ${m.id}`,
      input: [
        { key: "mesh", value: "{ from: $MESH }" },
        ...shared.map(([k, v]) => ({ key: k, value: amNum(v) })),
        { key: "baseplate_temperature", value: "80" },
        { key: "interlayer_time",       value: "10.0" },
        { key: "build_direction",       value: vec3(ctx.build_direction) },
        { key: "baseplate_layers",      value: "0" },
        { key: "num_layers",            value: "0" },
      ],
    },
    {
      id: "am-melt-pool", plugin: "postproc.am.melt_pool",
      note: "melt-pool depth / normalised enthalpy / porosity risk",
      input: [
        { key: "mesh",  value: "{ from: $MESH }" },
        { key: "field", value: "{ from: $STAGE0 }" },
        ...shared.map(([k, v]) => ({ key: k, value: amNum(v) })),
      ],
    },
  ];
}

function distortionStages(ctx) {
  const m = ctx.material, p = ctx.params;
  return [
    {
      id: "am-distortion", plugin: "solver.am.distortion.inherent_strain",
      note: "inherent strain — strain_calibration MUST be fitted to a measured part",
      input: [
        { key: "mesh",                value: "{ from: $MESH }" },
        { key: "youngs_modulus",      value: amNum(m.youngs_modulus) },
        { key: "poisson_ratio",       value: amNum(m.poisson_ratio) },
        { key: "cte",                 value: amNum(m.cte) },
        { key: "melt_temperature",    value: amNum(m.transition_c) },
        { key: "preheat_temperature", value: "80" },
        { key: "inherent_strain",     value: "0.0" },
        { key: "strain_calibration",  value: "0.30" },
        { key: "layer_height",        value: amNum(numOr(p.layer_height, 0.001)) },
        { key: "substrate_thickness", value: "0.0" },
        { key: "build_direction",     value: vec3(ctx.build_direction) },
        { key: "baseplate_layers",    value: "0" },
        { key: "baseplate_clamped",   value: "true" },
        { key: "num_layers",          value: "0" },
      ],
    },
    {
      id: "am-residual-stress", plugin: "postproc.am.residual_stress",
      note: "von-Mises-equivalent residual stress",
      input: [
        { key: "mesh",           value: "{ from: $MESH }" },
        { key: "field",          value: "{ from: $STAGE0 }" },
        { key: "yield_strength", value: amNum(m.yield_strength) },
        { key: "youngs_modulus", value: amNum(m.youngs_modulus) },
        { key: "poisson_ratio",  value: amNum(m.poisson_ratio) },
        { key: "cte",            value: amNum(m.cte) },
      ],
    },
  ];
}

function manufacturabilityStages(ctx) {
  const m = ctx.material, p = ctx.params;
  const layerHeight = numOr(
    p.layer_height,
    ctx.process === "fff" || ctx.process === "sls" ? 2.0e-4 : 0.001,
  );
  return [
    {
      id: "am-overhang", plugin: "solver.am.overhang",
      note: "downskin tilt / support need per cell",
      input: [
        { key: "mesh",                   value: "{ from: $MESH }" },
        { key: "build_direction",        value: vec3(ctx.build_direction) },
        { key: "overhang_threshold_deg", value: "45" },
        { key: "layer_height",           value: amNum(layerHeight) },
      ],
    },
    {
      id: "am-printability", plugin: "solver.am.printability",
      note: "documented DfAM heuristic, not a guarantee",
      input: [
        { key: "mesh",                   value: "{ from: $MESH }" },
        { key: "build_direction",        value: vec3(ctx.build_direction) },
        { key: "overhang_threshold_deg", value: "45" },
        { key: "min_wall_thickness",     value: "5.0e-4" },
        { key: "machine_build_volume",   value: "[0.25, 0.25, 0.3]" },
        { key: "corrosion_allowance",    value: "0.0" },
        { key: "layer_height",           value: amNum(layerHeight) },
      ],
    },
    {
      id: "am-buildtime", plugin: "solver.am.buildtime",
      note: "per-layer time / energy / mass / cost",
      input: [
        { key: "mesh",                   value: "{ from: $MESH }" },
        { key: "process",                value: `'${ctx.process}'` },
        { key: "layer_height",           value: amNum(layerHeight) },
        { key: "process_layer_height",   value: "3.0e-5" },
        { key: "scan_speed",             value: amNum(numOr(p.scan_speed, 0.8)) },
        { key: "hatch_spacing",          value: amNum(numOr(p.hatch_spacing, 1.1e-4)) },
        { key: "recoat_time",            value: "8.0" },
        { key: "print_speed",            value: "0.05" },
        { key: "road_width",             value: amNum(numOr(p.road_width, 4.0e-4)) },
        { key: "deposition_rate",        value: amNum(numOr(p.deposition_rate, 3.0)) },
        { key: "laser_power",            value: amNum(numOr(p.laser_power, 200)) },
        { key: "machine_power_overhead", value: "2500" },
        { key: "material_cost_per_kg",   value: "90" },
        { key: "machine_rate_per_hour",  value: "45" },
        { key: "density",                value: amNum(m.density) },
        { key: "build_direction",        value: vec3(ctx.build_direction) },
      ],
    },
  ];
}

function reportStages(ctx) {
  const m = ctx.material;
  return [
    {
      id: "am-report", plugin: "writer.am.report",
      note: "Markdown build report / traveller sheet",
      input: [
        { key: "mesh", value: "{ from: $MESH }" },
        ...(ctx.fieldStageId ? [{ key: "field", value: `{ from: ${ctx.fieldStageId} }` }] : []),
        { key: "path",                   value: "am-build-report.md" },
        { key: "title",                  value: "'souxmar AM build report'" },
        { key: "process",                value: `'${ctx.process}'` },
        { key: "material",               value: `'${m.id}'` },
        { key: "design_notes",           value: "''" },
        { key: "material_cost_per_kg",   value: "90" },
        { key: "machine_rate_per_hour",  value: "45" },
        { key: "machine_power_overhead", value: "2500" },
        { key: "density",                value: amNum(m.density) },
      ],
    },
  ];
}

function polymerStages(ctx) {
  const m = ctx.material, p = ctx.params;
  const layerTime = numOr(p.layer_time, 20);
  return [
    {
      id: "am-fff", plugin: "solver.am.polymer.fff",
      note: ctx.process === "sls"
        ? `interlayer thermal cycling — ${m.id}; FFF model applied to an SLS build, indicative only`
        : `interlayer thermal cycling — ${m.id}`,
      input: [
        { key: "mesh",                         value: "{ from: $MESH }" },
        { key: "nozzle_temperature",           value: amNum(numOr(p.nozzle_temperature, 250)) },
        { key: "bed_temperature",              value: amNum(numOr(p.bed_temperature, 100)) },
        { key: "chamber_temperature",          value: amNum(numOr(p.chamber_temperature, 40)) },
        { key: "layer_time",                   value: amNum(layerTime) },
        { key: "layer_height",                 value: amNum(numOr(p.layer_height, 2.0e-4)) },
        { key: "road_width",                   value: amNum(numOr(p.road_width, 4.0e-4)) },
        { key: "convection_coefficient",       value: "30" },
        { key: "density",                      value: amNum(m.density) },
        { key: "specific_heat",                value: amNum(m.specific_heat) },
        { key: "thermal_conductivity",         value: amNum(m.thermal_conductivity) },
        { key: "glass_transition_temperature", value: amNum(m.transition_c) },
        { key: "build_direction",              value: vec3(ctx.build_direction) },
        { key: "baseplate_layers",             value: "0" },
        { key: "num_layers",                   value: "0" },
      ],
    },
    {
      id: "am-bond-strength", plugin: "postproc.am.bond_strength",
      note: "reptation healing → degree of healing / Z-strength fraction",
      input: [
        { key: "mesh",                            value: "{ from: $MESH }" },
        { key: "field",                           value: "{ from: $STAGE0 }" },
        { key: "glass_transition_temperature",    value: amNum(m.transition_c) },
        { key: "reptation_time_reference",        value: "2.0" },
        { key: "reptation_reference_temperature", value: "260" },
        { key: "activation_energy",               value: "8.0e4" },
        { key: "layer_time",                      value: amNum(layerTime) },
      ],
    },
  ];
}

function gcodeStages(ctx) {
  const p = ctx.params;
  return [
    {
      id: "am-gcode", plugin: "writer.am.gcode",
      note: "single perimeter + alternating infill; no supports, no bridging",
      input: [
        { key: "mesh",               value: "{ from: $MESH }" },
        { key: "path",               value: "am-part.gcode" },
        { key: "layer_height",       value: amNum(numOr(p.layer_height, 2.0e-4)) },
        { key: "road_width",         value: amNum(numOr(p.road_width, 4.0e-4)) },
        { key: "filament_diameter",  value: "1.75e-3" },
        { key: "nozzle_temperature", value: amNum(numOr(p.nozzle_temperature, 250)) },
        { key: "bed_temperature",    value: amNum(numOr(p.bed_temperature, 100)) },
        { key: "print_speed",        value: "0.05" },
        { key: "travel_speed",       value: "0.15" },
        { key: "infill_spacing",     value: "0.002" },
        { key: "infill_angle_deg",   value: "45" },
        { key: "retract_length",     value: "0.001" },
        { key: "flow_multiplier",    value: "1.0" },
        { key: "build_direction",    value: vec3(ctx.build_direction) },
        { key: "max_layers",         value: "5000" },
      ],
    },
  ];
}

// ---------------------------------------------------------------------------
// MarinePanel — stage builders (contract §3.15–§3.18) + the one-atmosphere
// EOS-80 seawater density the panel displays and writes.

const MARINE_ALLOYS = {
  "316L": { id: "316L", youngs_modulus: 1.9e11, poisson_ratio: 0.28, yield_strength: 5.0e8, density: 7990, chromium_pct: 17.0, molybdenum_pct: 2.5, nitrogen_pct: 0.05 },
  "2507": { id: "2507", youngs_modulus: 2.0e11, poisson_ratio: 0.27, yield_strength: 5.5e8, density: 7800, chromium_pct: 25.0, molybdenum_pct: 3.8, nitrogen_pct: 0.27 },
  "NAB":  { id: "NAB",  youngs_modulus: 1.20e11, poisson_ratio: 0.33, yield_strength: 2.5e8, density: 7600, chromium_pct: 0.0, molybdenum_pct: 0.0, nitrogen_pct: 0.0 },
};

function pren(a) {
  return a.chromium_pct + 3.3 * a.molybdenum_pct + 16 * a.nitrogen_pct;
}

function clamp(v, lo, hi) {
  if (!Number.isFinite(v)) return lo;
  return v < lo ? lo : v > hi ? hi : v;
}

function seawaterDensity(salinityPsu, temperatureC) {
  const t = clamp(temperatureC, 0, 40);
  const s = clamp(salinityPsu, 0, 42);
  const rhoW =
    999.842594 +
    6.793952e-2 * t -
    9.095290e-3 * t * t +
    1.001685e-4 * t * t * t -
    1.120083e-6 * t * t * t * t +
    6.536332e-9 * t * t * t * t * t;
  const A =
    0.824493 -
    4.0899e-3 * t +
    7.6438e-5 * t * t -
    8.2467e-7 * t * t * t +
    5.3875e-9 * t * t * t * t;
  const B = -5.72466e-3 + 1.0227e-4 * t - 1.6546e-6 * t * t;
  const C = 4.8314e-4;
  return rhoW + A * s + B * Math.pow(s, 1.5) + C * s * s;
}

function round1(v) {
  return amNum(Math.round(v * 10) / 10);
}

function hydrostaticStages(ctx) {
  return [
    {
      id: "marine-hydrostatic", plugin: "solver.marine.hydrostatic",
      note: "one load case per depth factor: operating / test / collapse",
      input: [
        { key: "mesh",                 value: "{ from: $MESH }" },
        { key: "design_depth",         value: amNum(ctx.designDepth) },
        { key: "depth_factors",        value: `[${ctx.depthFactors.map(fmt).join(", ")}]` },
        { key: "seawater_density",     value: round1(ctx.seawaterDensity) },
        { key: "salinity_psu",         value: amNum(ctx.salinity) },
        { key: "seawater_temperature", value: amNum(ctx.seaTemp) },
        { key: "gravity",              value: "9.80665" },
        { key: "include_atmospheric",  value: "false" },
        { key: "atmospheric_pressure", value: "101325" },
        { key: "build_direction",      value: "[0, 0, 1]" },
      ],
    },
  ];
}

function collapseStages(ctx) {
  const a = ctx.alloy;
  return [
    {
      id: "marine-collapse", plugin: "solver.marine.hull_collapse",
      note: "PRELIMINARY sizing — not a classification-society calculation",
      input: [
        { key: "mesh",                    value: "{ from: $MESH }" },
        { key: "hull_type",               value: `'${ctx.hullType}'` },
        { key: "diameter",                value: amNum(ctx.diameter) },
        { key: "thickness",               value: amNum(ctx.thickness) },
        { key: "unsupported_length",      value: amNum(ctx.frameSpacing) },
        { key: "youngs_modulus",          value: amNum(a.youngs_modulus) },
        { key: "poisson_ratio",           value: amNum(a.poisson_ratio) },
        { key: "yield_strength",          value: amNum(a.yield_strength) },
        { key: "design_depth",            value: amNum(ctx.designDepth) },
        { key: "seawater_density",        value: round1(ctx.seawaterDensity) },
        { key: "gravity",                 value: "9.80665" },
        { key: "imperfection_knockdown",  value: "0.75" },
        { key: "am_anisotropy_knockdown", value: "0.90" },
        { key: "safety_factor",           value: "1.5" },
      ],
    },
  ];
}

function corrosionStages(ctx) {
  const a = ctx.alloy;
  return [
    {
      id: "marine-corrosion", plugin: "solver.marine.corrosion",
      note: "indicative literature rates, not design values",
      input: [
        { key: "mesh",                    value: "{ from: $MESH }" },
        { key: "alloy",                   value: `'${a.id}'` },
        { key: "chromium_pct",            value: amNum(a.chromium_pct) },
        { key: "molybdenum_pct",          value: amNum(a.molybdenum_pct) },
        { key: "nitrogen_pct",            value: amNum(a.nitrogen_pct) },
        { key: "mating_alloy",            value: `'${ctx.mateId}'` },
        { key: "area_ratio_cathode_anode", value: "1.0" },
        { key: "seawater_temperature",    value: amNum(ctx.seaTemp) },
        { key: "salinity_psu",            value: amNum(ctx.salinity) },
        { key: "flow_velocity",           value: "0.5" },
        { key: "oxygen_mg_per_l",         value: "8.0" },
        { key: "service_life_years",      value: amNum(ctx.serviceLife) },
        { key: "cathodic_protection",     value: ctx.cathodic ? "true" : "false" },
        { key: "coating_efficiency",      value: "0.0" },
        { key: "as_built_surface",        value: "true" },
      ],
    },
  ];
}

function qualificationStages(ctx) {
  return [
    {
      id: "marine-qualification", plugin: "writer.marine.qualification_report",
      note: "ADVISORY dossier — souxmar is not a classification society",
      input: [
        { key: "mesh", value: "{ from: $MESH }" },
        ...(ctx.fieldStageId ? [{ key: "field", value: `{ from: ${ctx.fieldStageId} }` }] : []),
        { key: "path",               value: "marine-qualification.md" },
        { key: "part_name",          value: "'AM part'" },
        { key: "process",            value: "'lpbf'" },
        { key: "alloy",              value: `'${ctx.alloy.id}'` },
        { key: "application",        value: "'structural'" },
        { key: "criticality",        value: "2" },
        { key: "design_depth",       value: amNum(ctx.designDepth) },
        { key: "service_life_years", value: amNum(ctx.serviceLife) },
        { key: "class_framework",    value: "'generic'" },
        { key: "redundancy",         value: "false" },
      ],
    },
  ];
}

// ---------------------------------------------------------------------------
// Assertions + reporter.

let pass = 0, fail = 0;
function check(name, cond, detail) {
  if (cond) { pass++; console.log(`  ✓ ${name}`); }
  else { fail++; console.log(`  ✗ ${name}`); if (detail) console.log(`      ${detail}`); }
}

function header(s) {
  console.log("\n" + "=".repeat(72));
  console.log(s);
  console.log("=".repeat(72));
}

// ---------------------------------------------------------------------------
// Scenarios.

header("Scenario 1 — cantilever-beam: mesh → solve → material → BC");

let yaml = readFileSync(join(REPO, "examples/cantilever-beam/pipeline.yaml"), "utf8");
console.log("\n[step 0] initial YAML — stages:");
console.log(yaml.split("\n").filter(l => /^\s*(-\s*id|plugin):/.test(l)).join("\n"));

console.log("\n[step 1] click Mesher chip: tetra/gmsh");
yaml = replaceMesherPlugin(yaml, "mesher.tetra.gmsh");
check("mesher plugin swapped",
  yaml.includes("plugin: mesher.tetra.gmsh"),
  yaml);
check("no stale mesher.tetra.hello",
  !yaml.includes("mesher.tetra.hello"));

console.log("\n[step 2] click Solvers chip: elasticity.linear");
yaml = replaceSolverPlugin(yaml, "solver.elasticity.linear");
check("solve stage inserted (id)",
  /- id: solve\b/.test(yaml));
check("solve stage has the right plugin",
  yaml.includes("plugin: solver.elasticity.linear"));
check("solve stage has input: with mesh-from-mesh",
  /plugin: solver\.elasticity\.linear[\s\S]*?input:[\s\S]*?mesh:\s*\{\s*from:\s*mesh\s*\}/.test(yaml));
check("solve appears BEFORE write (DAG order preserved)",
  yaml.indexOf("- id: solve") < yaml.indexOf("- id: write"));

console.log("\n[step 3] click Materials chip: Aluminum 6061-T6");
yaml = applyMaterial(yaml, ALU_6061);
const solveBlock = yaml.split("- id: write")[0].split("- id: solve")[1] ?? "";
check("solve block contains youngs_modulus 6.9e10",
  /youngs_modulus:\s*6\.9e10/.test(solveBlock),
  solveBlock);
check("solve block contains density 2700",
  /density:\s*2700/.test(solveBlock));
check("solve block contains poisson_ratio 0.33",
  /poisson_ratio:\s*0\.33/.test(solveBlock));
check("solve block contains thermal_expansion (small exponent)",
  /thermal_expansion:\s*2\.36e-5/.test(solveBlock));
check("# material: comment marker present",
  /# material: Aluminum 6061-T6/.test(solveBlock));

console.log("\n[step 4] click BC + Add: Force on +y default");
yaml = writeLoadsToYaml(yaml, [{ face: "+y", kind: "force", vector: [0, -1000, 0] }]);
const solveBlock2 = yaml.split("- id: write")[0].split("- id: solve")[1] ?? "";
check("loads: block inserted in solve stage",
  /loads:\s*\n\s*-\s*\{[^}]*face:\s*'\+y'[^}]*kind:\s*force/.test(solveBlock2),
  solveBlock2);

console.log("\n[step 5] add three more BCs — gravity, pressure, convection");
yaml = writeLoadsToYaml(yaml, [
  { face: "+y", kind: "force",                 vector: [0, -1000, 0] },
  { kind:    "gravity",                        vector: [0, -9.81, 0] },
  { face: "+x", kind: "pressure",              magnitude: 1.0e5 },
  { face: "-x", kind: "thermal.convection",    h: 25, t_ambient: 293.15 },
]);
check("loads block has 4 rows",
  (yaml.match(/^\s*-\s*\{[^}]*kind:/gm) || []).length >= 4);
check("gravity row written without face",
  /-\s*\{kind:\s*gravity,\s*vector:\s*\[0,\s*-9\.81,\s*0\]\}/.test(yaml));
check("pressure row carries magnitude",
  /\bpressure[^}]*magnitude:\s*100000/.test(yaml));
check("convection row carries h + t_ambient",
  /thermal\.convection[^}]*h:\s*25[^}]*t_ambient:\s*293\.15/.test(yaml));

header("Scenario 2 — modal-beam: replace existing solver in-place");

yaml = readFileSync(join(REPO, "examples/modal-beam/pipeline.yaml"), "utf8");
console.log("[step 1] click Solvers chip: heat.linear (swap modal → heat)");
yaml = replaceSolverPlugin(yaml, "solver.heat.linear");
check("solver swapped to heat.linear",
  yaml.includes("plugin: solver.heat.linear"));
check("no stale solver.modal.linear",
  !yaml.includes("solver.modal.linear"));
check("no new solve stage was inserted",
  yaml.split("- id: solve").length === 1);

header("Scenario 3 — thermal-fin: apply material onto existing solver");

yaml = readFileSync(join(REPO, "examples/thermal-fin/pipeline.yaml"), "utf8");
console.log("[step 1] click Materials chip: Aluminum 6061-T6");
yaml = applyMaterial(yaml, ALU_6061);
check("youngs_modulus appended to heat stage input",
  /plugin: solver\.heat\.linear[\s\S]*?youngs_modulus:\s*6\.9e10/.test(yaml));
check("# material comment marker present",
  /# material: Aluminum 6061-T6/.test(yaml));

header("Scenario 4 — cantilever-beam: LPBF 316L manufacturing chain");

yaml = readFileSync(join(REPO, "examples/cantilever-beam/pipeline.yaml"), "utf8");
const lpbf = {
  process: "lpbf",
  material: AM_316L,
  params: { ...METAL_DEFAULTS },
  build_direction: [0, 0, 1],
  fieldStageId: null,
};

check("panel gate open — cantilever-beam has a mesh-producing stage",
  hasMeshStage(yaml));
check("upstream mesh id resolved from the document",
  findMeshStageId(yaml) === "mesh",
  String(findMeshStageId(yaml)));

console.log("\n[step 1] click Manufacturing → Add thermal analysis");
yaml = insertPipelineStages(yaml, thermalStages(lpbf));
check("solver.am.thermal.lpbf stage inserted",
  /- id: am-thermal\b[\s\S]*?plugin: solver\.am\.thermal\.lpbf/.test(yaml),
  yaml);
check("thermal stage wired mesh: { from: mesh }",
  /- id: am-thermal\b[\s\S]*?mesh: \{ from: mesh \}/.test(yaml));
check("melt-pool postproc consumes the thermal stage's field",
  /- id: am-melt-pool\b[\s\S]*?field: \{ from: am-thermal \}/.test(yaml));
check("melt-pool postproc also carries the required mesh handle",
  /- id: am-melt-pool\b[\s\S]*?mesh: \{ from: mesh \}[\s\S]*?field: \{ from: am-thermal \}/.test(yaml));
check("contract defaults written verbatim (laser_power 200, hatch 1.1e-4)",
  /laser_power: 200/.test(yaml) && /hatch_spacing: 1\.1e-4/.test(yaml));
check("316L thermo-physical numbers written (ρ 7990, k 15, T_melt 1400)",
  /density: 7990/.test(yaml) && /thermal_conductivity: 15/.test(yaml) && /melt_temperature: 1400/.test(yaml));
check("build_direction defaults to +Z",
  /build_direction: \[0, 0, 1\]/.test(yaml));
check("both AM stages land BEFORE the existing writer stage",
  yaml.indexOf("- id: am-thermal") < yaml.indexOf("- id: write") &&
  yaml.indexOf("- id: am-melt-pool") < yaml.indexOf("- id: write"));

console.log("\n[step 2] click Add distortion analysis");
yaml = insertPipelineStages(yaml, distortionStages(lpbf));
check("inherent-strain solver + residual-stress postproc inserted",
  /plugin: solver\.am\.distortion\.inherent_strain/.test(yaml) &&
  /plugin: postproc\.am\.residual_stress/.test(yaml));
check("residual stress consumes the distortion field",
  /- id: am-residual-stress\b[\s\S]*?field: \{ from: am-distortion \}/.test(yaml));
check("strain_calibration written with its calibrate-me note",
  /strain_calibration: 0\.3/.test(yaml) &&
  /strain_calibration MUST be fitted to a measured part/.test(yaml));
check("baseplate_clamped emitted as a YAML bool",
  /baseplate_clamped: true/.test(yaml));

console.log("\n[step 3] click Add manufacturability checks");
yaml = insertPipelineStages(yaml, manufacturabilityStages(lpbf));
check("three DfAM solver stages inserted",
  /plugin: solver\.am\.overhang/.test(yaml) &&
  /plugin: solver\.am\.printability/.test(yaml) &&
  /plugin: solver\.am\.buildtime/.test(yaml));
check("buildtime carries the process discriminator",
  /process: 'lpbf'/.test(yaml));
check("machine_build_volume emitted as a flow list",
  /machine_build_volume: \[0\.25, 0\.25, 0\.3\]/.test(yaml));

console.log("\n[step 4] click Add build report");
lpbf.fieldStageId = findFieldStageId(yaml);
check("last field producer resolved for the report's field handle",
  lpbf.fieldStageId === "am-buildtime",
  String(lpbf.fieldStageId));
yaml = insertPipelineStages(yaml, reportStages(lpbf));
check("writer.am.report inserted",
  /plugin: writer\.am\.report/.test(yaml));
check("report wired to the last field producer",
  /- id: am-report\b[\s\S]*?field: \{ from: am-buildtime \}/.test(yaml));
check("writer stage appended AFTER the pre-existing writer (analysis first)",
  yaml.indexOf("- id: am-report") > yaml.indexOf("- id: write"));
check("no duplicate stage ids in the finished document",
  (() => {
    const ids = (yaml.match(/^\s*-\s*id\s*:\s*[\w\-]+/gm) || [])
      .map(s => s.replace(/^\s*-\s*id\s*:\s*/, ""));
    return new Set(ids).size === ids.length;
  })(),
  (yaml.match(/^\s*-\s*id\s*:\s*[\w\-]+/gm) || []).join(" | "));

header("Scenario 5 — repeat click dedupes ids and re-points the postproc");

yaml = insertPipelineStages(yaml, thermalStages(lpbf));
check("second thermal chain got deduped ids",
  /- id: am-thermal-2\b/.test(yaml) && /- id: am-melt-pool-2\b/.test(yaml),
  (yaml.match(/^\s*-\s*id\s*:\s*[\w\-]+/gm) || []).join(" | "));
check("$STAGE0 resolved to the DEDUPED id, not the original",
  /- id: am-melt-pool-2\b[\s\S]*?field: \{ from: am-thermal-2 \}/.test(yaml));
check("the first chain is untouched",
  /- id: am-melt-pool\n[\s\S]*?field: \{ from: am-thermal \}/.test(
    yaml.replace(/- id: am-melt-pool  #[^\n]*\n/, "- id: am-melt-pool\n")));

header("Scenario 6 — thermal-fin: FFF/PEKK polymer chain + G-code");

yaml = readFileSync(join(REPO, "examples/thermal-fin/pipeline.yaml"), "utf8");
const fff = {
  process: "fff",
  material: AM_PEKK,
  params: seedPolymer({ ...POLYMER_DEFAULTS }, AM_PEKK),
  build_direction: [0, 0, 1],
  fieldStageId: null,
};

console.log("[step 1] click Add FFF thermal + bond strength");
yaml = insertPipelineStages(yaml, polymerStages(fff));
check("solver.am.polymer.fff + postproc.am.bond_strength inserted",
  /plugin: solver\.am\.polymer\.fff/.test(yaml) &&
  /plugin: postproc\.am\.bond_strength/.test(yaml));
check("PEKK set-points seeded by the material pick (nozzle 400 / bed 130 / chamber 90)",
  /nozzle_temperature: 400/.test(yaml) &&
  /bed_temperature: 130/.test(yaml) &&
  /chamber_temperature: 90/.test(yaml));
check("polymer layer_height default is the §3.7 200 µm, not the metal 1 mm",
  /layer_height: 2\.0e-4/.test(yaml));
check("glass transition taken from the polymer (162 °C)",
  /glass_transition_temperature: 162/.test(yaml));
check("bond strength consumes the FFF interface_temperature field",
  /- id: am-bond-strength\b[\s\S]*?field: \{ from: am-fff \}/.test(yaml));
check("inserted before thermal-fin's existing writer stage",
  yaml.indexOf("- id: am-fff") < yaml.indexOf("- id: write"));
check("thermal-fin's own postproc stage survived",
  /- id: magnitude\b/.test(yaml) && /plugin: postproc\.scalar_magnitude/.test(yaml));

console.log("\n[step 2] click Add G-code output");
yaml = insertPipelineStages(yaml, gcodeStages(fff));
check("writer.am.gcode appended at the end",
  /plugin: writer\.am\.gcode/.test(yaml) &&
  yaml.indexOf("- id: am-gcode") > yaml.indexOf("- id: write"));
check("gcode writer carries its required path + mesh handle",
  /- id: am-gcode\b[\s\S]*?mesh: \{ from: mesh \}[\s\S]*?path: am-part\.gcode/.test(yaml));
check("filament diameter emitted in SI metres",
  /filament_diameter: 1\.75e-3/.test(yaml));

header("Scenario 7 — cantilever-beam: marine hull assessment");

yaml = readFileSync(join(REPO, "examples/cantilever-beam/pipeline.yaml"), "utf8");
const rho = seawaterDensity(35, 10);
check("EOS-80 seawater density at 35 PSU / 10 °C ≈ 1026.95 kg/m³",
  Math.abs(rho - 1026.95) < 0.05,
  String(rho));
check("PREN ranks 2507 above 316L",
  pren(MARINE_ALLOYS["2507"]) > pren(MARINE_ALLOYS["316L"]),
  `2507=${pren(MARINE_ALLOYS["2507"]).toFixed(1)} 316L=${pren(MARINE_ALLOYS["316L"]).toFixed(1)}`);

const marine = {
  designDepth: 300,
  depthFactors: [1.0, 1.5, 2.25],
  seaTemp: 10,
  salinity: 35,
  seawaterDensity: rho,
  serviceLife: 25,
  hullType: "ring_stiffened_cylinder",
  diameter: 1.0,
  thickness: 0.012,
  frameSpacing: 0.5,
  alloy: MARINE_ALLOYS["316L"],
  mateId: "NAB",
  cathodic: true,
  fieldStageId: null,
};

console.log("[step 1] click Add hydrostatic load case");
yaml = insertPipelineStages(yaml, hydrostaticStages(marine));
check("solver.marine.hydrostatic inserted before the writer",
  /plugin: solver\.marine\.hydrostatic/.test(yaml) &&
  yaml.indexOf("- id: marine-hydrostatic") < yaml.indexOf("- id: write"));
check("three depth factors → three load cases",
  /depth_factors: \[1, 1\.5, 2\.25\]/.test(yaml));
check("derived seawater density written (1027)",
  /seawater_density: 1027/.test(yaml));

console.log("\n[step 2] click Add collapse check");
yaml = insertPipelineStages(yaml, collapseStages(marine));
check("solver.marine.hull_collapse inserted with the hull form",
  /plugin: solver\.marine\.hull_collapse/.test(yaml) &&
  /hull_type: 'ring_stiffened_cylinder'/.test(yaml));
check("knockdowns + safety factor written verbatim",
  /imperfection_knockdown: 0\.75/.test(yaml) &&
  /am_anisotropy_knockdown: 0\.9/.test(yaml) &&
  /safety_factor: 1\.5/.test(yaml));
check("collapse stage is labelled PRELIMINARY in the YAML itself",
  /PRELIMINARY sizing — not a classification-society calculation/.test(yaml));

console.log("\n[step 3] click Add corrosion assessment");
yaml = insertPipelineStages(yaml, corrosionStages(marine));
check("solver.marine.corrosion inserted with the galvanic couple",
  /plugin: solver\.marine\.corrosion/.test(yaml) &&
  /alloy: '316L'/.test(yaml) &&
  /mating_alloy: 'NAB'/.test(yaml));
check("cathodic protection toggle reaches the YAML as a bool",
  /cathodic_protection: true/.test(yaml));
check("composition inputs written from the alloy table (PREN inputs)",
  /chromium_pct: 17/.test(yaml) &&
  /molybdenum_pct: 2\.5/.test(yaml) &&
  /nitrogen_pct: 0\.05/.test(yaml));

console.log("\n[step 4] click Add qualification report");
marine.fieldStageId = findFieldStageId(yaml);
check("last marine field producer resolved",
  marine.fieldStageId === "marine-corrosion",
  String(marine.fieldStageId));
yaml = insertPipelineStages(yaml, qualificationStages(marine));
check("writer.marine.qualification_report appended at the end",
  /plugin: writer\.marine\.qualification_report/.test(yaml) &&
  yaml.indexOf("- id: marine-qualification") > yaml.indexOf("- id: write"));
check("dossier wired to the corrosion field",
  /- id: marine-qualification\b[\s\S]*?field: \{ from: marine-corrosion \}/.test(yaml));
check("advisory disclaimer travels with the stage",
  /ADVISORY dossier — souxmar is not a classification society/.test(yaml));
check("all four marine stages present exactly once",
  ["solver.marine.hydrostatic", "solver.marine.hull_collapse",
   "solver.marine.corrosion", "writer.marine.qualification_report"]
    .every(cap => yaml.split(cap).length === 2));

header("Scenario 8 — gate: no mesh-producing stage");

const noMesh = [
  "version: 1",
  "",
  "stages:",
  "  - id: write",
  "    plugin: writer.vtu",
  "    input:",
  "      path: out.vtu",
  "",
].join("\n");

check("hasMeshStage() closes the gate",
  hasMeshStage(noMesh) === false);
const gated = insertPipelineStages(noMesh, thermalStages(lpbf));
check("no stage was written into the document",
  !/- id: am-thermal\b/.test(gated) && !/solver\.am\.thermal\.lpbf\b(?![,\s]*$)/.test(
    gated.split("# TODO")[0]));
check("a TODO comment names the capabilities that could not be wired",
  /# TODO: no mesh-producing stage \(mesher\.\* \/ reader\.\*\) found; cannot wire solver\.am\.thermal\.lpbf, postproc\.am\.melt_pool\./.test(gated),
  gated);
check("a reader.* stage is accepted as a mesh producer (reader.lattice)",
  hasMeshStage(noMesh.replace("plugin: writer.vtu", "plugin: reader.lattice")) === true);

// ---------------------------------------------------------------------------

header(`Results: ${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
