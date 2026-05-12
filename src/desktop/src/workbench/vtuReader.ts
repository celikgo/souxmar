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
  return geom;
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
  const text = el.textContent ?? "";
  // The VTU text body is a stream of whitespace-separated numbers.
  // Number() is fine here — all values are decimal int or float.
  const out: number[] = [];
  let i = 0;
  const n = text.length;
  while (i < n) {
    // Skip whitespace.
    while (i < n && (text.charCodeAt(i) <= 32)) i++;
    const start = i;
    while (i < n && text.charCodeAt(i) > 32) i++;
    if (i === start) break;
    const v = Number(text.slice(start, i));
    if (Number.isNaN(v)) {
      throw new Error(`VTU ${label}: non-numeric token "${text.slice(start, i)}"`);
    }
    out.push(v);
  }
  return out;
}
