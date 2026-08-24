# fem-cantilever — the first example that actually solves something

A 3-stage pipeline exercising `solver.elasticity.fem`, the first capability
in the default build that forms a stiffness matrix:

```
mesh   (mesher.am.layered)        — 60 x 20 x 20 mm Hex8 box, side-tagged
 ↓
solve  (solver.elasticity.fem)    — assemble, apply BCs, solve
 ↓
write  (writer.vtu)               — displacement in <PointData>
```

Run it:

```sh
souxmar run examples/fem-cantilever/pipeline.yaml \
  --plugin-path build/dev/examples/plugins
```

Open `fem-cantilever.vtu` in ParaView, colour by `displacement`, apply
**Warp By Vector** with a large scale factor.

## Why the answer here is checkable

The load case is a steel bar held at its `-X` face and pulled along `+X` by a
10 MPa traction. That is uniform uniaxial stress, and its displacement field
is *linear*:

```
u_x =  (σ/E) x        u_y = −ν (σ/E) y        u_z = −ν (σ/E) z
```

A linear field lies inside the Hex8 finite-element space, so Galerkin
orthogonality makes the discrete answer **equal** to the analytic one rather
than close to it — on any mesh, however coarse. Measured on the output of
this exact pipeline, the largest disagreement anywhere in the model is
`1.8e-19 m` against a tip displacement of `2.857142857143e-06 m`, a relative
error of `6.3e-14`. That is the conjugate-gradient tolerance, not the
element.

`tests/integration/test_fem_elasticity.cpp` asserts the same thing on both
element types as the constant-strain patch test. This example is therefore a
check you can re-run, not a picture.

## Why tension and not bending

Bending is the better picture and the worse example. A trilinear hexahedron
**shear-locks**: with few elements through the thickness it cannot represent
the linear bending strain without spurious shear, comes out far too stiff,
and under-predicts tip deflection substantially. Shipping a bent beam here
would mean shipping a number that is quietly wrong by tens of percent, with
nothing in the file to say so.

To bend it anyway — which is a reasonable thing to want — change the
traction to `[0.0, 0.0, -1.0e7]` and reduce `target_size` until the tip
deflection stops moving. `docs/CAPABILITIES.md` records the locking
behaviour, and `tests/integration/test_fem_elasticity.cpp`'s convergence test
shows the trend it produces.

## Why rollers and not a clamp

The three `fix:` entries constrain one component each on three mutually
perpendicular faces. Together they remove all six rigid-body modes and
nothing else, so the bar is still free to contract under Poisson's ratio and
the exact uniaxial solution remains admissible.

Clamping the `-X` face in all three components instead would suppress that
contraction near the support. That is a perfectly good FEM problem — it is
just a *different* one, with a stress concentration at the support and no
closed form, so the check above would no longer apply.

## Why this mesher

`mesher.am.layered` needs no upstream `geometry:` stage. That matters more
than it should: the only always-on readers in the tree produce meshes rather
than geometry, and `reader.step` — which `examples/swap-mesher` and
`examples/mesh-comparison` both use to feed `mesher.tetra.grid` — sits behind
`SOUXMAR_WITH_OPENCASCADE`, which no CI workflow sets. Those examples exit 70
on every platform. This one runs in the default matrix, which is the whole
point of the solver being dependency-free.

It also stamps the canonical side tags (`10 = -X`, `11 = +X`, `12 = -Y`,
`13 = +Y`, `14 = -Z`, `15 = +Z`), which lets the solve name its boundaries by
tag. The plane selectors (`plane: x, at: min`) are the alternative for meshes
that carry no face tags — `mesher.tetra.grid` is one.

## What this is *not*

Not a validated structural analysis. The solver is small-strain, linear,
isotropic, static: no plasticity, no contact, no large deflection, no
dynamics, and no stress output yet (von Mises belongs in a `postproc.stress.*`
stage that reads this displacement field back). `docs/CAPABILITIES.md` and the
header of `examples/plugins/fem-elasticity/fem_elasticity.cpp` carry the full
validity envelope, including how it fails as ν approaches 0.5.
