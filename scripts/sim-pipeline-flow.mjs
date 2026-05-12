#!/usr/bin/env node
// SPDX-License-Identifier: Apache-2.0
//
// Headless simulator for the workbench's pipeline-editor panels.
// Inlines the pure regex/parser transforms used by MeshingPanel,
// SolversPanel, MaterialsPanel and BoundaryConditionsPanel, then
// replays a representative "user clicks through the panels" sequence
// against the in-tree example pipelines.
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

// ---------------------------------------------------------------------------

header(`Results: ${pass} passed, ${fail} failed`);
process.exit(fail > 0 ? 1 : 0);
