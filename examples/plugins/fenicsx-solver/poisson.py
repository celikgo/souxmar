# SPDX-License-Identifier: Apache-2.0
#
# The Poisson form, in UFL — the canonical source for the
# FFCx-compiled `poisson.c` that fenicsx_solver.cpp links against.
#
# Build step (run by hand when the form changes; output committed):
#
#     ffcx --output-directory . poisson.py
#
# This emits `poisson.c` + `poisson.h` carrying the FFCx-generated
# tabulate / cell-integral kernels for the bilinear and linear forms
# declared below. The C++ shim references them by name (`poisson_a`,
# `poisson_L`); FFCx names the exported `ufcx_form` symbols after the
# Python variable names.
#
# Weak form (homogeneous Dirichlet, source f, P1 tetrahedral elements):
#
#   a(u, v) = ∫_Ω  grad(u) · grad(v) dx
#   L(v)    = ∫_Ω  f * v         dx

from basix.ufl import element
from ufl import (Coefficient, FunctionSpace, Mesh, TestFunction,
                 TrialFunction, dx, grad, inner)

# P1 Lagrange on tetrahedra. souxmar's Tet4 element ingests as
# CellType.tetrahedron in dolfinx; basix's "Lagrange" / degree=1
# matches the souxmar SOUXMAR_ET_TET4 node ordering.
e = element("Lagrange", "tetrahedron", 1)
coord_element = element("Lagrange", "tetrahedron", 1, shape=(3,))
mesh = Mesh(coord_element)
V    = FunctionSpace(mesh, e)

u = TrialFunction(V)
v = TestFunction(V)
f = Coefficient(V)

a = inner(grad(u), grad(v)) * dx
L = f * v                    * dx
