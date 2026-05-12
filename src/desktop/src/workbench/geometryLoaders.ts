// SPDX-License-Identifier: Apache-2.0
//
// Format dispatcher for the workbench's frontend 3D viewer. Each
// entry takes the file's raw bytes and returns a three.js Object3D
// ready to be added to the scene. Centralising this here keeps
// ModelViewer focused on scene + camera + overlays.
//
// Coverage:
//   .obj, .stl, .ply, .gltf, .glb, .fbx, .3mf, .dae, .3ds, .vtk
// All ten use the three.js addons bundled with the npm package, so
// the only thing this file adds is the dispatch + adapter glue.
//
// .step/.stp/.iges/.igs land in a follow-up commit (occt-import-js
// WASM). .vtu also follows separately.

import * as THREE from "three";
import { OBJLoader }      from "three/addons/loaders/OBJLoader.js";
import { STLLoader }      from "three/addons/loaders/STLLoader.js";
import { PLYLoader }      from "three/addons/loaders/PLYLoader.js";
import { GLTFLoader }     from "three/addons/loaders/GLTFLoader.js";
import { FBXLoader }      from "three/addons/loaders/FBXLoader.js";
import { ThreeMFLoader }  from "three/addons/loaders/3MFLoader.js";
import { ColladaLoader }  from "three/addons/loaders/ColladaLoader.js";
import { TDSLoader }      from "three/addons/loaders/TDSLoader.js";
import { VTKLoader }      from "three/addons/loaders/VTKLoader.js";
import occtInit           from "occt-import-js";
import occtWasmUrl        from "occt-import-js/dist/occt-import-js.wasm?url";
import { parseVtu }       from "./vtuReader";

const CLAY_COLOUR = 0x6ecbff;

/** Apply our standard clay material to every Mesh in a tree, replacing
 *  whatever the loader assigned. Most CAD/mesh formats ship without
 *  textures we'd want to render, and a single shared look keeps the
 *  viewer feeling like one tool rather than ten.
 *  Materials that came from the loader are disposed so the GPU memory
 *  is released. */
export function applyClayMaterial(obj: THREE.Object3D): void {
  obj.traverse(c => {
    const m = c as THREE.Mesh;
    if (!m.isMesh) return;
    const old = m.material;
    if (old) {
      const arr = Array.isArray(old) ? old : [old];
      for (const mat of arr) {
        if (mat && typeof (mat as THREE.Material).dispose === "function") {
          (mat as THREE.Material).dispose();
        }
      }
    }
    m.material = new THREE.MeshStandardMaterial({
      color:        CLAY_COLOUR,
      metalness:    0.1,
      roughness:    0.55,
      flatShading:  false,
      side:         THREE.DoubleSide,
    });
  });
}

/** Wraps a BufferGeometry in a Mesh + clay material — used by loaders
 *  whose `.parse()` returns BufferGeometry rather than Object3D. */
function meshFromGeometry(geom: THREE.BufferGeometry): THREE.Mesh {
  geom.computeVertexNormals();
  const mat = new THREE.MeshStandardMaterial({
    color:        CLAY_COLOUR,
    metalness:    0.1,
    roughness:    0.55,
    flatShading:  false,
    side:         THREE.DoubleSide,
  });
  return new THREE.Mesh(geom, mat);
}

/** Loaders that consume an ArrayBuffer + don't reference external assets. */
/** `bytes.buffer` is typed as `ArrayBufferLike` (a superset that includes
 *  SharedArrayBuffer). All our bytes come from a Tauri command that returns
 *  a regular Vec<u8>, so the runtime value is always a plain ArrayBuffer —
 *  this cast keeps the loader signatures honest without paying for a copy. */
function asArrayBuffer(bytes: Uint8Array): ArrayBuffer {
  return bytes.buffer as ArrayBuffer;
}

function bufferOnly<T extends THREE.Object3D | THREE.BufferGeometry>(
  parse: (buf: ArrayBuffer) => T,
): GeometryLoader {
  return async bytes => {
    const out = parse(asArrayBuffer(bytes));
    return out instanceof THREE.BufferGeometry ? meshFromGeometry(out) : out;
  };
}

/** Loaders that consume a UTF-8 string. */
function textOnly<T extends THREE.Object3D | THREE.BufferGeometry>(
  parse: (text: string) => T,
): GeometryLoader {
  return async bytes => {
    const text = new TextDecoder().decode(bytes);
    const out = parse(text);
    return out instanceof THREE.BufferGeometry ? meshFromGeometry(out) : out;
  };
}

export type GeometryLoader = (bytes: Uint8Array) => Promise<THREE.Object3D>;

// Dispatch table. Each loader is responsible only for parsing — the
// caller (ModelViewer) handles centering, scaling, and material reset.
const LOADERS: Record<string, GeometryLoader> = {
  // --- mesh / surface formats -------------------------------------------
  obj: textOnly(text => new OBJLoader().parse(text)),
  stl: bufferOnly(buf  => new STLLoader().parse(buf)),
  ply: bufferOnly(buf  => new PLYLoader().parse(buf)),
  vtk: bufferOnly(buf  => new VTKLoader().parse(buf, "")),

  // --- scene formats (carry transforms / hierarchies) -------------------
  fbx:  bufferOnly(buf => new FBXLoader().parse(buf, "")),
  "3mf": bufferOnly(buf => new ThreeMFLoader().parse(buf)),
  "3ds": bufferOnly(buf => new TDSLoader().parse(buf, "")),
  dae:  textOnly(text => {
    const collada = new ColladaLoader().parse(text, "");
    if (!collada || !collada.scene) {
      throw new Error("Collada file produced no scene");
    }
    return collada.scene;
  }),

  // --- glTF: binary (.glb) self-contained, JSON (.gltf) only loads
  // without external buffers/textures unless we wire the Tauri asset
  // protocol later. We accept .gltf today; missing external refs surface
  // as a loader error.
  glb:  async bytes => {
    const gltf = await new GLTFLoader().parseAsync(asArrayBuffer(bytes), "");
    return gltf.scene;
  },
  gltf: async bytes => {
    const text = new TextDecoder().decode(bytes);
    const gltf = await new GLTFLoader().parseAsync(text, "");
    return gltf.scene;
  },

  // --- STEP / IGES via occt-import-js (WASM, OpenCASCADE port) ---------
  // First load triggers a ~5 MB wasm fetch + initialise; subsequent loads
  // reuse the cached module.
  step: async bytes => occtToThree(bytes, "step"),
  stp:  async bytes => occtToThree(bytes, "step"),
  iges: async bytes => occtToThree(bytes, "iges"),
  igs:  async bytes => occtToThree(bytes, "iges"),

  // --- VTU (VTK UnstructuredGrid) --------------------------------------
  // Built-in reader targets the ASCII dialect souxmar's `writer.vtu`
  // plugin emits. Format="binary"/"appended" surface a typed error.
  vtu: textOnly(text => parseVtu(text)),

  // --- BLEND -----------------------------------------------------------
  // .blend is Blender's native binary format. There's no in-browser
  // parser worth shipping (the format is non-public and version-coupled).
  // The reader plugin uses headless Blender on the engine side. Surface
  // a clear "export to OBJ/glTF first" error rather than pretending.
  blend: async () => {
    throw new Error(
      ".blend files can't render in the viewer directly. Export from Blender to .glb (recommended) or .obj first, then import that.",
    );
  },
};

// ---------------------------------------------------------------------------
// occt-import-js bridge.
//
// The wasm module is lazy + memoised — opening a non-CAD file should never
// pay for the 5 MB fetch. Once initialised we keep the OcctModule alive for
// the lifetime of the app; subsequent STEP/IGES opens parse in-process.
// ---------------------------------------------------------------------------

let occtPromise: Promise<Awaited<ReturnType<typeof occtInit>>> | null = null;

function getOcct() {
  if (!occtPromise) {
    occtPromise = occtInit({ locateFile: () => occtWasmUrl });
  }
  return occtPromise;
}

async function occtToThree(bytes: Uint8Array, kind: "step" | "iges"): Promise<THREE.Object3D> {
  const occt = await getOcct();
  const params = { linearUnit: "millimeter" as const };
  const result = kind === "step"
    ? occt.ReadStepFile(bytes, params)
    : occt.ReadIgesFile(bytes, params);
  if (!result.success) {
    throw new Error(`occt-import-js could not parse the ${kind.toUpperCase()} file`);
  }
  if (result.meshes.length === 0) {
    throw new Error(`${kind.toUpperCase()} file produced no triangulated meshes`);
  }
  // Each occt mesh corresponds to one b-rep solid/face; combine into a
  // single Group so the camera-fit logic in ModelViewer sees one bbox.
  const group = new THREE.Group();
  for (const m of result.meshes) {
    const geom = new THREE.BufferGeometry();
    geom.setAttribute("position", new THREE.Float32BufferAttribute(m.attributes.position.array, 3));
    if (m.attributes.normal) {
      geom.setAttribute("normal", new THREE.Float32BufferAttribute(m.attributes.normal.array, 3));
    }
    geom.setIndex(m.index.array);
    if (!m.attributes.normal) geom.computeVertexNormals();
    group.add(meshFromGeometry(geom));
  }
  return group;
}

/** Lower-case extension list the viewer can render (no leading dot). */
export function viewableExtensions(): string[] {
  return Object.keys(LOADERS);
}

/** Parse `bytes` according to the file's extension. Throws on
 *  unsupported extensions or loader-internal errors. */
export async function loadGeometry(
  bytes: Uint8Array,
  ext:   string,
): Promise<THREE.Object3D> {
  const key = ext.toLowerCase().replace(/^\./, "");
  const loader = LOADERS[key];
  if (!loader) {
    throw new Error(`No viewer for .${key} yet.`);
  }
  return loader(bytes);
}
