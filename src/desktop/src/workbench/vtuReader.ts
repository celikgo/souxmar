// SPDX-License-Identifier: Apache-2.0
//
// Minimal VTU (VTK UnstructuredGrid) reader for the workbench viewer.
//
// Targets the dialect that ships with souxmar's `writer.vtu` plugin:
//   <VTKFile type="UnstructuredGrid" version="1.0" byte_order="LittleEndian">
//     <UnstructuredGrid>
//       <Piece NumberOfPoints="..." NumberOfCells="...">
//         <Points>
//           <DataArray type="Float64" NumberOfComponents="3" format="ascii">
//             ...x y z x y z...
//         <Cells>
//           <DataArray type="Int64" Name="connectivity" format="ascii">...</>
//           <DataArray type="Int64" Name="offsets"      format="ascii">...</>
//           <DataArray type="UInt8" Name="types"        format="ascii">...</>
//
// Limitations (deliberately surfaced as errors, not silent acceptance):
//   - format="binary" / format="appended" / encoded chunks: not supported.
//     These appear when the writer emits base64- or raw-binary blobs;
//     adding decoders here is feasible but a follow-up.
//   - Cell types not in CELL_TRIANGULATION are skipped with a console
//     warning so a partial mesh still renders.

import * as THREE from "three";

/** VTK cell-type ID → array of vertex-index triples relative to the cell. */
const CELL_TRIANGULATION: Record<number, ReadonlyArray<readonly [number, number, number]>> = {
  // VTK_TRIANGLE
  5:  [[0, 1, 2]],
  // VTK_QUAD
  9:  [[0, 1, 2], [0, 2, 3]],
  // VTK_TETRA — four triangle faces; outward winding for right-handed cells.
  10: [[0, 2, 1], [0, 1, 3], [0, 3, 2], [1, 2, 3]],
  // VTK_HEXAHEDRON — six quad faces → twelve triangles. Conventional
  // outward-facing winding per https://vtk.org/wp-content/uploads/2015/04/file-formats.pdf.
  12: [
    [0, 3, 2], [0, 2, 1], // -z
    [4, 5, 6], [4, 6, 7], // +z
    [0, 1, 5], [0, 5, 4], // -y
    [2, 3, 7], [2, 7, 6], // +y
    [0, 4, 7], [0, 7, 3], // -x
    [1, 2, 6], [1, 6, 5], // +x
  ],
  // VTK_WEDGE — three quad sides + two tri caps → 8 triangles
  13: [
    [0, 1, 2], [3, 5, 4],         // tri caps
    [0, 3, 4], [0, 4, 1],         // quad side 1
    [1, 4, 5], [1, 5, 2],         // quad side 2
    [2, 5, 3], [2, 3, 0],         // quad side 3
  ],
  // VTK_PYRAMID — one quad base + four tri sides → 6 triangles
  14: [
    [0, 3, 2], [0, 2, 1],         // quad base
    [0, 1, 4], [1, 2, 4],
    [2, 3, 4], [3, 0, 4],
  ],
};

export function parseVtu(text: string): THREE.BufferGeometry {
  return parseVtuWithFields(text).geometry;
}

/** A scalar or vector point-field carried alongside the geometry. The
 *  `magnitude` array is the per-point scalar value used for colormap
 *  rendering — equal to `values` for n=1 fields, and `sqrt(sum(c²))`
 *  for n>1 fields. min/max are computed over `magnitude`. */
export interface VtuField {
  name:        string;
  components:  number;   // 1 for scalar, 3 for vector
  /** Raw per-point values, flattened (length = num_points * components). */
  values:      Float32Array;
  /** Per-point magnitude — colormap input. length = num_points. */
  magnitude:   Float32Array;
  min:         number;
  max:         number;
}

export interface VtuParsed {
  geometry:  THREE.BufferGeometry;
  numPoints: number;
  fields:    VtuField[];
}

export function parseVtuWithFields(text: string): VtuParsed {
  const xml = new DOMParser().parseFromString(text, "application/xml");
  const err = xml.querySelector("parsererror");
  if (err) {
    throw new Error(`VTU is not valid XML: ${err.textContent?.trim() ?? "parse error"}`);
  }
  const piece = xml.querySelector("UnstructuredGrid > Piece");
  if (!piece) {
    throw new Error("VTU missing <UnstructuredGrid><Piece> element");
  }

  const pointsArr = readArrayUnder(piece, "Points > DataArray", "Points DataArray");
  const conn      = readArrayUnder(piece, 'Cells > DataArray[Name="connectivity"]', "connectivity");
  const offsets   = readArrayUnder(piece, 'Cells > DataArray[Name="offsets"]',      "offsets");
  const types     = readArrayUnder(piece, 'Cells > DataArray[Name="types"]',        "types");

  if (pointsArr.length % 3 !== 0) {
    throw new Error(`Points array length ${pointsArr.length} is not a multiple of 3`);
  }
  const numPoints = pointsArr.length / 3;

  // Build triangle index array from the cell connectivity, dispatching per
  // cell type. Unknown cell types are skipped and reported once.
  const triIndices: number[] = [];
  let cellStart = 0;
  const unsupported = new Set<number>();
  for (let i = 0; i < types.length; i++) {
    const ct = types[i];
    const cellEnd = offsets[i];
    const localIds = conn.slice(cellStart, cellEnd);
    cellStart = cellEnd;

    const tri = CELL_TRIANGULATION[ct];
    if (!tri) {
      unsupported.add(ct);
      continue;
    }
    for (const [a, b, c] of tri) {
      triIndices.push(localIds[a], localIds[b], localIds[c]);
    }
  }
  if (unsupported.size > 0) {
    // eslint-disable-next-line no-console
    console.warn(
      `vtuReader: skipped cells of unsupported types: ${Array.from(unsupported).sort((a, b) => a - b).join(", ")}`,
    );
  }

  const geom = new THREE.BufferGeometry();
  geom.setAttribute("position", new THREE.Float32BufferAttribute(new Float32Array(pointsArr), 3));
  geom.setIndex(triIndices);
  geom.computeVertexNormals();

  // Parse <PointData> scalar/vector arrays. CellData not yet supported —
  // the writer.vtu plugin in souxmar emits PointData today.
  const fields: VtuField[] = [];
  const pointDataArrays = piece.querySelectorAll("PointData > DataArray");
  pointDataArrays.forEach(el => {
    const name = el.getAttribute("Name") ?? "";
    if (!name) return;
    const fmt = el.getAttribute("format") ?? "ascii";
    if (fmt !== "ascii") {
      // eslint-disable-next-line no-console
      console.warn(`vtuReader: skipped PointData "${name}" — format="${fmt}" not supported`);
      return;
    }
    const components = Math.max(1, Number(el.getAttribute("NumberOfComponents") ?? "1"));
    const raw = parseAsciiNumbers(el.textContent ?? "");
    if (raw.length !== numPoints * components) {
      // eslint-disable-next-line no-console
      console.warn(
        `vtuReader: PointData "${name}" has ${raw.length} values, expected ${numPoints * components}`,
      );
      return;
    }
    const values = new Float32Array(raw);
    const magnitude = new Float32Array(numPoints);
    if (components === 1) {
      magnitude.set(values);
    } else {
      for (let i = 0; i < numPoints; i++) {
        let s = 0;
        for (let c = 0; c < components; c++) {
          const v = values[i * components + c];
          s += v * v;
        }
        magnitude[i] = Math.sqrt(s);
      }
    }
    let mn = Infinity, mx = -Infinity;
    for (let i = 0; i < magnitude.length; i++) {
      const v = magnitude[i];
      if (v < mn) mn = v;
      if (v > mx) mx = v;
    }
    if (!Number.isFinite(mn)) mn = 0;
    if (!Number.isFinite(mx)) mx = 0;
    fields.push({ name, components, values, magnitude, min: mn, max: mx });
  });

  return { geometry: geom, numPoints, fields };
}

function readArrayUnder(piece: Element, selector: string, label: string): number[] {
  const el = piece.querySelector(selector);
  if (!el) throw new Error(`VTU missing ${label}`);
  const fmt = el.getAttribute("format") ?? "ascii";
  if (fmt !== "ascii") {
    throw new Error(
      `VTU ${label} uses format="${fmt}" — the workbench's built-in viewer only reads format="ascii" today. Use ParaView for binary/appended files, or re-run the writer with the ASCII flag.`,
    );
  }
  return parseAsciiNumbers(el.textContent ?? "", label);
}

/** Tokenise a whitespace-separated numeric stream. Used for both the
 *  Points/Cells arrays and PointData field arrays. The `label`
 *  parameter is optional — when set, malformed tokens throw with
 *  context; when omitted, callers handle the empty/short array case
 *  themselves (used for opt-in PointData parsing). */
function parseAsciiNumbers(text: string, label?: string): number[] {
  const out: number[] = [];
  let i = 0;
  const n = text.length;
  while (i < n) {
    while (i < n && (text.charCodeAt(i) <= 32)) i++;
    const start = i;
    while (i < n && text.charCodeAt(i) > 32) i++;
    if (i === start) break;
    const v = Number(text.slice(start, i));
    if (Number.isNaN(v)) {
      if (label) throw new Error(`VTU ${label}: non-numeric token "${text.slice(start, i)}"`);
      return out;
    }
    out.push(v);
  }
  return out;
}
