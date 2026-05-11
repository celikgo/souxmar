---
name: onboarding-souxmar-contributor
description: Use when a new contributor (internal or external) needs to set up their souxmar dev environment, understand the workflow, sign DCO, and find their first task. Triggers on "onboarding", "new contributor", "first PR", "set up souxmar dev environment", "DCO", "where do I start".
---

# Onboarding a souxmar contributor

This skill walks a new contributor from `git clone` to a merged first PR. Same flow for internal team members and external open-source contributors.

## When to use this skill

- A new engineer joined the team.
- An external contributor opened their first issue or PR.
- A contributor returned after a long absence and needs the current workflow.

## Prerequisites the contributor needs

- A workstation running macOS 13+, Ubuntu 22.04+, or Windows 10 22H2+.
- Git, with a configured signing key for DCO.
- Familiarity with C++20 (for backend work) or React + TypeScript (for desktop work). Other roles vary.
- A GitHub account, two-factor authentication enabled.

## The setup flow

### 1. Clone

```bash
git clone https://github.com/souxmar/souxmar.git
cd souxmar
git config commit.gpgsign true   # if signing commits
```

### 2. Read these documents in order

Total time: 60–90 min.

1. `README.md` — what souxmar is, surfaces, status.
2. `docs/VISION.md` — purpose, target users, non-goals.
3. `docs/ARCHITECTURE.md` — the layered system. **Required for any backend contributor.**
4. `docs/CONTRIBUTING.md` — workflow, DCO, PR template.
5. `docs/GOVERNANCE.md` — merge tiers, RFC process, your role's authority.
6. `docs/ENGINEERING_PRACTICES.md` — quality bar, performance budgets, definition of done.
7. The doc most relevant to your team:
   - Backend: `docs/PLUGIN_SDK.md`.
   - Desktop: `docs/DESKTOP_APP.md` and `docs/UI_DESIGN.md`.
   - AI: `docs/AI_INTEGRATION.md`.
   - Platform: `docs/SPRINT_PLAN.md` and the existing CI workflows.
   - DX: `docs/PLUGIN_SDK.md`, `docs/AI_INTEGRATION.md`, plus any open docs PRs.

### 3. Install build deps

```bash
# macOS
brew install cmake ninja vcpkg pnpm rustup-init
rustup-init -y
xcode-select --install

# Ubuntu
sudo apt install cmake ninja-build build-essential libssl-dev curl pkg-config
curl https://sh.rustup.rs -sSf | sh
curl -fsSL https://get.pnpm.io/install.sh | sh

# Windows
# Install: Visual Studio 2022 Build Tools, CMake, Ninja, Rustup, pnpm
# Open "x64 Native Tools Command Prompt"
```

### 4. Build and test

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

First build typically takes 20–30 min (vcpkg builds OCCT, PETSc, VTK from source). Subsequent builds use ccache; expect single-digit minutes.

### 5. Run the desktop app (if doing desktop work)

```bash
pnpm -C src/desktop install
pnpm -C src/desktop dev
```

The app launches with a dev React server with hot module reload. Backend changes still require a rebuild + relaunch.

### 6. Run the canonical pipeline

```bash
./build/cli/souxmar run examples/cantilever-beam/cantilever.souxmar.yaml
# Open the generated .vtu in ParaView to confirm.
```

If this works, your environment is healthy.

### 7. Pick a first task

For external contributors, look for issues labelled `good-first-issue`. These are scoped, have clear acceptance criteria, and a maintainer is available to mentor.

For internal new hires, your team lead has a "warmup task" prepared — typically a small refactor or test addition in your team's area.

## DCO sign-off

Every commit MUST be signed off:

```bash
git commit -s -m "Your message"
```

This adds a `Signed-off-by: Your Name <your@email>` line, certifying the Developer Certificate of Origin. We do not require a CLA. The DCO is a one-time learning curve; after that, `git config alias.cm 'commit -s'` makes it muscle memory.

PRs without DCO sign-off on every commit cannot merge — the DCO bot blocks them.

## The PR workflow

1. Open an issue first (or comment on an existing one) describing the change.
2. Branch from `main`: `git checkout -b your-name/short-description`.
3. Make the change, including tests, docs, and benchmark updates per `ENGINEERING_PRACTICES.md`.
4. Push; open a PR; fill the PR template completely.
5. CI runs (~25 min for typical PRs). Fix anything red.
6. Reviewers respond within 1 working day; respond to feedback within 1 working day.
7. Once approved + green: a maintainer merges.

## Tier reminder

Pick the right merge tier based on what you are changing (per `GOVERNANCE.md`):

- **Tier 1** (trivial): doc typos, dependency bumps already covered by CI. One reviewer.
- **Tier 2** (standard): bug fixes, in-tree plugins, refactors within a module. Two reviewers from the affected module.
- **Tier 3** (heavy): C ABI, on-disk pipeline format, agent tool contract, data model, governance. Requires an RFC first.

When in doubt, ask in the PR description which tier you think it is. A maintainer will confirm.

## Where to ask questions

- `#souxmar-dev` Slack (internal) or `#dev` on Discord (external) for general help.
- The PR thread for review questions.
- The weekly RFC office hours (Wednesday) for design-shape questions.
- A direct mention to a maintainer if the question is blocking and time-sensitive.

We do not have daily standups. Async questions on Slack/Discord get answered the same day.

## What to expect in the first month (new hire)

- Week 1: setup, read docs, ship a small warmup PR.
- Week 2: pair with a teammate on a real story; ship it together.
- Week 3: take ownership of one small area. Push your first independent design proposal.
- Week 4: full sprint participation; demo at retro.

External contributors: pace is yours. Maintainers will respond on your timeline.

## Common mistakes

- Skipping the docs-reading step "to get straight to coding." Trust us — the docs save you re-doing the work after the first review.
- Opening a PR without an issue or RFC. Quick fixes are fine; anything else needs context.
- Submitting a PR with no test, no benchmark update, no doc update. The Definition of Done in `ENGINEERING_PRACTICES.md` is enforced.
- Forgetting `git commit -s`. Easy to fix with `git rebase -i` and amending; less easy if you have ten commits.
- "Just" pushing to main. Push to a branch; open a PR; let the process happen.

## Reference

- `docs/CONTRIBUTING.md` — full contributing guide.
- `docs/GOVERNANCE.md` — merge tiers, roles.
- `docs/ENGINEERING_PRACTICES.md` — Definition of Done.
- `docs/SPRINT_PLAN.md` — current sprint context.
- `docs/TEAM_STRUCTURE.md` — who owns what.
