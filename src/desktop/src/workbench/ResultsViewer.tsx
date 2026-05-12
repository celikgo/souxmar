// SPDX-License-Identifier: Apache-2.0
//
// Results viewer — three.js scene for souxmar's `writer.vtu` output
// files with per-point field colormap rendering. Distinct from
// ModelViewer (which is geometry-only with a clay material) because
// the result-viewing workflow is fundamentally different: the user
// expects to see *which physics field* is plotted, switch between
// fields, and read off magnitudes from a color legend.
//
// Today this handles the single-frame VTU case. Multi-frame .pvd
// playback is RFC-006's territory (Sprint 32) — when that lands, the
// field-picker + colormap surface here gets a playback strip below it
// without otherwise changing.

import { useEffect, useRef, useState } from "react";
import type { CSSProperties } from "react";
import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { parseVtuWithFields, type VtuField } from "./vtuReader";
import { invokeCommand } from "../tauri/bridge";

interface Props {
  projectPath: string;
  /** Path of the .vtu file relative to the project root. */
  relPath:     string;
}

export function ResultsViewer({ projectPath, relPath }: Props) {
  const mountRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<{
    scene:    THREE.Scene;
    renderer: THREE.WebGLRenderer;
    camera:   THREE.PerspectiveCamera;
    controls: OrbitControls;
    mesh:     THREE.Mesh | null;
    cleanup:  () => void;
  } | null>(null);

  const [fields,   setFields]   = useState<VtuField[]>([]);
  const [activeIdx, setActive]  = useState<number>(-1);  // -1 = clay/no-field
  const [error,    setError]    = useState<string | null>(null);
  const [meta,     setMeta]     = useState<{ tris: number; verts: number } | null>(null);

  // Scene setup — kept light. Hemisphere + a key directional + ambient.
  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) return;

    const width  = mount.clientWidth  || 800;
    const height = mount.clientHeight || 600;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x15202b);

    const camera = new THREE.PerspectiveCamera(45, width / height, 0.01, 10000);
    camera.position.set(3, 3, 3);

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.setSize(width, height);
    mount.appendChild(renderer.domElement);

    const hemi = new THREE.HemisphereLight(0xa0c4ff, 0x1b3a52, 0.85);
    const key  = new THREE.DirectionalLight(0xffffff, 0.7);
    key.position.set(5, 10, 7);
    const ambient = new THREE.AmbientLight(0xffffff, 0.25);
    scene.add(hemi, key, ambient);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.08;

    let raf = 0;
    const tick = () => {
      raf = requestAnimationFrame(tick);
      controls.update();
      renderer.render(scene, camera);
    };
    tick();

    const onResize = () => {
      const w = mount.clientWidth || 800;
      const h = mount.clientHeight || 600;
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
      renderer.setSize(w, h);
    };
    window.addEventListener("resize", onResize);

    sceneRef.current = {
      scene, renderer, camera, controls, mesh: null,
      cleanup: () => {
        cancelAnimationFrame(raf);
        window.removeEventListener("resize", onResize);
        controls.dispose();
        renderer.dispose();
        if (renderer.domElement.parentElement) {
          renderer.domElement.parentElement.removeChild(renderer.domElement);
        }
      },
    };
    return () => sceneRef.current?.cleanup();
  }, []);

  // Load the file whenever projectPath / relPath changes.
  useEffect(() => {
    let cancelled = false;
    setFields([]);
    setActive(-1);
    setError(null);
    setMeta(null);

    invokeCommand<number[]>("read_geometry_bytes", { projectPath, relPath })
      .then(bytes => {
        if (cancelled) return;
        const text = new TextDecoder().decode(new Uint8Array(bytes));
        const parsed = parseVtuWithFields(text);
        if (cancelled) return;
        const s = sceneRef.current;
        if (!s) return;

        // Remove the previous mesh if any.
        if (s.mesh) {
          s.scene.remove(s.mesh);
          const mat = s.mesh.material;
          if (Array.isArray(mat)) mat.forEach(m => m.dispose());
          else mat.dispose();
          s.mesh.geometry.dispose();
        }

        const mat = new THREE.MeshStandardMaterial({
          color:       0x6ecbff,
          metalness:   0.05,
          roughness:   0.6,
          flatShading: false,
          side:        THREE.DoubleSide,
          vertexColors: false,
        });
        const mesh = new THREE.Mesh(parsed.geometry, mat);
        s.scene.add(mesh);
        s.mesh = mesh;

        // Fit camera.
        const bbox = new THREE.Box3().setFromObject(mesh);
        const size = bbox.getSize(new THREE.Vector3());
        const center = bbox.getCenter(new THREE.Vector3());
        const radius = Math.max(0.5, size.length() * 0.6);
        s.camera.position.copy(center).add(new THREE.Vector3(radius, radius, radius));
        s.camera.near = Math.max(0.001, radius / 1000);
        s.camera.far  = radius * 1000;
        s.camera.updateProjectionMatrix();
        s.controls.target.copy(center);

        setFields(parsed.fields);
        // Auto-pick the first field if any.
        setActive(parsed.fields.length > 0 ? 0 : -1);
        const indexArray = parsed.geometry.getIndex();
        setMeta({
          tris:  indexArray ? indexArray.count / 3 : 0,
          verts: parsed.numPoints,
        });
      })
      .catch(err => {
        if (!cancelled) setError(String(err));
      });

    return () => { cancelled = true; };
  }, [projectPath, relPath]);

  // Apply the chosen field as vertex colors whenever it changes.
  useEffect(() => {
    const s = sceneRef.current;
    if (!s || !s.mesh) return;
    const mat = s.mesh.material as THREE.MeshStandardMaterial;
    const geom = s.mesh.geometry;
    if (activeIdx < 0 || !fields[activeIdx]) {
      geom.deleteAttribute("color");
      mat.vertexColors = false;
      mat.color.setHex(0x6ecbff);
      mat.needsUpdate = true;
      return;
    }
    const f = fields[activeIdx];
    const colors = new Float32Array(f.magnitude.length * 3);
    const span = f.max - f.min;
    for (let i = 0; i < f.magnitude.length; i++) {
      const t = span > 1e-12 ? (f.magnitude[i] - f.min) / span : 0;
      const [r, g, b] = viridis(t);
      colors[i * 3] = r;
      colors[i * 3 + 1] = g;
      colors[i * 3 + 2] = b;
    }
    geom.setAttribute("color", new THREE.BufferAttribute(colors, 3));
    mat.color.setHex(0xffffff); // vertex colors multiply with this; white = use them as-is
    mat.vertexColors = true;
    mat.needsUpdate = true;
  }, [activeIdx, fields]);

  const activeField = activeIdx >= 0 ? fields[activeIdx] : null;

  return (
    <div style={wrapStyle}>
      <header style={headerStyle}>
        <span style={pathStyle}>{relPath}</span>
        <div style={headerRightStyle}>
          {meta && (
            <span style={metaStyle}>{meta.tris.toLocaleString()} tris · {meta.verts.toLocaleString()} verts</span>
          )}
          {fields.length > 0 && (
            <>
              <label style={labelStyle}>Field</label>
              <select
                value={String(activeIdx)}
                onChange={e => setActive(Number(e.target.value))}
                style={selectStyle}
              >
                <option value={-1}>— none —</option>
                {fields.map((f, i) => (
                  <option key={i} value={String(i)}>
                    {f.name}
                    {f.components === 3 ? " (|v|)" : ""}
                  </option>
                ))}
              </select>
            </>
          )}
        </div>
      </header>

      <div style={canvasWrapStyle}>
        <div ref={mountRef} style={canvasStyle} />
        {error && <div style={errorStyle}>{error}</div>}
        {activeField && <ColorbarLegend field={activeField} />}
        {fields.length === 0 && !error && (
          <div style={hintStyle}>
            No PointData fields in this file. Geometry only.
          </div>
        )}
      </div>
    </div>
  );
}

// ---------------------------------------------------------------------------
// Color legend — small bottom-left overlay.

function ColorbarLegend({ field }: { field: VtuField }) {
  // Build a CSS gradient sampling the viridis colormap at 12 stops.
  const stops: string[] = [];
  for (let i = 0; i <= 12; i++) {
    const t = i / 12;
    const [r, g, b] = viridis(t);
    stops.push(
      `rgb(${Math.round(r * 255)}, ${Math.round(g * 255)}, ${Math.round(b * 255)}) ${Math.round(t * 100)}%`,
    );
  }
  return (
    <div style={legendStyle}>
      <div style={legendNameStyle}>{field.name}</div>
      <div style={{ ...legendBarStyle, background: `linear-gradient(to top, ${stops.join(", ")})` }} />
      <div style={legendTicksStyle}>
        <span>{formatTick(field.max)}</span>
        <span>{formatTick((field.min + field.max) / 2)}</span>
        <span>{formatTick(field.min)}</span>
      </div>
    </div>
  );
}

function formatTick(v: number): string {
  if (v === 0) return "0";
  const a = Math.abs(v);
  if (a >= 1e4 || a < 1e-2) return v.toExponential(2);
  return v.toFixed(3);
}

// ---------------------------------------------------------------------------
// Viridis colormap — perceptually uniform, color-blind friendly.
// Eight-stop piecewise-linear approximation from the matplotlib LUT.
// Input t ∈ [0, 1]; output [r, g, b] each in [0, 1].

const VIRIDIS_STOPS: ReadonlyArray<readonly [number, number, number]> = [
  [0.267004, 0.004874, 0.329415],   // 0.000
  [0.282623, 0.140926, 0.457517],   // 0.125
  [0.253935, 0.265254, 0.529983],   // 0.250
  [0.206756, 0.371758, 0.553117],   // 0.375
  [0.163625, 0.471133, 0.558148],   // 0.500
  [0.127568, 0.566949, 0.550556],   // 0.625
  [0.134692, 0.658636, 0.517649],   // 0.750
  [0.266941, 0.748751, 0.440573],   // 0.875
  [0.477504, 0.821444, 0.318195],   // 1.000 (overshoot; we clamp below)
];

function viridis(t: number): [number, number, number] {
  if (!(t >= 0)) t = 0;
  if (t > 1) t = 1;
  const segments = VIRIDIS_STOPS.length - 1;
  const f = t * segments;
  const i = Math.min(segments - 1, Math.floor(f));
  const u = f - i;
  const a = VIRIDIS_STOPS[i];
  const b = VIRIDIS_STOPS[i + 1];
  return [
    a[0] + (b[0] - a[0]) * u,
    a[1] + (b[1] - a[1]) * u,
    a[2] + (b[2] - a[2]) * u,
  ];
}

// ---------------------------------------------------------------------------
// Styles — dim theme.

const wrapStyle: CSSProperties = {
  display:        "flex",
  flexDirection:  "column",
  height:         "100%",
  background:     "var(--bg-canvas)",
  color:          "var(--fg-primary)",
  overflow:       "hidden",
};

const headerStyle: CSSProperties = {
  display:        "flex",
  alignItems:     "center",
  justifyContent: "space-between",
  height:         32,
  padding:        "0 var(--space-3)",
  borderBottom:   "1px solid var(--border-subtle)",
  background:     "var(--bg-panel)",
  fontSize:       11,
  color:          "var(--fg-tertiary)",
  flexShrink:     0,
  gap:            12,
};

const pathStyle: CSSProperties = {
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
  whiteSpace:     "nowrap",
  overflow:       "hidden",
  textOverflow:   "ellipsis",
  minWidth:       0,
};

const headerRightStyle: CSSProperties = {
  display:    "flex",
  alignItems: "center",
  gap:        8,
  flexShrink: 0,
};

const metaStyle: CSSProperties = {
  whiteSpace: "nowrap",
};

const labelStyle: CSSProperties = {
  color:          "var(--fg-tertiary)",
  textTransform:  "uppercase",
  fontSize:       9,
  letterSpacing:  0.5,
};

const selectStyle: CSSProperties = {
  padding:        "2px 6px",
  background:     "var(--bg-canvas)",
  color:          "var(--fg-primary)",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm, 4px)",
  fontSize:       11,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
};

const canvasWrapStyle: CSSProperties = {
  flex:           1,
  position:       "relative",
  overflow:       "hidden",
};

const canvasStyle: CSSProperties = {
  width:          "100%",
  height:         "100%",
};

const errorStyle: CSSProperties = {
  position:       "absolute",
  top:            8,
  left:           8,
  right:          8,
  padding:        "6px 10px",
  background:     "rgba(244, 33, 46, 0.12)",
  border:         "1px solid rgba(244, 33, 46, 0.4)",
  borderRadius:   "var(--radius-sm)",
  color:          "#f4212e",
  fontSize:       11,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
};

const hintStyle: CSSProperties = {
  position:       "absolute",
  bottom:         8,
  left:           8,
  padding:        "4px 8px",
  background:     "rgba(0,0,0,0.45)",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm)",
  color:          "var(--fg-tertiary)",
  fontSize:       11,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
};

const legendStyle: CSSProperties = {
  position:       "absolute",
  left:           12,
  bottom:         12,
  display:        "flex",
  flexDirection:  "column",
  gap:            6,
  background:     "rgba(0,0,0,0.45)",
  padding:        "8px 10px",
  border:         "1px solid var(--border-subtle)",
  borderRadius:   "var(--radius-sm)",
};

const legendNameStyle: CSSProperties = {
  fontSize:       10,
  textTransform:  "uppercase",
  letterSpacing:  0.5,
  color:          "var(--fg-tertiary)",
};

const legendBarStyle: CSSProperties = {
  width:          18,
  height:         140,
  borderRadius:   2,
  border:         "1px solid rgba(255,255,255,0.15)",
};

const legendTicksStyle: CSSProperties = {
  position:       "absolute",
  right:          -54,
  bottom:         18,
  width:          50,
  height:         140,
  display:        "flex",
  flexDirection:  "column",
  justifyContent: "space-between",
  color:          "var(--fg-primary)",
  fontSize:       10,
  fontFamily:     "var(--font-mono, ui-monospace, SFMono-Regular, Menlo, monospace)",
};

// ---------------------------------------------------------------------------
// Extension membership.

const RESULT_EXTS = new Set(["vtu", "pvd"]);

export function isResultsPath(relPath: string): boolean {
  const lower = relPath.toLowerCase();
  const dot = lower.lastIndexOf(".");
  if (dot < 0) return false;
  return RESULT_EXTS.has(lower.slice(dot + 1));
}
