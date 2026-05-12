# Modal-beam — first runnable modal-analysis example

A 3-stage pipeline exercising the `solver.modal.*` capability surface:

```
mesh   (mesher.tetra.hello)     — placeholder unit-tet, 4 nodes
 ↓
modal  (solver.modal.linear)    — closed-form Euler-Bernoulli, N mode shapes
 ↓
write  (writer.vtu)             — ParaView-readable, one time step per mode
```

The same YAML runs unchanged once the FEniCSx + SLEPc adapter
lands — `solver.modal.linear` becomes resolved by a real
eigensolver instead of the closed-form stub. Same swap-test
contract `mesher.*` already proves.

## What this *is*

A demonstration that:

- The `solver.modal.*` namespace dispatches cleanly through the
  existing solver C ABI — no new stage type needed.
- A multi-step vector Field where each step is a *mode shape*
  (rather than a time slice) round-trips the field stream and the
  VTU writer.
- The agent's `solve` + `query_field` chain (see
  `docs/AI_INTEGRATION.md`) generalises to modal analysis with no
  new agent-tool work.

## What this *is not* (yet)

- The mesher is `mesher.tetra.hello`, a unit tet. Mode shapes are
  evaluated at four nodes, which is enough to prove the plumbing
  but isn't a real beam discretisation. The plumbing is the point.
- The solver is `modal-stub`, which treats the bounding-box x-span
  as the cantilever length L. Mode shapes are correct for the
  Euler-Bernoulli closed form; the natural frequencies they imply
  only match if you actually pulled (E, I, ρ, A) for the mesh's
  cross-section. A real eigensolver replaces this when the
  FEniCSx + SLEPc adapter lands.

## Running it

```sh
SOUXMAR=./build/dev/src/cli/souxmar
PLUGINS=./build/dev/examples/plugins

$SOUXMAR run examples/modal-beam/pipeline.yaml --plugin-path $PLUGINS
```

Expected output:

```
Running pipeline pipeline.yaml (3 stages, N capabilities available)
  [OK      ] mesh    hash=...
  [OK      ] modal   hash=...
  [OK      ] write   hash=...

pipeline ok (3 stages)
```

`modal-beam.vtu` is now in your working directory. Open in
ParaView, switch to the Animation View, and scrub through the
time-step axis to see modes 1–4 in succession.

## What to vary

| Stage   | Field            | Effect                                                     |
| ------- | ---------------- | ---------------------------------------------------------- |
| `mesh`  | `target_size`    | Edge length the upstream mesher should aim for             |
| `modal` | `num_modes`      | Number of mode shapes computed (1–4 in the stub)           |
| `modal` | `youngs_modulus` | E — enters the frequency formula, not the shape            |
| `modal` | `density`        | ρ — enters the frequency formula, not the shape            |
| `modal` | `area, inertia`  | A, I — enter the frequency formula, not the shape          |

## Computing the natural frequencies

The stub emits mode *shapes* normalised to unit tip deflection.
The corresponding angular frequencies are:

```
omega_n = (beta_n)^2 * sqrt(E * I / (rho * A))
```

where `(beta_n · L)` are the first four roots of
`cos(βL)·cosh(βL) + 1 = 0`:

| Mode | βL                 |
| ---- | ------------------ |
| 1    | 1.8751040687119612 |
| 2    | 4.694091132974174  |
| 3    | 7.854757438237612  |
| 4    | 10.995540734875467 |

A future host-side frequency-table accessor (the field-metadata
follow-up tracked alongside the modal-solver work) will expose
these alongside the field; until then, the formula above is the
source of truth.

## Comparing to the cantilever / thermal-fin examples

| Aspect                | `cantilever-beam`        | `thermal-fin`               | `modal-beam`                       |
| --------------------- | ------------------------ | --------------------------- | ---------------------------------- |
| Stages                | 2 (mesh + write)         | 4 (mesh + heat + post + write) | 3 (mesh + modal + write)        |
| Capability namespaces | `mesher.*`, `writer.*`   | + `solver.heat.*`, `postproc.*` | + `solver.modal.*`            |
| Field shape           | none                     | 5-step scalar (time series) | N-step vector (one step per mode)  |
| Goal                  | First end-to-end demo    | Heat-equation tour          | Modal/vibration capability tour    |
