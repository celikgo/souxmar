// SPDX-License-Identifier: Apache-2.0
//
// Manufacturing panel — additive-manufacturing process setup for the
// pipeline editor. Sits with the Mesher / Solvers / Materials / BC
// pickers above the YAML editor when a pipeline*.yaml is open, and
// like them it is buffer-only: every action rewrites the open buffer
// and the user still presses Save (⌘S) to reach disk.
//
// Unlike the Solvers picker this panel does not swap a `plugin:` line —
// it appends whole stages through `insertPipelineStages` (see
// YamlViewer.tsx), because an AM analysis is a chain: a solver that
// produces a field plus the postproc that consumes it. Stage ids are
// deduped against the document, so clicking an action twice gives you
// `am-thermal` and `am-thermal-2` rather than a broken DAG.
//
// Capability ids and every input key/unit/default below come from the
// frozen manufacturing capability contract (§3.3–§3.14). Numbers the
// user does not edit are still written out explicitly so the YAML is
// self-documenting rather than relying on plugin-side defaults.
//
// Honest limits of the mapping:
//   · DED-WAAM has no capability of its own — the metal thermal chain
//     is `solver.am.thermal.lpbf`, a Rosenthal moving-source model, so
//     for WAAM "laser power" is arc power and "scan speed" is travel
//     speed. The model ignores arc physics and droplet transfer.
//   · SLS has no capability of its own either; the polymer chain is
//     `solver.am.polymer.fff` (interlayer thermal cycling + reptation
//     healing). For a powder bed that is indicative at best — the
//     panel labels it as such and still writes `process: sls` into
//     `solver.am.buildtime`, which does model the process explicitly.
//   · The material table is curated in the frontend on purpose:
//     contract §2.6 says no plugin reads a material file, every
//     property is an explicit input with a default. Values are public
//     literature figures for editorial reference, not certification
//     data.
//
// Units follow the contract's house rule: strict SI everywhere, with
// temperatures in °C.

import { useMemo, useState } from "react";
import type { CSSProperties } from "react";
import type { StageSpec } from "./YamlViewer";
import { findFieldStageId, hasMeshStage, insertPipelineStages } from "./YamlViewer";

interface Props {
  /** Current editor buffer (may include unsaved edits). */
  currentText: string;
  /** Mutate the editor buffer. */
  onChange:    (nextText: string) => void;
}

// ---------------------------------------------------------------------------
// Processes.

type ProcessId = "lpbf" | "ded_waam" | "fff" | "sls";
type Family    = "metal" | "polymer";

interface ProcessMeta {
  id:     ProcessId;
  label:  string;
  blurb:  string;
  family: Family;
}

const PROCESSES: ProcessMeta[] = [
  { id: "lpbf",     label: "LPBF",     family: "metal",   blurb: "laser powder-bed fusion — metal powder bed" },
  { id: "ded_waam", label: "DED-WAAM", family: "metal",   blurb: "wire-arc directed energy deposition" },
  { id: "fff",      label: "FFF",      family: "polymer", blurb: "fused filament fabrication — polymer extrusion" },
  { id: "sls",      label: "SLS",      family: "polymer", blurb: "selective laser sintering — polymer powder" },
];

function familyOf(process: ProcessId): Family {
  return PROCESSES.find(p => p.id === process)!.family;
}

// ---------------------------------------------------------------------------
// Build orientation — the six axis-aligned directions. `build_direction`
// is normalised plugin-side (contract §2.1); +Z is the contract default
// and the only direction `mesher.am.layered` can actually produce.

interface Orientation {
  id:    string;
  label: string;
  vec:   [number, number, number];
}

const ORIENTATIONS: Orientation[] = [
  { id: "+x", label: "+X", vec: [ 1, 0, 0] },
  { id: "-x", label: "−X", vec: [-1, 0, 0] },
  { id: "+y", label: "+Y", vec: [ 0, 1, 0] },
  { id: "-y", label: "−Y", vec: [ 0,-1, 0] },
  { id: "+z", label: "+Z", vec: [ 0, 0, 1] },
  { id: "-z", label: "−Z", vec: [ 0, 0,-1] },
];

// ---------------------------------------------------------------------------
// Materials. `id` is the contract `alloy:` string for metals (§3.17's
// vocabulary) so the corrosion/report stages accept it verbatim;
// polymers carry a plain label because no capability keys off it.
//
// transition_c is the melting temperature for alloys and the glass
// transition for polymers — both °C, both fed to different contract
// keys (`melt_temperature` / `glass_transition_temperature`).

interface Material {
  id:                    string;
  label:                 string;
  family:                Family;
  youngs_modulus:        number;  // Pa
  poisson_ratio:         number;  // -
  density:               number;  // kg/m³
  yield_strength:        number;  // Pa
  thermal_conductivity:  number;  // W/(m·K)
  specific_heat:         number;  // J/(kg·K)
  cte:                   number;  // 1/K
  transition_c:          number;  // °C — T_melt (metal) / T_g (polymer)
  /** Suggested extrusion set-points, °C. Polymers only. */
  nozzle_c?:             number;
  bed_c?:                number;
  chamber_c?:            number;
}

const MATERIALS: Material[] = [
  // Alloys — 316L reproduces the contract defaults of §3.3/§3.5 exactly.
  {
    id: "316L", label: "316L stainless", family: "metal",
    youngs_modulus: 1.9e11, poisson_ratio: 0.28, density: 7990,
    yield_strength: 5.0e8, thermal_conductivity: 15.0, specific_heat: 500,
    cte: 1.6e-5, transition_c: 1400,
  },
  {
    id: "2507", label: "2507 super duplex", family: "metal",
    youngs_modulus: 2.0e11, poisson_ratio: 0.27, density: 7800,
    yield_strength: 5.5e8, thermal_conductivity: 15.0, specific_heat: 480,
    cte: 1.3e-5, transition_c: 1400,
  },
  {
    id: "NAB", label: "NAB (Ni-Al bronze)", family: "metal",
    youngs_modulus: 1.20e11, poisson_ratio: 0.33, density: 7600,
    yield_strength: 2.5e8, thermal_conductivity: 40.0, specific_heat: 420,
    cte: 1.6e-5, transition_c: 1050,
  },
  {
    id: "Ti6Al4V", label: "Ti-6Al-4V", family: "metal",
    youngs_modulus: 1.138e11, poisson_ratio: 0.342, density: 4430,
    yield_strength: 8.8e8, thermal_conductivity: 6.7, specific_heat: 526,
    cte: 8.6e-6, transition_c: 1650,
  },
  {
    id: "IN625", label: "Inconel 625", family: "metal",
    youngs_modulus: 2.08e11, poisson_ratio: 0.31, density: 8440,
    yield_strength: 4.9e8, thermal_conductivity: 9.8, specific_heat: 410,
    cte: 1.28e-5, transition_c: 1350,
  },
  {
    id: "AlSi10Mg", label: "AlSi10Mg", family: "metal",
    youngs_modulus: 7.0e10, poisson_ratio: 0.33, density: 2670,
    yield_strength: 2.4e8, thermal_conductivity: 120, specific_heat: 910,
    cte: 2.1e-5, transition_c: 570,
  },
  {
    id: "CuNi90-10", label: "CuNi 90-10", family: "metal",
    youngs_modulus: 1.35e11, poisson_ratio: 0.33, density: 8900,
    yield_strength: 1.4e8, thermal_conductivity: 40.0, specific_heat: 380,
    cte: 1.7e-5, transition_c: 1150,
  },
  // Polymers. PA12 reproduces the ρ/c_p/k defaults of contract §3.7.
  {
    id: "PEKK", label: "PEKK", family: "polymer",
    youngs_modulus: 3.9e9, poisson_ratio: 0.38, density: 1300,
    yield_strength: 1.0e8, thermal_conductivity: 0.25, specific_heat: 1700,
    cte: 4.7e-5, transition_c: 162,
    nozzle_c: 400, bed_c: 130, chamber_c: 90,
  },
  {
    id: "PA12-CF", label: "PA12-CF", family: "polymer",
    youngs_modulus: 5.9e9, poisson_ratio: 0.39, density: 1060,
    yield_strength: 7.6e7, thermal_conductivity: 0.35, specific_heat: 1800,
    cte: 4.0e-5, transition_c: 50,
    nozzle_c: 260, bed_c: 100, chamber_c: 50,
  },
  {
    id: "PA12", label: "PA12", family: "polymer",
    youngs_modulus: 1.7e9, poisson_ratio: 0.39, density: 1010,
    yield_strength: 4.5e7, thermal_conductivity: 0.25, specific_heat: 1800,
    cte: 9.0e-5, transition_c: 45,
    nozzle_c: 250, bed_c: 100, chamber_c: 40,
  },
  {
    id: "ABS", label: "ABS", family: "polymer",
    youngs_modulus: 2.2e9, poisson_ratio: 0.35, density: 1040,
    yield_strength: 4.0e7, thermal_conductivity: 0.17, specific_heat: 1900,
    cte: 9.0e-5, transition_c: 105,
    nozzle_c: 240, bed_c: 100, chamber_c: 60,
  },
];

// ---------------------------------------------------------------------------
// Numeric process parameters. `key` is the contract input key, `def` the
// contract default. layer_height appears in both tables with different
// defaults (§3.3 metal 1 mm vs §3.7 polymer 200 µm), which is why the
// two families keep separate state records.

interface ParamSpec {
  key:      string;
  label:    string;
  unit:     string;
  def:      number;
  step:     number;
  /** Processes that surface this control. */
  shownFor: ProcessId[];
  /** Per-process label override (WAAM has an arc, not a laser). */
  labelFor?: Partial<Record<ProcessId, string>>;
}

const METAL_PARAMS: ParamSpec[] = [
  { key: "laser_power",     label: "laser power",     unit: "W",    def: 200,    step: 10,     shownFor: ["lpbf", "ded_waam"], labelFor: { ded_waam: "arc power" } },
  { key: "scan_speed",      label: "scan speed",      unit: "m/s",  def: 0.8,    step: 0.05,   shownFor: ["lpbf", "ded_waam"], labelFor: { ded_waam: "travel speed" } },
  { key: "hatch_spacing",   label: "hatch spacing",   unit: "m",    def: 1.1e-4, step: 1e-5,   shownFor: ["lpbf", "ded_waam"] },
  { key: "layer_height",    label: "layer height",    unit: "m",    def: 0.001,  step: 1e-4,   shownFor: ["lpbf", "ded_waam"] },
  { key: "deposition_rate", label: "deposition rate", unit: "kg/h", def: 3.0,    step: 0.5,    shownFor: ["ded_waam"] },
];

const POLYMER_PARAMS: ParamSpec[] = [
  { key: "nozzle_temperature",  label: "nozzle T",  unit: "°C", def: 250,    step: 5,    shownFor: ["fff", "sls"] },
  { key: "bed_temperature",     label: "bed T",     unit: "°C", def: 100,    step: 5,    shownFor: ["fff", "sls"] },
  { key: "chamber_temperature", label: "chamber T", unit: "°C", def: 40,     step: 5,    shownFor: ["fff", "sls"] },
  { key: "layer_time",          label: "layer time",unit: "s",  def: 20,     step: 1,    shownFor: ["fff", "sls"] },
  { key: "layer_height",        label: "layer height", unit: "m", def: 2.0e-4, step: 5e-5, shownFor: ["fff", "sls"] },
  { key: "road_width",          label: "road width",   unit: "m", def: 4.0e-4, step: 5e-5, shownFor: ["fff", "sls"] },
];

function defaultsOf(specs: ParamSpec[]): Record<string, number> {
  const out: Record<string, number> = {};
  for (const s of specs) out[s.key] = s.def;
  return out;
}

// ---------------------------------------------------------------------------

export function ManufacturingPanel({ currentText, onChange }: Props) {
  // Collapsed by default, unlike the four panels above: most pipelines
  // are not AM builds, and seven expanded strips would leave the YAML
  // editor a slit. The header line still summarises the current setup.
  const [collapsed, setCollapsed]   = useState(true);
  const [process, setProcess]       = useState<ProcessId>("lpbf");
  const [metalId, setMetalId]       = useState<string>("316L");
  const [polymerId, setPolymerId]   = useState<string>("PA12");
  const [orientId, setOrientId]     = useState<string>("+z");
  const [metal, setMetal]           = useState<Record<string, number>>(() => defaultsOf(METAL_PARAMS));
  const [polymer, setPolymer]       = useState<Record<string, number>>(() => defaultsOf(POLYMER_PARAMS));

  const family      = familyOf(process);
  const paramSpecs  = family === "metal" ? METAL_PARAMS : POLYMER_PARAMS;
  const params      = family === "metal" ? metal : polymer;
  const setParams   = family === "metal" ? setMetal : setPolymer;
  const visible     = paramSpecs.filter(s => s.shownFor.includes(process));

  const materials = useMemo(() => MATERIALS.filter(m => m.family === family), [family]);
  const material  = useMemo(
    () => materials.find(m => m.id === (family === "metal" ? metalId : polymerId)) ?? materials[0],
    [materials, family, metalId, polymerId],
  );
  const orientation = ORIENTATIONS.find(o => o.id === orientId) ?? ORIENTATIONS[4];

  // Every AM stage needs `mesh: { from: … }`; without a mesh-producing
  // stage the actions have nowhere to attach. Gate them (MaterialsPanel
  // pattern) rather than writing a dangling reference.
  const meshReady = useMemo(() => hasMeshStage(currentText), [currentText]);

  const ctx: BuildContext = {
    process,
    material,
    params,
    build_direction: orientation.vec,
    fieldStageId: findFieldStageId(currentText),
  };

  const actions = family === "metal" ? metalActions(ctx) : polymerActions(ctx);

  const run = (specs: StageSpec[]) => {
    if (!meshReady) return;
    onChange(insertPipelineStages(currentText, specs));
  };

  const pickMaterial = (m: Material) => {
    if (m.family === "metal") {
      setMetalId(m.id);
      return;
    }
    setPolymerId(m.id);
    // Seed the extrusion set-points from the polymer's recommended
    // window — the user can still override every field afterwards.
    setPolymer(p => ({
      ...p,
      nozzle_temperature:  m.nozzle_c  ?? p.nozzle_temperature,
      bed_temperature:     m.bed_c     ?? p.bed_temperature,
      chamber_temperature: m.chamber_c ?? p.chamber_temperature,
    }));
  };

  return (
    <section style={wrapStyle} aria-label="Manufacturing">
      <header style={headerStyle}>
        <button
          type="button"
          onClick={() => setCollapsed(c => !c)}
          style={titleButtonStyle}
          aria-expanded={!collapsed}
        >
          <span style={chevronStyle}>{collapsed ? "▸" : "▾"}</span>
          <span style={titleTextStyle}>Manufacturing</span>
          <span style={countStyle}>
            {PROCESSES.find(p => p.id === process)!.label} ·{" "}
            <code style={codeStyle}>{material.id}</code> · build{" "}
            <code style={codeStyle}>{orientation.label}</code>
            {!meshReady && (
              <span style={hintStyle}>needs a mesh stage — add a mesher above</span>
            )}
            <span style={badgeStyle}>curated materials · §2.6</span>
          </span>
        </button>
      </header>

      {!collapsed && (
        <div style={bodyStyle}>
          {!meshReady && (
            <div style={bannerStyle} role="status">
              <strong>No mesh-producing stage in this pipeline.</strong> Every AM
              stage is wired with <code style={bannerCodeStyle}>mesh: {"{ from: … }"}</code>,
              so there is nothing to attach to yet. Add a{" "}
              <code style={bannerCodeStyle}>mesher.*</code> stage from the{" "}
              <strong>Mesher</strong> panel above (or a{" "}
              <code style={bannerCodeStyle}>reader.lattice</code> stage by hand);
              these actions unlock the moment one exists.
            </div>
          )}

          <div style={groupsStyle}>
            <div style={groupStyle}>
              <div style={groupTitleStyle}>Process</div>
              <div style={chipsStyle}>
                {PROCESSES.map(p => (
                  <button
                    key={p.id}
                    type="button"
                    onClick={() => setProcess(p.id)}
                    style={{ ...chipStyle, ...(p.id === process ? chipActiveStyle : null) }}
                    aria-pressed={p.id === process}
                    title={p.blurb}
                  >
                    <span style={chipNameStyle}>{p.label}</span>
                    <span style={chipMetaStyle}>{p.family}</span>
                  </button>
                ))}
              </div>
            </div>

            <div style={groupStyle}>
              <div style={groupTitleStyle}>Material</div>
              <div style={columnStyle}>
                <div style={chipsStyle}>
                  {materials.map(m => (
                    <button
                      key={m.id}
                      type="button"
                      onClick={() => pickMaterial(m)}
                      style={{ ...chipStyle, ...(m.id === material.id ? chipActiveStyle : null) }}
                      aria-pressed={m.id === material.id}
                      title={describeMaterial(m)}
                    >
                      <span style={chipNameStyle}>{m.label}</span>
                      <span style={chipMetaStyle}>
                        ρ={num(m.density)} kg/m³
                      </span>
                    </button>
                  ))}
                </div>
                <dl style={readoutStyle} aria-label={`${material.label} properties (read-only)`}>
                  <Readout term="E"   value={`${num(material.youngs_modulus)} Pa`} />
                  <Readout term="ν"   value={num(material.poisson_ratio)} />
                  <Readout term="ρ"   value={`${num(material.density)} kg/m³`} />
                  <Readout term="σ_y" value={`${num(material.yield_strength)} Pa`} />
                  <Readout term="k"   value={`${num(material.thermal_conductivity)} W/(m·K)`} />
                  <Readout term="c_p" value={`${num(material.specific_heat)} J/(kg·K)`} />
                  <Readout term="α"   value={`${num(material.cte)} 1/K`} />
                  <Readout
                    term={family === "metal" ? "T_melt" : "T_g"}
                    value={`${num(material.transition_c)} °C`}
                  />
                </dl>
              </div>
            </div>

            <div style={groupStyle}>
              <div style={groupTitleStyle}>Parameters</div>
              <div style={fieldsStyle}>
                {visible.map(s => {
                  const id = `mfg-${family}-${s.key}`;
                  return (
                    <span key={s.key} style={fieldStyle}>
                      <label htmlFor={id} style={labelStyle}>
                        {s.labelFor?.[process] ?? s.label} ({s.unit})
                      </label>
                      <input
                        id={id}
                        type="number"
                        step={s.step}
                        value={params[s.key]}
                        onChange={e =>
                          setParams(p => ({ ...p, [s.key]: safeNumber(e.target.value, s.def) }))
                        }
                        style={numInputStyle}
                        title={`contract key ${s.key} — default ${s.def} ${s.unit}`}
                      />
                      <span style={defaultStyle}>def {s.def}</span>
                    </span>
                  );
                })}
              </div>
            </div>

            <div style={groupStyle}>
              <div style={groupTitleStyle}>Build up</div>
              <div style={chipsStyle}>
                {ORIENTATIONS.map(o => (
                  <button
                    key={o.id}
                    type="button"
                    onClick={() => setOrientId(o.id)}
                    style={{ ...chipStyle, ...(o.id === orientId ? chipActiveStyle : null) }}
                    aria-pressed={o.id === orientId}
                    title={`build_direction: [${o.vec.join(", ")}]`}
                  >
                    <span style={chipNameStyle}>{o.label}</span>
                  </button>
                ))}
              </div>
            </div>

            <div style={groupStyle}>
              <div style={groupTitleStyle}>Actions</div>
              <div style={chipsStyle}>
                {actions.map(a => (
                  <button
                    key={a.key}
                    type="button"
                    onClick={() => run(a.build())}
                    disabled={!meshReady}
                    style={{
                      ...actionStyle,
                      ...(!meshReady ? actionDisabledStyle : null),
                    }}
                    title={meshReady ? a.hint : "Add a mesh-producing stage first"}
                  >
                    {a.label}
                  </button>
                ))}
              </div>
            </div>
          </div>

          <div style={noteStyle} role="note">
            Printability and build-time numbers are documented heuristics, not
            guarantees; the DfAM score in <code style={bannerCodeStyle}>solver.am.printability</code>{" "}
            weights overhang, wall thickness and build-volume fit and says so in
            its own output. Distortion needs{" "}
            <code style={bannerCodeStyle}>strain_calibration</code> fitted against
            a measured part before its magnitudes mean anything.
          </div>
        </div>
      )}
    </section>
  );
}

// `dl > div > (dt, dd)` is the valid HTML5 grouping wrapper — a span
// there would be invalid markup even though it renders.
function Readout({ term, value }: { term: string; value: string }) {
  return (
    <div style={readoutItemStyle}>
      <dt style={readoutTermStyle}>{term}</dt>
      <dd style={readoutValueStyle}>{value}</dd>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Stage construction. One function per action; every key comes from the
// contract, in contract order, so a reviewer can diff the two side by
// side. `$MESH` / `$STAGE<n>` are resolved by insertPipelineStages.

interface BuildContext {
  process:         ProcessId;
  material:        Material;
  params:          Record<string, number>;
  build_direction: [number, number, number];
  fieldStageId:    string | null;
}

interface ActionMeta {
  key:   string;
  label: string;
  hint:  string;
  build: () => StageSpec[];
}

function metalActions(ctx: BuildContext): ActionMeta[] {
  return [
    {
      key:   "thermal",
      label: "Add thermal analysis",
      hint:  "solver.am.thermal.lpbf (Rosenthal layer-wise history) + postproc.am.melt_pool",
      build: () => thermalStages(ctx),
    },
    {
      key:   "distortion",
      label: "Add distortion analysis",
      hint:  "solver.am.distortion.inherent_strain + postproc.am.residual_stress",
      build: () => distortionStages(ctx),
    },
    {
      key:   "dfam",
      label: "Add manufacturability checks",
      hint:  "solver.am.overhang + solver.am.printability + solver.am.buildtime",
      build: () => manufacturabilityStages(ctx),
    },
    {
      key:   "report",
      label: "Add build report",
      hint:  "writer.am.report — Markdown build report / traveller sheet",
      build: () => reportStages(ctx),
    },
  ];
}

function polymerActions(ctx: BuildContext): ActionMeta[] {
  const sls = ctx.process === "sls";
  return [
    {
      key:   "fff",
      label: "Add FFF thermal + bond strength",
      hint:  sls
        ? "solver.am.polymer.fff + postproc.am.bond_strength — the only polymer chain in the contract; indicative for a powder bed, not an SLS model"
        : "solver.am.polymer.fff (lumped interlayer cooling) + postproc.am.bond_strength (reptation healing)",
      build: () => polymerStages(ctx),
    },
    {
      key:   "gcode",
      label: "Add G-code output",
      hint:  "writer.am.gcode — planar slice → FFF G-code (single perimeter, no supports)",
      build: () => gcodeStages(ctx),
    },
  ];
}

function thermalStages(ctx: BuildContext): StageSpec[] {
  const { material: m, params: p } = ctx;
  // Shared §3.3/§3.4 process inputs, written into both stages so the
  // melt-pool postproc sees the same process window as the solver.
  const shared: Array<[string, number]> = [
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
      id:     "am-thermal",
      plugin: "solver.am.thermal.lpbf",
      note:   `${ctx.process} thermal history — ${m.id}`,
      input:  [
        { key: "mesh", value: "{ from: $MESH }" },
        ...shared.map(([k, v]) => ({ key: k, value: num(v) })),
        { key: "baseplate_temperature", value: "80" },
        { key: "interlayer_time",       value: "10.0" },
        { key: "build_direction",       value: vec3(ctx.build_direction) },
        { key: "baseplate_layers",      value: "0" },
        { key: "num_layers",            value: "0" },
      ],
    },
    {
      id:     "am-melt-pool",
      plugin: "postproc.am.melt_pool",
      note:   "melt-pool depth / normalised enthalpy / porosity risk",
      input:  [
        { key: "mesh",  value: "{ from: $MESH }" },
        { key: "field", value: "{ from: $STAGE0 }" },
        ...shared.map(([k, v]) => ({ key: k, value: num(v) })),
      ],
    },
  ];
}

function distortionStages(ctx: BuildContext): StageSpec[] {
  const { material: m, params: p } = ctx;
  return [
    {
      id:     "am-distortion",
      plugin: "solver.am.distortion.inherent_strain",
      note:   "inherent strain — strain_calibration MUST be fitted to a measured part",
      input:  [
        { key: "mesh",                value: "{ from: $MESH }" },
        { key: "youngs_modulus",      value: num(m.youngs_modulus) },
        { key: "poisson_ratio",       value: num(m.poisson_ratio) },
        { key: "cte",                 value: num(m.cte) },
        { key: "melt_temperature",    value: num(m.transition_c) },
        { key: "preheat_temperature", value: "80" },
        { key: "inherent_strain",     value: "0.0" },
        { key: "strain_calibration",  value: "0.30" },
        { key: "layer_height",        value: num(numOr(p.layer_height, 0.001)) },
        { key: "substrate_thickness", value: "0.0" },
        { key: "build_direction",     value: vec3(ctx.build_direction) },
        { key: "baseplate_layers",    value: "0" },
        { key: "baseplate_clamped",   value: "true" },
        { key: "num_layers",          value: "0" },
      ],
    },
    {
      id:     "am-residual-stress",
      plugin: "postproc.am.residual_stress",
      note:   "von-Mises-equivalent residual stress",
      input:  [
        { key: "mesh",           value: "{ from: $MESH }" },
        { key: "field",          value: "{ from: $STAGE0 }" },
        { key: "yield_strength", value: num(m.yield_strength) },
        { key: "youngs_modulus", value: num(m.youngs_modulus) },
        { key: "poisson_ratio",  value: num(m.poisson_ratio) },
        { key: "cte",            value: num(m.cte) },
      ],
    },
  ];
}

function manufacturabilityStages(ctx: BuildContext): StageSpec[] {
  const { material: m, params: p } = ctx;
  const layerHeight = numOr(p.layer_height, ctx.process === "fff" || ctx.process === "sls" ? 2.0e-4 : 0.001);
  return [
    {
      id:     "am-overhang",
      plugin: "solver.am.overhang",
      note:   "downskin tilt / support need per cell",
      input:  [
        { key: "mesh",                   value: "{ from: $MESH }" },
        { key: "build_direction",        value: vec3(ctx.build_direction) },
        { key: "overhang_threshold_deg", value: "45" },
        { key: "layer_height",           value: num(layerHeight) },
      ],
    },
    {
      id:     "am-printability",
      plugin: "solver.am.printability",
      note:   "documented DfAM heuristic, not a guarantee",
      input:  [
        { key: "mesh",                   value: "{ from: $MESH }" },
        { key: "build_direction",        value: vec3(ctx.build_direction) },
        { key: "overhang_threshold_deg", value: "45" },
        { key: "min_wall_thickness",     value: "5.0e-4" },
        { key: "machine_build_volume",   value: "[0.25, 0.25, 0.3]" },
        { key: "corrosion_allowance",    value: "0.0" },
        { key: "layer_height",           value: num(layerHeight) },
      ],
    },
    {
      id:     "am-buildtime",
      plugin: "solver.am.buildtime",
      note:   "per-layer time / energy / mass / cost",
      input:  [
        { key: "mesh",                   value: "{ from: $MESH }" },
        { key: "process",                value: `'${ctx.process}'` },
        { key: "layer_height",           value: num(layerHeight) },
        { key: "process_layer_height",   value: "3.0e-5" },
        { key: "scan_speed",             value: num(numOr(p.scan_speed, 0.8)) },
        { key: "hatch_spacing",          value: num(numOr(p.hatch_spacing, 1.1e-4)) },
        { key: "recoat_time",            value: "8.0" },
        { key: "print_speed",            value: "0.05" },
        { key: "road_width",             value: num(numOr(p.road_width, 4.0e-4)) },
        { key: "deposition_rate",        value: num(numOr(p.deposition_rate, 3.0)) },
        { key: "laser_power",            value: num(numOr(p.laser_power, 200)) },
        { key: "machine_power_overhead", value: "2500" },
        { key: "material_cost_per_kg",   value: "90" },
        { key: "machine_rate_per_hour",  value: "45" },
        { key: "density",                value: num(m.density) },
        { key: "build_direction",        value: vec3(ctx.build_direction) },
      ],
    },
  ];
}

function reportStages(ctx: BuildContext): StageSpec[] {
  const { material: m } = ctx;
  return [
    {
      id:     "am-report",
      plugin: "writer.am.report",
      note:   "Markdown build report / traveller sheet",
      input:  [
        { key: "mesh", value: "{ from: $MESH }" },
        // writer.* takes an OPTIONAL field; wire the last field
        // producer in the document, or leave the key out entirely so
        // the writer renders its "no field supplied" note.
        ...(ctx.fieldStageId
          ? [{ key: "field", value: `{ from: ${ctx.fieldStageId} }` }]
          : []),
        { key: "path",                   value: "am-build-report.md" },
        { key: "title",                  value: "'souxmar AM build report'" },
        { key: "process",                value: `'${ctx.process}'` },
        { key: "material",               value: `'${m.id}'` },
        { key: "design_notes",           value: "''" },
        { key: "material_cost_per_kg",   value: "90" },
        { key: "machine_rate_per_hour",  value: "45" },
        { key: "machine_power_overhead", value: "2500" },
        { key: "density",                value: num(m.density) },
      ],
    },
  ];
}

function polymerStages(ctx: BuildContext): StageSpec[] {
  const { material: m, params: p } = ctx;
  const layerTime = numOr(p.layer_time, 20);
  return [
    {
      id:     "am-fff",
      plugin: "solver.am.polymer.fff",
      note:   ctx.process === "sls"
        ? `interlayer thermal cycling — ${m.id}; FFF model applied to an SLS build, indicative only`
        : `interlayer thermal cycling — ${m.id}`,
      input:  [
        { key: "mesh",                         value: "{ from: $MESH }" },
        { key: "nozzle_temperature",           value: num(numOr(p.nozzle_temperature, 250)) },
        { key: "bed_temperature",              value: num(numOr(p.bed_temperature, 100)) },
        { key: "chamber_temperature",          value: num(numOr(p.chamber_temperature, 40)) },
        { key: "layer_time",                   value: num(layerTime) },
        { key: "layer_height",                 value: num(numOr(p.layer_height, 2.0e-4)) },
        { key: "road_width",                   value: num(numOr(p.road_width, 4.0e-4)) },
        { key: "convection_coefficient",       value: "30" },
        { key: "density",                      value: num(m.density) },
        { key: "specific_heat",                value: num(m.specific_heat) },
        { key: "thermal_conductivity",         value: num(m.thermal_conductivity) },
        { key: "glass_transition_temperature", value: num(m.transition_c) },
        { key: "build_direction",              value: vec3(ctx.build_direction) },
        { key: "baseplate_layers",             value: "0" },
        { key: "num_layers",                   value: "0" },
      ],
    },
    {
      id:     "am-bond-strength",
      plugin: "postproc.am.bond_strength",
      note:   "reptation healing → degree of healing / Z-strength fraction",
      input:  [
        { key: "mesh",                            value: "{ from: $MESH }" },
        { key: "field",                           value: "{ from: $STAGE0 }" },
        { key: "glass_transition_temperature",    value: num(m.transition_c) },
        { key: "reptation_time_reference",        value: "2.0" },
        { key: "reptation_reference_temperature", value: "260" },
        { key: "activation_energy",               value: "8.0e4" },
        { key: "layer_time",                      value: num(layerTime) },
      ],
    },
  ];
}

function gcodeStages(ctx: BuildContext): StageSpec[] {
  const { params: p } = ctx;
  return [
    {
      id:     "am-gcode",
      plugin: "writer.am.gcode",
      note:   "single perimeter + alternating infill; no supports, no bridging",
      input:  [
        { key: "mesh",               value: "{ from: $MESH }" },
        { key: "path",               value: "am-part.gcode" },
        { key: "layer_height",       value: num(numOr(p.layer_height, 2.0e-4)) },
        { key: "road_width",         value: num(numOr(p.road_width, 4.0e-4)) },
        { key: "filament_diameter",  value: "1.75e-3" },
        { key: "nozzle_temperature", value: num(numOr(p.nozzle_temperature, 250)) },
        { key: "bed_temperature",    value: num(numOr(p.bed_temperature, 100)) },
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
// Small helpers.

function describeMaterial(m: Material): string {
  return [
    m.label,
    `E       = ${m.youngs_modulus} Pa`,
    `ν       = ${m.poisson_ratio}`,
    `ρ       = ${m.density} kg/m³`,
    `σ_yield = ${m.yield_strength} Pa`,
    `k       = ${m.thermal_conductivity} W/(m·K)`,
    `c_p     = ${m.specific_heat} J/(kg·K)`,
    `α       = ${m.cte} 1/K`,
    `${m.family === "metal" ? "T_melt " : "T_g    "} = ${m.transition_c} °C`,
  ].join("\n");
}

/** YAML scalar formatting — plain decimals where readable, exponential
 *  for the very large / very small. Same rule as MaterialsPanel, with
 *  one hardening: the mantissa always keeps a decimal point, because
 *  `2.0e-4` is a float to every YAML parser while a bare `2e-4` is a
 *  *string* under a strict YAML 1.1 resolver. It also happens to be
 *  the notation the capability contract writes its defaults in. */
function num(v: number): string {
  if (Number.isInteger(v) && Math.abs(v) < 1e7) return String(v);
  if (Math.abs(v) >= 1e4 || (v !== 0 && Math.abs(v) < 1e-3)) {
    const [mantissa, exponent] = v.toExponential().replace(/e\+?/, "e").split("e");
    return `${mantissa.includes(".") ? mantissa : `${mantissa}.0`}e${exponent}`;
  }
  return String(v);
}

function vec3(v: [number, number, number]): string {
  return `[${v[0]}, ${v[1]}, ${v[2]}]`;
}

function numOr(v: number | undefined, fallback: number): number {
  return v !== undefined && Number.isFinite(v) ? v : fallback;
}

function safeNumber(raw: string, fallback: number): number {
  const n = Number(raw);
  return Number.isFinite(n) ? n : fallback;
}

// ---------------------------------------------------------------------------
// Styles — dim theme, same grammar as Solvers / Materials / BCs. Kept
// side-by-side with those panels on purpose (see MeshingPanel's note):
// if they drift, that is the signal to factor out a Picker primitive.

const wrapStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  background:     "var(--bg-panel, rgba(255,255,255,0.02))",
  borderBottom:   "1px solid var(--border-subtle)",
  flexShrink:     0,
};

const headerStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "center",
  height:         28,
  borderBottom:   "1px solid var(--border-subtle)",
};

const titleButtonStyle: CSSProperties = {
  display:        "inline-flex",
  alignItems:     "center",
  gap:            8,
  background:     "transparent",
  border:         "none",
  color:          "var(--fg-tertiary)",
  fontSize:       11,
  cursor:         "pointer",
  padding:        "0 var(--space-3)",
  height:         "100%",
  width:          "100%",
  textAlign:      "left",
};

const chevronStyle: CSSProperties = {
  fontSize:       9,
  color:          "var(--fg-tertiary)",
  width:          12,
};

const titleTextStyle: CSSProperties = {
  textTransform:  "uppercase",
  letterSpacing:  0.6,
  fontWeight:     600,
  color:          "var(--fg-secondary)",
};

const countStyle: CSSProperties = {
  display:        "inline-flex",
  alignItems:     "center",
  gap:            10,
  opacity:        0.8,
  fontSize:       11,
};

const badgeStyle: CSSProperties = {
  fontSize:       9,
  padding:        "1px 6px",
  borderRadius:   "var(--radius-sm, 3px)",
  background:     "rgba(255,255,255,0.06)",
  color:          "var(--fg-tertiary)",
  textTransform:  "uppercase",
  letterSpacing:  0.5,
};

const hintStyle: CSSProperties = {
  color:          "var(--warning, #ffd43b)",
  fontStyle:      "italic",
};

const codeStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  fontSize:       11,
  color:          "var(--accent-default, #1d9bf0)",
};

const bodyStyle: CSSProperties = {
  padding:        "8px var(--space-3, 12px)",
  maxHeight:      280,
  overflow:       "auto",
};

const bannerStyle: CSSProperties = {
  padding:        "8px 10px",
  marginBottom:   8,
  borderLeft:     "3px solid var(--warning, #ffd43b)",
  background:     "rgba(255, 212, 59, 0.08)",
  borderRadius:   "var(--radius-sm, 4px)",
  fontSize:       11,
  color:          "var(--fg-secondary)",
  lineHeight:     1.45,
};

const bannerCodeStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  fontSize:       10.5,
  color:          "var(--accent-default, #1d9bf0)",
};

const noteStyle: CSSProperties = {
  marginTop:      8,
  padding:        "6px 10px",
  borderLeft:     "3px solid var(--border-strong, #4c5c6b)",
  background:     "rgba(255,255,255,0.03)",
  borderRadius:   "var(--radius-sm, 4px)",
  fontSize:       10.5,
  color:          "var(--fg-tertiary)",
  lineHeight:     1.45,
};

const groupsStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  gap:            8,
};

const groupStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "flex-start",
  gap:            10,
};

const groupTitleStyle: CSSProperties = {
  flexShrink:     0,
  width:          84,
  fontSize:       10,
  textTransform:  "uppercase",
  letterSpacing:  0.6,
  color:          "var(--fg-tertiary)",
  paddingTop:     5,
};

const columnStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  gap:            6,
  flex:           1,
  minWidth:       0,
};

const chipsStyle: CSSProperties = {
  display:        "flex",
  flexWrap:       "wrap",
  gap:            6,
  flex:           1,
  minWidth:       0,
};

const chipStyle: CSSProperties = {
  display:        "inline-flex",
  flexDirection:  "column",
  alignItems:     "flex-start",
  padding:        "4px 9px",
  borderRadius:   "var(--radius-md, 6px)",
  border:         "1px solid var(--border-subtle)",
  background:     "var(--bg-elevated, rgba(255,255,255,0.04))",
  color:          "var(--fg-primary)",
  fontSize:       11,
  cursor:         "pointer",
  whiteSpace:     "nowrap",
  minWidth:       0,
};

const chipActiveStyle: CSSProperties = {
  background:     "var(--accent-default, #1d9bf0)",
  borderColor:    "var(--accent-default, #1d9bf0)",
  color:          "#fff",
};

const chipNameStyle: CSSProperties = {
  fontWeight:     500,
};

const chipMetaStyle: CSSProperties = {
  fontSize:       10,
  opacity:        0.65,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
};

const readoutStyle: CSSProperties = {
  display:        "flex",
  flexWrap:       "wrap",
  gap:            "2px 12px",
  margin:         0,
  fontSize:       10,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  color:          "var(--fg-tertiary)",
};

const readoutItemStyle: CSSProperties = {
  display:        "inline-flex",
  alignItems:     "baseline",
  gap:            4,
  whiteSpace:     "nowrap",
};

const readoutTermStyle: CSSProperties = {
  margin:         0,
  color:          "var(--fg-secondary)",
};

const readoutValueStyle: CSSProperties = {
  margin:         0,
  color:          "var(--fg-primary)",
  opacity:        0.85,
};

const fieldsStyle: CSSProperties = {
  display:        "flex",
  flexWrap:       "wrap",
  gap:            "6px 12px",
  flex:           1,
  minWidth:       0,
};

const fieldStyle: CSSProperties = {
  display:        "inline-flex",
  alignItems:     "center",
  gap:            5,
  whiteSpace:     "nowrap",
};

const labelStyle: CSSProperties = {
  color:          "var(--fg-tertiary)",
  textTransform:  "uppercase",
  fontSize:       9,
  letterSpacing:  0.4,
  whiteSpace:     "nowrap",
};

const numInputStyle: CSSProperties = {
  width:          84,
  padding:        "2px 4px",
  background:     "var(--bg-canvas)",
  color:          "var(--fg-primary)",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm)",
  fontSize:       11,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
};

const defaultStyle: CSSProperties = {
  fontSize:       9,
  color:          "var(--fg-tertiary)",
  opacity:        0.75,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
};

const actionStyle: CSSProperties = {
  padding:        "4px 10px",
  borderRadius:   "var(--radius-sm, 4px)",
  border:         "1px solid var(--border-subtle)",
  background:     "var(--accent-default, #1d9bf0)",
  color:          "#fff",
  fontSize:       11,
  fontWeight:     500,
  cursor:         "pointer",
  whiteSpace:     "nowrap",
};

const actionDisabledStyle: CSSProperties = {
  opacity:        0.4,
  cursor:         "not-allowed",
};
