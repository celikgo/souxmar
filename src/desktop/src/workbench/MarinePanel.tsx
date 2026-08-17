// SPDX-License-Identifier: Apache-2.0
//
// Marine panel — pressure-hull / seawater setup for the pipeline
// editor. Sits with the other pickers above the YAML editor when a
// pipeline*.yaml is open; buffer-only, like all of them, so ⌘S is
// still what reaches disk.
//
// Actions append whole stages through `insertPipelineStages` (see
// YamlViewer.tsx): §3.15 hydrostatic load cases, §3.16 collapse
// margin, §3.17 corrosion / galvanic / CP demand, §3.18 the advisory
// qualification dossier. Capability ids, input keys, units and
// defaults are the frozen contract's; nothing here is renamed or
// re-united.
//
// ADVISORY ONLY. The collapse margin is preliminary sizing built from
// Windenburg–Trilling / classical-buckling closed forms with blanket
// knockdown factors, and the qualification report is a checklist
// derived from publicly documented AM qualification practice. souxmar
// is not a classification society and none of this output is class
// approval. The panel says so on screen too — that sentence is not
// decoration, it is the honest scope of the model.
//
// Seawater density: the panel shows the one-atmosphere International
// Equation of State of Seawater 1980 (Millero & Poisson 1981) value
// for the chosen salinity/temperature and writes that number into
// BOTH marine stages. §3.15 also accepts `seawater_density: 0.0` to
// let the solver derive its own, but writing the number explicitly
// keeps a single value in the YAML instead of two correlations that
// could disagree — and §3.16 has no derive path at all.
//
// The alloy table is duplicated from ManufacturingPanel deliberately:
// contract §2.6 forbids plugins reading a material file, each panel
// ships as an independent surface, and there are exactly two
// consumers. A third consumer is the signal to factor out a materials
// module — not before.

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
// Alloys — `id` is the contract `alloy:` / `mating_alloy:` vocabulary of
// §3.17; chromium/molybdenum/nitrogen are that section's composition
// inputs (%), and PREN = %Cr + 3.3·%Mo + 16·%N is shown read-only so the
// pitting-resistance ranking is visible before you run anything.

interface Alloy {
  id:                   string;
  label:                string;
  youngs_modulus:       number;  // Pa
  poisson_ratio:        number;  // -
  yield_strength:       number;  // Pa
  density:              number;  // kg/m³
  chromium_pct:         number;  // %
  molybdenum_pct:       number;  // %
  nitrogen_pct:         number;  // %
}

const ALLOYS: Alloy[] = [
  { id: "316L",      label: "316L stainless",      youngs_modulus: 1.9e11,   poisson_ratio: 0.28,  yield_strength: 5.0e8, density: 7990, chromium_pct: 17.0, molybdenum_pct: 2.5, nitrogen_pct: 0.05 },
  { id: "2507",      label: "2507 super duplex",   youngs_modulus: 2.0e11,   poisson_ratio: 0.27,  yield_strength: 5.5e8, density: 7800, chromium_pct: 25.0, molybdenum_pct: 3.8, nitrogen_pct: 0.27 },
  { id: "NAB",       label: "NAB (Ni-Al bronze)",  youngs_modulus: 1.20e11,  poisson_ratio: 0.33,  yield_strength: 2.5e8, density: 7600, chromium_pct: 0.0,  molybdenum_pct: 0.0, nitrogen_pct: 0.0 },
  { id: "Ti6Al4V",   label: "Ti-6Al-4V",           youngs_modulus: 1.138e11, poisson_ratio: 0.342, yield_strength: 8.8e8, density: 4430, chromium_pct: 0.0,  molybdenum_pct: 0.0, nitrogen_pct: 0.0 },
  { id: "IN625",     label: "Inconel 625",         youngs_modulus: 2.08e11,  poisson_ratio: 0.31,  yield_strength: 4.9e8, density: 8440, chromium_pct: 21.5, molybdenum_pct: 9.0, nitrogen_pct: 0.0 },
  { id: "AlSi10Mg",  label: "AlSi10Mg",            youngs_modulus: 7.0e10,   poisson_ratio: 0.33,  yield_strength: 2.4e8, density: 2670, chromium_pct: 0.0,  molybdenum_pct: 0.0, nitrogen_pct: 0.0 },
  { id: "CuNi90-10", label: "CuNi 90-10",          youngs_modulus: 1.35e11,  poisson_ratio: 0.33,  yield_strength: 1.4e8, density: 8900, chromium_pct: 0.0,  molybdenum_pct: 0.0, nitrogen_pct: 0.0 },
];

/** `mating_alloy: ''` means "no galvanic couple" per §3.17. */
const NO_MATE = "";

// ---------------------------------------------------------------------------
// Hull forms — the §3.16 `hull_type` vocabulary.

interface HullForm {
  id:    string;
  label: string;
  blurb: string;
}

const HULL_FORMS: HullForm[] = [
  { id: "cylinder",                label: "Cylinder",       blurb: "unstiffened shell — classical elastic buckling" },
  { id: "ring_stiffened_cylinder", label: "Ring-stiffened", blurb: "interframe collapse (Windenburg–Trilling) — contract default" },
  { id: "sphere",                  label: "Sphere",         blurb: "p_cr = 2E(t/R)²/√(3(1−ν²)) with knockdown" },
];

// ---------------------------------------------------------------------------

export function MarinePanel({ currentText, onChange }: Props) {
  // Collapsed by default for the same reason as ManufacturingPanel:
  // most pipelines are not pressure-hull work, and the header line
  // (including the "advisory only" badge) stays visible either way.
  const [collapsed, setCollapsed] = useState(true);

  // Depth envelope — §3.15 `depth_factors` is [operating, test, collapse];
  // operating is pinned at 1.0 because that IS the design depth.
  const [designDepth, setDesignDepth]       = useState(300);
  const [testFactor, setTestFactor]         = useState(1.5);
  const [collapseFactor, setCollapseFactor] = useState(2.25);

  // Seawater + service.
  const [seaTemp, setSeaTemp]               = useState(10);
  const [salinity, setSalinity]             = useState(35);
  const [serviceLife, setServiceLife]       = useState(25);

  // Hull.
  const [hullType, setHullType]             = useState("ring_stiffened_cylinder");
  const [diameter, setDiameter]             = useState(1.0);
  const [thickness, setThickness]           = useState(0.012);
  const [frameSpacing, setFrameSpacing]     = useState(0.5);

  // Galvanic couple + protection.
  const [alloyId, setAlloyId]               = useState("316L");
  const [mateId, setMateId]                 = useState(NO_MATE);
  const [cathodic, setCathodic]             = useState(false);

  const alloy = ALLOYS.find(a => a.id === alloyId) ?? ALLOYS[0];
  const rho   = seawaterDensity(salinity, seaTemp);

  // Marine stages are all wired `mesh: { from: … }`; gate on a
  // mesh-producing stage the same way the Materials panel gates on a
  // solver stage.
  const meshReady = useMemo(() => hasMeshStage(currentText), [currentText]);
  const fieldStageId = useMemo(() => findFieldStageId(currentText), [currentText]);

  const ctx: BuildContext = {
    designDepth,
    depthFactors: [1.0, testFactor, collapseFactor],
    seaTemp,
    salinity,
    seawaterDensity: rho,
    serviceLife,
    hullType,
    diameter,
    thickness,
    frameSpacing,
    alloy,
    mateId,
    cathodic,
    fieldStageId,
  };

  const actions: ActionMeta[] = [
    {
      key:   "hydrostatic",
      label: "Add hydrostatic load case",
      hint:  `solver.marine.hydrostatic — ${ctx.depthFactors.length} load cases at ${ctx.depthFactors.map(f => Math.round(designDepth * f)).join(" / ")} m`,
      build: () => hydrostaticStages(ctx),
    },
    {
      key:   "collapse",
      label: "Add collapse check",
      hint:  "solver.marine.hull_collapse — preliminary sizing, NOT a classification-society calculation",
      build: () => collapseStages(ctx),
    },
    {
      key:   "corrosion",
      label: "Add corrosion assessment",
      hint:  "solver.marine.corrosion — indicative literature rates, not design values",
      build: () => corrosionStages(ctx),
    },
    {
      key:   "qualification",
      label: "Add qualification report",
      hint:  "writer.marine.qualification_report — advisory dossier, never approval",
      build: () => qualificationStages(ctx),
    },
  ];

  const run = (specs: StageSpec[]) => {
    if (!meshReady) return;
    onChange(insertPipelineStages(currentText, specs));
  };

  return (
    <section style={wrapStyle} aria-label="Marine">
      <header style={headerStyle}>
        <button
          type="button"
          onClick={() => setCollapsed(c => !c)}
          style={titleButtonStyle}
          aria-expanded={!collapsed}
        >
          <span style={chevronStyle}>{collapsed ? "▸" : "▾"}</span>
          <span style={titleTextStyle}>Marine</span>
          <span style={countStyle}>
            {designDepth} m design · collapse{" "}
            <code style={codeStyle}>{Math.round(designDepth * collapseFactor)} m</code> ·{" "}
            <code style={codeStyle}>{alloy.id}</code>
            {!meshReady && (
              <span style={hintStyle}>needs a mesh stage — add a mesher above</span>
            )}
            <span style={badgeStyle}>advisory only</span>
          </span>
        </button>
      </header>

      {!collapsed && (
        <div style={bodyStyle}>
          {!meshReady && (
            <div style={bannerStyle} role="status">
              <strong>No mesh-producing stage in this pipeline.</strong> Marine
              stages are wired with{" "}
              <code style={bannerCodeStyle}>mesh: {"{ from: … }"}</code>, so there
              is nothing to attach to yet. Add a{" "}
              <code style={bannerCodeStyle}>mesher.*</code> stage from the{" "}
              <strong>Mesher</strong> panel above; these actions unlock the moment
              one exists.
            </div>
          )}

          <div style={groupsStyle}>
            <div style={groupStyle}>
              <div style={groupTitleStyle}>Depth</div>
              <div style={fieldsStyle}>
                <Field
                  id="marine-design-depth"
                  label="design depth (m)"
                  title="contract key design_depth — default 300 m"
                  value={designDepth}
                  step={10}
                  onChange={v => setDesignDepth(v)}
                  def={300}
                />
                <Field
                  id="marine-test-factor"
                  label="test factor (-)"
                  title="depth_factors[1] — default 1.5"
                  value={testFactor}
                  step={0.05}
                  onChange={v => setTestFactor(v)}
                  def={1.5}
                />
                <Field
                  id="marine-collapse-factor"
                  label="collapse factor (-)"
                  title="depth_factors[2] — default 2.25"
                  value={collapseFactor}
                  step={0.05}
                  onChange={v => setCollapseFactor(v)}
                  def={2.25}
                />
                <span style={derivedStyle}>
                  cases: {ctx.depthFactors.map(f => `${Math.round(designDepth * f)} m`).join(" / ")}
                </span>
              </div>
            </div>

            <div style={groupStyle}>
              <div style={groupTitleStyle}>Seawater</div>
              <div style={fieldsStyle}>
                <Field
                  id="marine-sea-temp"
                  label="temperature (°C)"
                  title="contract key seawater_temperature — default 10 °C"
                  value={seaTemp}
                  step={1}
                  onChange={v => setSeaTemp(v)}
                  def={10}
                />
                <Field
                  id="marine-salinity"
                  label="salinity (PSU)"
                  title="contract key salinity_psu — default 35"
                  value={salinity}
                  step={0.5}
                  onChange={v => setSalinity(v)}
                  def={35}
                />
                <span
                  style={derivedStyle}
                  title="One-atmosphere International Equation of State of Seawater 1980 (Millero & Poisson 1981). Valid 0–40 °C, 0–42 PSU, surface pressure; ±0.01 kg/m³ against the reference table. Pressure compressibility is NOT included."
                >
                  ρ = {rho.toFixed(1)} kg/m³ (derived)
                </span>
                <Field
                  id="marine-service-life"
                  label="service life (a)"
                  title="contract key service_life_years — default 25"
                  value={serviceLife}
                  step={1}
                  onChange={v => setServiceLife(v)}
                  def={25}
                />
              </div>
            </div>

            <div style={groupStyle}>
              <div style={groupTitleStyle}>Hull form</div>
              <div style={columnStyle}>
                <div style={chipsStyle}>
                  {HULL_FORMS.map(h => (
                    <button
                      key={h.id}
                      type="button"
                      onClick={() => setHullType(h.id)}
                      style={{ ...chipStyle, ...(h.id === hullType ? chipActiveStyle : null) }}
                      aria-pressed={h.id === hullType}
                      title={h.blurb}
                    >
                      <span style={chipNameStyle}>{h.label}</span>
                      <span style={chipMetaStyle}>{h.id}</span>
                    </button>
                  ))}
                </div>
                <div style={fieldsStyle}>
                  <Field
                    id="marine-diameter"
                    label="diameter (m)"
                    title="contract key diameter — default 1.0 m"
                    value={diameter}
                    step={0.05}
                    onChange={v => setDiameter(v)}
                    def={1.0}
                  />
                  <Field
                    id="marine-thickness"
                    label="thickness (m)"
                    title="contract key thickness — default 0.012 m"
                    value={thickness}
                    step={0.001}
                    onChange={v => setThickness(v)}
                    def={0.012}
                  />
                  <Field
                    id="marine-frame-spacing"
                    label="frame spacing (m)"
                    title="contract key unsupported_length — default 0.5 m"
                    value={frameSpacing}
                    step={0.05}
                    onChange={v => setFrameSpacing(v)}
                    def={0.5}
                  />
                  <span style={derivedStyle}>D/t = {(diameter / Math.max(thickness, 1e-9)).toFixed(0)}</span>
                </div>
              </div>
            </div>

            <div style={groupStyle}>
              <div style={groupTitleStyle}>Alloy</div>
              <div style={columnStyle}>
                <div style={fieldsStyle}>
                  <span style={fieldStyle}>
                    <label htmlFor="marine-alloy" style={labelStyle}>alloy</label>
                    <select
                      id="marine-alloy"
                      value={alloyId}
                      onChange={e => setAlloyId(e.target.value)}
                      style={selectStyle}
                    >
                      {ALLOYS.map(a => (
                        <option key={a.id} value={a.id}>{a.label}</option>
                      ))}
                    </select>
                  </span>
                  <span style={fieldStyle}>
                    <label htmlFor="marine-mating-alloy" style={labelStyle}>mating alloy</label>
                    <select
                      id="marine-mating-alloy"
                      value={mateId}
                      onChange={e => setMateId(e.target.value)}
                      style={selectStyle}
                      title="The other half of the galvanic couple; 'none' writes mating_alloy: '' (no couple)."
                    >
                      <option value={NO_MATE}>none</option>
                      {ALLOYS.map(a => (
                        <option key={a.id} value={a.id}>{a.label}</option>
                      ))}
                    </select>
                  </span>
                  <span style={fieldStyle}>
                    <input
                      id="marine-cathodic"
                      type="checkbox"
                      checked={cathodic}
                      onChange={e => setCathodic(e.target.checked)}
                      style={checkboxStyle}
                    />
                    <label htmlFor="marine-cathodic" style={labelStyle}>
                      cathodic protection
                    </label>
                  </span>
                </div>
                <dl style={readoutStyle} aria-label={`${alloy.label} properties (read-only)`}>
                  <Readout term="E"    value={`${num(alloy.youngs_modulus)} Pa`} />
                  <Readout term="ν"    value={num(alloy.poisson_ratio)} />
                  <Readout term="σ_y"  value={`${num(alloy.yield_strength)} Pa`} />
                  <Readout term="ρ"    value={`${num(alloy.density)} kg/m³`} />
                  <Readout term="%Cr"  value={num(alloy.chromium_pct)} />
                  <Readout term="%Mo"  value={num(alloy.molybdenum_pct)} />
                  <Readout term="%N"   value={num(alloy.nitrogen_pct)} />
                  <Readout term="PREN" value={pren(alloy).toFixed(1)} />
                </dl>
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
                    style={{ ...actionStyle, ...(!meshReady ? actionDisabledStyle : null) }}
                    title={meshReady ? a.hint : "Add a mesh-producing stage first"}
                  >
                    {a.label}
                  </button>
                ))}
              </div>
            </div>
          </div>

          <div style={advisoryStyle} role="note">
            <strong>Advisory only — not class approval.</strong> The collapse
            margin is preliminary sizing from closed-form interframe /
            membrane-yield / sphere-buckling expressions with blanket
            imperfection and AM-anisotropy knockdowns; the qualification report
            is a checklist derived from publicly documented AM qualification
            practice (e.g. DNV-ST-B203, ABS AM guidance). souxmar is not a
            classification society, corrosion rates are indicative literature
            values rather than design values, and nothing this panel writes
            constitutes approval of a pressure hull.
          </div>
        </div>
      )}
    </section>
  );
}

// ---------------------------------------------------------------------------
// Small labelled-number control. Keeps the label/input association
// explicit so the field is reachable and announced.

function Field({
  id,
  label,
  title,
  value,
  step,
  def,
  onChange,
}: {
  id:       string;
  label:    string;
  title:    string;
  value:    number;
  step:     number;
  def:      number;
  onChange: (v: number) => void;
}) {
  return (
    <span style={fieldStyle}>
      <label htmlFor={id} style={labelStyle}>{label}</label>
      <input
        id={id}
        type="number"
        step={step}
        value={value}
        onChange={e => {
          const n = Number(e.target.value);
          onChange(Number.isFinite(n) ? n : def);
        }}
        style={numInputStyle}
        title={title}
      />
      <span style={defaultStyle}>def {def}</span>
    </span>
  );
}

// `dl > div > (dt, dd)` is the valid HTML5 grouping wrapper.
function Readout({ term, value }: { term: string; value: string }) {
  return (
    <div style={readoutItemStyle}>
      <dt style={readoutTermStyle}>{term}</dt>
      <dd style={readoutValueStyle}>{value}</dd>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Stage construction — contract §3.15–§3.18, keys in contract order.

interface BuildContext {
  designDepth:     number;
  depthFactors:    number[];
  seaTemp:         number;
  salinity:        number;
  seawaterDensity: number;
  serviceLife:     number;
  hullType:        string;
  diameter:        number;
  thickness:       number;
  frameSpacing:    number;
  alloy:           Alloy;
  mateId:          string;
  cathodic:        boolean;
  fieldStageId:    string | null;
}

interface ActionMeta {
  key:   string;
  label: string;
  hint:  string;
  build: () => StageSpec[];
}

function hydrostaticStages(ctx: BuildContext): StageSpec[] {
  return [
    {
      id:     "marine-hydrostatic",
      plugin: "solver.marine.hydrostatic",
      note:   "one load case per depth factor: operating / test / collapse",
      input:  [
        { key: "mesh",                 value: "{ from: $MESH }" },
        { key: "design_depth",         value: num(ctx.designDepth) },
        { key: "depth_factors",        value: `[${ctx.depthFactors.map(num).join(", ")}]` },
        { key: "seawater_density",     value: round1(ctx.seawaterDensity) },
        { key: "salinity_psu",         value: num(ctx.salinity) },
        { key: "seawater_temperature", value: num(ctx.seaTemp) },
        { key: "gravity",              value: "9.80665" },
        { key: "include_atmospheric",  value: "false" },
        { key: "atmospheric_pressure", value: "101325" },
        { key: "build_direction",      value: "[0, 0, 1]" },
      ],
    },
  ];
}

function collapseStages(ctx: BuildContext): StageSpec[] {
  const a = ctx.alloy;
  return [
    {
      id:     "marine-collapse",
      plugin: "solver.marine.hull_collapse",
      note:   "PRELIMINARY sizing — not a classification-society calculation",
      input:  [
        { key: "mesh",                    value: "{ from: $MESH }" },
        { key: "hull_type",               value: `'${ctx.hullType}'` },
        { key: "diameter",                value: num(ctx.diameter) },
        { key: "thickness",               value: num(ctx.thickness) },
        { key: "unsupported_length",      value: num(ctx.frameSpacing) },
        { key: "youngs_modulus",          value: num(a.youngs_modulus) },
        { key: "poisson_ratio",           value: num(a.poisson_ratio) },
        { key: "yield_strength",          value: num(a.yield_strength) },
        { key: "design_depth",            value: num(ctx.designDepth) },
        { key: "seawater_density",        value: round1(ctx.seawaterDensity) },
        { key: "gravity",                 value: "9.80665" },
        { key: "imperfection_knockdown",  value: "0.75" },
        { key: "am_anisotropy_knockdown", value: "0.90" },
        { key: "safety_factor",           value: "1.5" },
      ],
    },
  ];
}

function corrosionStages(ctx: BuildContext): StageSpec[] {
  const a = ctx.alloy;
  return [
    {
      id:     "marine-corrosion",
      plugin: "solver.marine.corrosion",
      note:   "indicative literature rates, not design values",
      input:  [
        { key: "mesh",                    value: "{ from: $MESH }" },
        { key: "alloy",                   value: `'${a.id}'` },
        { key: "chromium_pct",            value: num(a.chromium_pct) },
        { key: "molybdenum_pct",          value: num(a.molybdenum_pct) },
        { key: "nitrogen_pct",            value: num(a.nitrogen_pct) },
        { key: "mating_alloy",            value: `'${ctx.mateId}'` },
        { key: "area_ratio_cathode_anode", value: "1.0" },
        { key: "seawater_temperature",    value: num(ctx.seaTemp) },
        { key: "salinity_psu",            value: num(ctx.salinity) },
        { key: "flow_velocity",           value: "0.5" },
        { key: "oxygen_mg_per_l",         value: "8.0" },
        { key: "service_life_years",      value: num(ctx.serviceLife) },
        { key: "cathodic_protection",     value: ctx.cathodic ? "true" : "false" },
        { key: "coating_efficiency",      value: "0.0" },
        { key: "as_built_surface",        value: "true" },
      ],
    },
  ];
}

function qualificationStages(ctx: BuildContext): StageSpec[] {
  return [
    {
      id:     "marine-qualification",
      plugin: "writer.marine.qualification_report",
      note:   "ADVISORY dossier — souxmar is not a classification society",
      input:  [
        { key: "mesh", value: "{ from: $MESH }" },
        // `field` is optional for writer.*; wire the last field producer
        // in the document so the dossier has simulation evidence to
        // render, else omit it and let the writer say "no field supplied".
        ...(ctx.fieldStageId
          ? [{ key: "field", value: `{ from: ${ctx.fieldStageId} }` }]
          : []),
        { key: "path",                value: "marine-qualification.md" },
        { key: "part_name",           value: "'AM part'" },
        { key: "process",             value: "'lpbf'" },
        { key: "alloy",               value: `'${ctx.alloy.id}'` },
        { key: "application",         value: "'structural'" },
        { key: "criticality",         value: "2" },
        { key: "design_depth",        value: num(ctx.designDepth) },
        { key: "service_life_years",  value: num(ctx.serviceLife) },
        { key: "class_framework",     value: "'generic'" },
        { key: "redundancy",          value: "false" },
      ],
    },
  ];
}

// ---------------------------------------------------------------------------
// Numerics.

/** PREN = %Cr + 3.3·%Mo + 16·%N (contract §3.17). */
function pren(a: Alloy): number {
  return a.chromium_pct + 3.3 * a.molybdenum_pct + 16 * a.nitrogen_pct;
}

/**
 * One-atmosphere seawater density from the International Equation of
 * State of Seawater 1980 (Millero & Poisson 1981): pure-water
 * polynomial in T (°C) plus the haline terms in S (PSU).
 *
 * Validity: 0–40 °C, 0–42 PSU, surface pressure; ~±0.01 kg/m³ against
 * the reference table. Pressure compressibility is NOT included, so
 * this is the surface density, not the in-situ density at depth — the
 * hydrostatic solver uses the same constant-ρ assumption (§3.15).
 */
function seawaterDensity(salinityPsu: number, temperatureC: number): number {
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

function clamp(v: number, lo: number, hi: number): number {
  if (!Number.isFinite(v)) return lo;
  return v < lo ? lo : v > hi ? hi : v;
}

/** YAML scalar formatting — same rule as ManufacturingPanel: plain
 *  decimals where readable, exponential for the very large / very
 *  small, and the mantissa always keeps a decimal point so `2.0e-4` is
 *  a float to every YAML parser (a bare `2e-4` is a *string* under a
 *  strict YAML 1.1 resolver). */
function num(v: number): string {
  if (Number.isInteger(v) && Math.abs(v) < 1e7) return String(v);
  if (Math.abs(v) >= 1e4 || (v !== 0 && Math.abs(v) < 1e-3)) {
    const [mantissa, exponent] = v.toExponential().replace(/e\+?/, "e").split("e");
    return `${mantissa.includes(".") ? mantissa : `${mantissa}.0`}e${exponent}`;
  }
  return String(v);
}

function round1(v: number): string {
  return num(Math.round(v * 10) / 10);
}

// ---------------------------------------------------------------------------
// Styles — dim theme, same grammar as Solvers / Materials / BCs /
// Manufacturing. Kept side-by-side with those panels on purpose.

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
  background:     "var(--warning-soft, rgba(255, 212, 0, 0.12))",
  color:          "var(--warning, #ffd43b)",
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
  maxHeight:      300,
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

const advisoryStyle: CSSProperties = {
  marginTop:      8,
  padding:        "8px 10px",
  borderLeft:     "3px solid var(--danger, #f5365c)",
  background:     "var(--danger-soft, rgba(244, 33, 46, 0.10))",
  borderRadius:   "var(--radius-sm, 4px)",
  fontSize:       10.5,
  color:          "var(--fg-secondary)",
  lineHeight:     1.5,
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
  alignItems:     "center",
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

const selectStyle: CSSProperties = {
  padding:        "2px 4px",
  background:     "var(--bg-canvas)",
  color:          "var(--fg-primary)",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm)",
  fontSize:       11,
  fontFamily:     "inherit",
};

const checkboxStyle: CSSProperties = {
  width:          13,
  height:         13,
  accentColor:    "var(--accent-default, #1d9bf0)",
  cursor:         "pointer",
};

const derivedStyle: CSSProperties = {
  fontSize:       10,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  color:          "var(--fg-secondary)",
  cursor:         "help",
  whiteSpace:     "nowrap",
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
