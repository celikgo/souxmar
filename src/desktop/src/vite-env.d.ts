/// <reference types="vite/client" />

// occt-import-js ships only a CommonJS bundle without typings.
// Declare a minimal shape covering the surface we use.
declare module "occt-import-js" {
  interface OcctImportParams {
    linearUnit?:           "millimeter" | "centimeter" | "meter" | "inch" | "foot";
    linearDeflectionType?: "bounding_box_ratio" | "absolute_value";
    linearDeflection?:     number;
    angularDeflection?:    number;
  }
  interface OcctMeshAttr {
    array: number[];
  }
  interface OcctMesh {
    name:        string;
    color?:      [number, number, number];
    attributes:  { position: OcctMeshAttr; normal?: OcctMeshAttr };
    index:       { array: number[] };
  }
  interface OcctImportResult {
    success: boolean;
    meshes:  OcctMesh[];
    root:    { name: string; meshes: number[]; children: unknown[] };
  }
  interface OcctModule {
    ReadStepFile: (content: Uint8Array, params: OcctImportParams | null) => OcctImportResult;
    ReadIgesFile: (content: Uint8Array, params: OcctImportParams | null) => OcctImportResult;
    ReadBrepFile: (content: Uint8Array, params: OcctImportParams | null) => OcctImportResult;
  }
  interface InitOptions {
    locateFile?: (path: string, prefix: string) => string;
  }
  const init: (options?: InitOptions) => Promise<OcctModule>;
  export default init;
}

declare module "occt-import-js/dist/occt-import-js.wasm?url" {
  const url: string;
  export default url;
}
