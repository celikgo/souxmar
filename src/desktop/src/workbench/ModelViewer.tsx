// SPDX-License-Identifier: Apache-2.0
//
// Frontend-only 3D model viewer for the workbench. Reads the geometry
// file straight off disk via the `read_geometry_bytes` Tauri command,
// parses it with three.js's bundled OBJLoader / STLLoader, and renders
// it in a WebGL canvas with orbit controls.
//
// This is intentionally a "vanilla" three.js viewer (no react-three-fiber)
// so it sits in a single file with no extra deps. The Sprint 12+ FFI-backed
// viewport is a separate, larger project (VTU streaming + WebGPU); this
// one ships today so imported geometry isn't invisible.

import { useEffect, useRef, useState } from "react";
import type { CSSProperties } from "react";
import * as THREE from "three";
import { OBJLoader } from "three/addons/loaders/OBJLoader.js";
import { STLLoader } from "three/addons/loaders/STLLoader.js";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import { SimplifyModifier } from "three/addons/modifiers/SimplifyModifier.js";
import { invokeCommand } from "../tauri/bridge";

interface Props {
  projectPath:    string;
  /** Path of the geometry file relative to the project root (e.g. "geometry/cube.stl"). */
  relPath:        string;
  /** Optional overlay nodes — used by the BC panel to draw force arrows on top of the scene. */
  overlays?:      Overlay[];
  /** Called when the user simplifies the mesh and saves it; the workbench
   *  swaps the active model to the new path. */
  onSimplified?:  (newRelPath: string) => void;
}

export type Overlay =
  | { kind: "force"; face: BboxFace; vector: [number, number, number]; magnitude: number }
  | { kind: "fixed"; face: BboxFace };

export type BboxFace = "+x" | "-x" | "+y" | "-y" | "+z" | "-z";

export function ModelViewer({ projectPath, relPath, overlays, onSimplified }: Props) {
  const mountRef = useRef<HTMLDivElement>(null);
  const sceneRef = useRef<{
    scene: THREE.Scene;
    renderer: THREE.WebGLRenderer;
    camera: THREE.PerspectiveCamera;
    controls: OrbitControls;
    bbox: THREE.Box3;
    overlayGroup: THREE.Group;
    cleanup: () => void;
  } | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [stats, setStats] = useState<{ tris: number; verts: number } | null>(null);
  const [simplifyOpen, setSimplifyOpen] = useState(false);
  const [simplifyRatio, setSimplifyRatio] = useState(0.5);
  const [simplifyBusy, setSimplifyBusy] = useState(false);

  // One-time scene setup. The renderer/camera/controls live across model
  // changes; only the mesh inside `objectGroup` swaps when relPath changes.
  useEffect(() => {
    const mount = mountRef.current;
    if (!mount) return;

    const width = mount.clientWidth || 800;
    const height = mount.clientHeight || 600;

    const scene = new THREE.Scene();
    scene.background = new THREE.Color(0x15202b);

    const camera = new THREE.PerspectiveCamera(45, width / height, 0.01, 10000);
    camera.position.set(3, 3, 3);

    const renderer = new THREE.WebGLRenderer({ antialias: true });
    renderer.setPixelRatio(window.devicePixelRatio);
    renderer.setSize(width, height);
    mount.appendChild(renderer.domElement);

    // Soft three-point lighting. Hemisphere ambient + a key directional
    // from above-camera gives a usable shape-read on grey clay material.
    const hemi = new THREE.HemisphereLight(0xa0c4ff, 0x1b3a52, 0.9);
    const key = new THREE.DirectionalLight(0xffffff, 0.8);
    key.position.set(5, 10, 7);
    scene.add(hemi, key);

    const grid = new THREE.GridHelper(10, 10, 0x38444d, 0x1e2933);
    scene.add(grid);

    const axes = new THREE.AxesHelper(1.5);
    scene.add(axes);

    const overlayGroup = new THREE.Group();
    scene.add(overlayGroup);

    const controls = new OrbitControls(camera, renderer.domElement);
    controls.enableDamping = true;
    controls.dampingFactor = 0.1;
    controls.target.set(0, 0, 0);
    controls.update();

    let raf = 0;
    function loop() {
      controls.update();
      renderer.render(scene, camera);
      raf = requestAnimationFrame(loop);
    }
    raf = requestAnimationFrame(loop);

    const observed = mount;
    function onResize() {
      const w = observed.clientWidth || 800;
      const h = observed.clientHeight || 600;
      renderer.setSize(w, h);
      camera.aspect = w / h;
      camera.updateProjectionMatrix();
    }
    const ro = new ResizeObserver(onResize);
    ro.observe(observed);

    sceneRef.current = {
      scene,
      renderer,
      camera,
      controls,
      bbox: new THREE.Box3(),
      overlayGroup,
      cleanup: () => {
        cancelAnimationFrame(raf);
        ro.disconnect();
        controls.dispose();
        renderer.dispose();
        if (renderer.domElement.parentElement === mount) {
          mount.removeChild(renderer.domElement);
        }
      },
    };

    return () => {
      sceneRef.current?.cleanup();
      sceneRef.current = null;
    };
  }, []);

  // Re-load whenever the active file changes.
  useEffect(() => {
    if (!projectPath || !relPath) return;
    const ref = sceneRef.current;
    if (!ref) return;

    let disposed = false;
    setError(null);
    setStats(null);

    invokeCommand<number[]>("read_geometry_bytes", {
      projectPath,
      relPath,
    })
      .then(bytes => {
        if (disposed || !sceneRef.current) return;
        const buf = new Uint8Array(bytes).buffer;
        const lower = relPath.toLowerCase();
        let mesh: THREE.Object3D | null = null;
        if (lower.endsWith(".obj")) {
          const text = new TextDecoder().decode(buf);
          mesh = new OBJLoader().parse(text);
        } else if (lower.endsWith(".stl")) {
          const geom = new STLLoader().parse(buf);
          geom.computeVertexNormals();
          const mat = new THREE.MeshStandardMaterial({
            color: 0x6ecbff,
            metalness: 0.1,
            roughness: 0.55,
            flatShading: false,
            side: THREE.DoubleSide,
          });
          mesh = new THREE.Mesh(geom, mat);
        } else {
          setError(`No viewer for ${relPath} yet. Today only .obj and .stl render here.`);
          return;
        }

        if (!mesh) return;

        // Apply a consistent clay material to OBJ groups (they may carry
        // their own materials we don't want here).
        mesh.traverse(c => {
          const m = c as THREE.Mesh;
          if (m.isMesh) {
            m.material = new THREE.MeshStandardMaterial({
              color: 0x6ecbff,
              metalness: 0.1,
              roughness: 0.55,
              flatShading: false,
              side: THREE.DoubleSide,
            });
          }
        });

        // Centre + scale the model so any input fits the 10-unit grid.
        const bbox = new THREE.Box3().setFromObject(mesh);
        const size = new THREE.Vector3();
        bbox.getSize(size);
        const centre = new THREE.Vector3();
        bbox.getCenter(centre);
        const maxDim = Math.max(size.x, size.y, size.z) || 1;
        const scale = 4 / maxDim;
        mesh.position.copy(centre).multiplyScalar(-scale);
        mesh.scale.setScalar(scale);

        // Replace any previous mesh.
        const tagged = sceneRef.current.scene.getObjectByName("model");
        if (tagged) sceneRef.current.scene.remove(tagged);
        mesh.name = "model";
        sceneRef.current.scene.add(mesh);

        // Recompute world-space bbox for overlay placement.
        const worldBbox = new THREE.Box3().setFromObject(mesh);
        sceneRef.current.bbox = worldBbox;

        // Aim the camera at the model.
        const wsize = new THREE.Vector3();
        worldBbox.getSize(wsize);
        const wcentre = new THREE.Vector3();
        worldBbox.getCenter(wcentre);
        const r = wsize.length() || 4;
        sceneRef.current.camera.position.copy(wcentre).add(new THREE.Vector3(r, r * 0.8, r));
        sceneRef.current.controls.target.copy(wcentre);
        sceneRef.current.controls.update();

        // Stats — count triangles by walking the geometry attributes.
        let tris = 0;
        let verts = 0;
        mesh.traverse(c => {
          const m = c as THREE.Mesh;
          if (m.isMesh && m.geometry) {
            const pos = m.geometry.attributes.position;
            if (pos) verts += pos.count;
            tris += (m.geometry.index?.count ?? pos?.count ?? 0) / 3;
          }
        });
        setStats({ tris: Math.round(tris), verts });
      })
      .catch(e => {
        if (!disposed) setError(String(e));
      });

    return () => {
      disposed = true;
    };
  }, [projectPath, relPath]);

  // Re-render overlays whenever they change.
  useEffect(() => {
    const ref = sceneRef.current;
    if (!ref) return;
    const grp = ref.overlayGroup;
    while (grp.children.length > 0) {
      const c = grp.children[0];
      grp.remove(c);
      if ((c as THREE.Mesh).geometry) (c as THREE.Mesh).geometry.dispose();
    }
    if (!overlays || overlays.length === 0 || ref.bbox.isEmpty()) return;
    const bbox = ref.bbox;
    for (const ov of overlays) {
      const pos = faceCentre(bbox, ov.face);
      if (ov.kind === "force") {
        // Force arrow: origin at the face centre, direction from `vector`,
        // length scaled to bbox extent so the arrow stays visible.
        const dir = new THREE.Vector3(...ov.vector);
        if (dir.lengthSq() === 0) continue;
        const norm = dir.clone().normalize();
        const len = bbox.getSize(new THREE.Vector3()).length() * 0.35;
        const arrow = new THREE.ArrowHelper(norm, pos, len, 0xf85149, len * 0.2, len * 0.12);
        grp.add(arrow);
      } else {
        // Fixed BC: a flat semi-transparent disc at the face centre.
        const r = bbox.getSize(new THREE.Vector3()).length() * 0.06;
        const geom = new THREE.SphereGeometry(r, 16, 12);
        const mat = new THREE.MeshBasicMaterial({
          color: 0x6ecbff,
          opacity: 0.55,
          transparent: true,
        });
        const m = new THREE.Mesh(geom, mat);
        m.position.copy(pos);
        grp.add(m);
      }
    }
  }, [overlays]);

  async function onSimplify() {
    const ref = sceneRef.current;
    if (!ref) return;
    const model = ref.scene.getObjectByName("model");
    if (!model) return;

    setSimplifyBusy(true);
    setError(null);
    try {
      // Walk the model, merge into a single BufferGeometry of triangles,
      // then run SimplifyModifier with a target removal count derived
      // from the slider ratio (1.0 = keep all, 0.0 = collapse to nothing).
      const mod = new SimplifyModifier();
      let totalRemoved = 0;
      let newTris = 0;
      let newVerts = 0;
      const objLines: string[] = ["# Simplified by souxmar workbench (three.js SimplifyModifier)"];
      let vOffset = 1; // OBJ indices are 1-based

      model.traverse(c => {
        const m = c as THREE.Mesh;
        if (!m.isMesh || !m.geometry) return;
        // SimplifyModifier needs a non-indexed BufferGeometry.
        const src = m.geometry.toNonIndexed();
        const startVerts = src.attributes.position.count;
        const targetRemove = Math.floor(startVerts * (1 - simplifyRatio));
        const simplified = targetRemove > 0 ? mod.modify(src, targetRemove) : src;
        totalRemoved += startVerts - simplified.attributes.position.count;

        // Replace in scene.
        m.geometry.dispose();
        m.geometry = simplified;

        // Append to OBJ string. Position attribute is in *model-local* coords;
        // we apply the mesh's world transform so the saved file matches
        // visual world bounds.
        const pos = simplified.attributes.position;
        m.updateWorldMatrix(true, false);
        const wm = m.matrixWorld;
        const tmp = new THREE.Vector3();
        for (let i = 0; i < pos.count; i++) {
          tmp.fromBufferAttribute(pos, i).applyMatrix4(wm);
          objLines.push(`v ${tmp.x} ${tmp.y} ${tmp.z}`);
        }
        for (let i = 0; i < pos.count; i += 3) {
          objLines.push(`f ${vOffset + i} ${vOffset + i + 1} ${vOffset + i + 2}`);
        }
        vOffset += pos.count;

        newVerts += pos.count;
        newTris  += pos.count / 3;
      });

      setStats({ tris: Math.round(newTris), verts: newVerts });

      // Save to disk: `<original-stem>-simplified.obj`.
      const base = relPath.replace(/\.[^./]+$/, "");
      const dstRel = `${base}-simplified.obj`;
      const dst = await invokeCommand<string>("simplify_mesh", {
        projectPath,
        relPath:  dstRel,
        content:  objLines.join("\n") + "\n",
      });
      setSimplifyOpen(false);
      if (onSimplified) onSimplified(dstRel);
      // Log via overlay HUD area would be nice, but we don't have one;
      // the parent gets `onSimplified` and can update the terminal.
      void dst;
      void totalRemoved;
    } catch (e) {
      setError(String(e));
    } finally {
      setSimplifyBusy(false);
    }
  }

  return (
    <div style={wrapStyle}>
      <div ref={mountRef} style={canvasStyle} />
      <div style={hudStyle}>
        <span>{relPath}</span>
        {stats && (
          <span style={mutedStyle}>
            {stats.verts.toLocaleString()} vertices · {stats.tris.toLocaleString()} triangles
          </span>
        )}
        {error && <span style={errorStyle}>{error}</span>}
      </div>
      <div style={toolbarStyle}>
        <button
          type="button"
          onClick={() => setSimplifyOpen(v => !v)}
          style={toolButtonStyle}
        >
          Simplify…
        </button>
        {simplifyOpen && (
          <div style={popoverStyle}>
            <label style={popoverLabelStyle}>Keep {Math.round(simplifyRatio * 100)}% of vertices</label>
            <input
              type="range"
              min={0.05}
              max={0.95}
              step={0.05}
              value={simplifyRatio}
              onChange={e => setSimplifyRatio(Number(e.target.value))}
              style={{ width: "100%" }}
            />
            <button
              type="button"
              onClick={onSimplify}
              disabled={simplifyBusy}
              style={popoverApplyStyle}
            >
              {simplifyBusy ? "Simplifying…" : "Apply + save"}
            </button>
            <span style={popoverHintStyle}>
              Writes <code>&lt;name&gt;-simplified.obj</code> to the project's geometry/ folder.
            </span>
          </div>
        )}
      </div>
    </div>
  );
}

function faceCentre(bbox: THREE.Box3, face: BboxFace): THREE.Vector3 {
  const c = new THREE.Vector3();
  bbox.getCenter(c);
  switch (face) {
    case "+x": c.x = bbox.max.x; break;
    case "-x": c.x = bbox.min.x; break;
    case "+y": c.y = bbox.max.y; break;
    case "-y": c.y = bbox.min.y; break;
    case "+z": c.z = bbox.max.z; break;
    case "-z": c.z = bbox.min.z; break;
  }
  return c;
}

const wrapStyle: CSSProperties = {
  position: "relative",
  width: "100%",
  height: "100%",
  overflow: "hidden",
  background: "var(--bg-canvas)",
};

const canvasStyle: CSSProperties = {
  position: "absolute",
  inset: 0,
};

const hudStyle: CSSProperties = {
  position: "absolute",
  top: 8,
  left: 8,
  padding: "4px 8px",
  background: "rgba(15, 20, 25, 0.7)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-sm)",
  color: "var(--fg-secondary)",
  fontSize: 11,
  fontFamily: "var(--font-mono)",
  display: "flex",
  flexDirection: "column",
  gap: 2,
  pointerEvents: "none",
};

const mutedStyle: CSSProperties = {
  color: "var(--fg-tertiary)",
};

const errorStyle: CSSProperties = {
  color: "var(--error, #f85149)",
};

const toolbarStyle: CSSProperties = {
  position: "absolute",
  top: 8,
  right: 8,
  display: "flex",
  flexDirection: "column",
  alignItems: "flex-end",
  gap: 6,
};

const toolButtonStyle: CSSProperties = {
  padding: "4px 10px",
  background: "rgba(15, 20, 25, 0.7)",
  color: "var(--fg-primary)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-sm)",
  fontSize: 11,
  cursor: "pointer",
};

const popoverStyle: CSSProperties = {
  display: "flex",
  flexDirection: "column",
  gap: 8,
  padding: 10,
  width: 220,
  background: "var(--bg-elevated)",
  border: "1px solid var(--border-subtle)",
  borderRadius: "var(--radius-md)",
  boxShadow: "0 4px 16px rgba(0,0,0,0.4)",
  fontSize: 11,
};

const popoverLabelStyle: CSSProperties = {
  fontSize: 11,
  color: "var(--fg-secondary)",
};

const popoverApplyStyle: CSSProperties = {
  padding: "4px 8px",
  background: "var(--accent-default)",
  color: "white",
  border: "none",
  borderRadius: "var(--radius-sm)",
  fontSize: 11,
  fontWeight: 500,
  cursor: "pointer",
};

const popoverHintStyle: CSSProperties = {
  fontSize: 10,
  color: "var(--fg-tertiary)",
};
