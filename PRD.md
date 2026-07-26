# Product Requirements Document
# Critical Path

## 1. Summary

**Critical Path** answers four questions at a glance: *what do I owe, what is happening now, why
is it happening, and what should I change or collect?*

Pick a goal — a Space Elevator phase, the active milestone, or any item — and the mod walks the
player's **real configured factory** (actual machines, recipes including alternates, clock speeds,
belts, pipes, and scheduled transport) and solves the flow available to that goal. It shows both
**current flow** and **configured design capacity**, then distils the result into the limiting path,
an honest completion estimate, and a concrete next action.

The design detail that makes it a game feature rather than a spreadsheet: **the critical path is a
path, not a tree.** Healthy branches collapse to one row; only broken branches expand. See
[docs/DesignConcept.md](docs/DesignConcept.md) for the product concept,
[docs/AnalysisModel.md](docs/AnalysisModel.md) for the interpretation contract, and
[docs/SpaceElevatorExample.md](docs/SpaceElevatorExample.md) for a worked example.

## 2. Problem and Position

Online production planners are powerful: they optimize intended factories across alternate
recipes, resource limits, imported inputs, machine counts, power, and other constraints. Save-aware
web tools go further by parsing an uploaded save into a detailed map of the factory's real
buildings, recipes, clocks, inventories, logistics, and statistics. Critical Path is complementary
to those tools, not a claim that they are simplistic.

Their unavoidable boundary is time and place: an uploaded save is a point-in-time external
snapshot. A recipe change, rebuilt connection, clock adjustment, recovered machine, or changed
vehicle route is invisible until the player saves and uploads again, then manually relates the new
snapshot to the objective that is stalled.

Critical Path runs inside the current game session. It can refresh against the world the player is
standing in, interpret changes through the selected Elevator or Milestone objective, and lead from
rate to cause to action without an export/re-upload loop. Existing in-game tooling shows useful
local rates and state; the remaining gap is factory-wide, objective-directed causality.

### Product posture

Critical Path is a user-facing, community-supported Finalomega Labs release. A purchase price is
not the measure of its responsibility to players: usability, accuracy, accessibility, respectful
defaults, and honest uncertainty are product requirements. The studio's established modding
reputation creates an expectation that this tool will feel dependable and approachable, not like a
developer diagnostic panel published unchanged.

That means:

- respect the tools players already use and explain the complementary live-use advantage plainly;
- optimize the common path for a player who wants an answer, not an analysis lesson;
- expose deeper evidence on demand without making it a prerequisite for trust;
- never manufacture certainty, urgency, or uniqueness for marketing effect;
- treat bug reports, localization, accessibility, and large-save performance as user experience.

## 3. Goals

1. Name the deepest broken or limiting link on the path to a chosen objective, not the surface
   symptom.
2. Separate current operation from configured potential so an idle-but-buildable line is not
   mistaken for missing capacity.
3. Distinguish fulfilled, degraded, blocked, planned, research-gated, and unknown states without
   turning incomplete evidence into a confident diagnosis.
4. Report a trustworthy completion estimate or buffer runway when the available evidence permits.
5. Ground every claim in the live factory — no idealized topology and no invented recipes.
6. Turn diagnosis into action: quantify the present capacity gap and locate the relevant machines,
   routes, or collection points in the world.

## 4. Non-Goals

- **Not a scenario planner.** It does not place buildings, invent a factory, or project the impact
  of a hypothetical new production line in the current release. It reports capacity and deficits
  in the factory that exists now.
- **Not an automation tool.** Read-only: it never builds, configures, dismantles, or mutates.
- **Not an AI/LLM mod.** All analysis is deterministic.
- **No cheats.** It reports only what the player could determine by walking the factory.
- **Not multiplayer-authoritative** in v1 (see §10).

## 5. Target Users

Mid-to-late-game pioneers with factories too large to audit by hand — the point at which "why is
this line idle?" stops being answerable by looking at it. Secondary: returning players resuming a
save they no longer remember building.

## 6. Core User Stories

1. *"Why isn't my Space Elevator phase progressing?"* → the chain stalls at Iron Ore; extraction
   is 990/min against 1,485/min demand; add or upgrade extractors.
2. *"How long until Phase 3 is done?"* → 360 left to produce after banked stock, ~72 minutes at
   the current rate; no bottleneck exists.
3. *"What do I still need to unlock?"* → the objective path needs Computers; no recipe is
   unlocked; the research that grants it is available now.
4. *"Where is the machine that's blocking me?"* → marker on the compass/map, highlight in world.
5. *"Is this line broken or just full?"* → saturated: producing surplus with nowhere to go; not a
   fault, no action needed.
6. *"Could this factory do more if I fixed the inputs?"* → current flow is low, but configured
   design capacity is healthy; the limiter is upstream rather than another downstream machine.
7. *"Why does this island look disconnected?"* → its scheduled train or truck route is represented
   as transport topology; missing or unmeasured throughput remains visibly approximate.

## 7. MVP Requirements (P0)

### 7.0 Mod-Agnostic Data (P0, structural)

All content is resolved through the game's registries (phase/schematic/recipe managers, buildable
subsystem) and descriptor reflection. **No vanilla item names, class names, or asset paths in the
analysis or UI.** Modded items, recipes, machines, and objectives flow through unchanged; item and
machine icons come from their descriptors, so modded content is iconized for free. Non-standard
producer classes degrade to a flagged `unknown producer`, never a silent miscount.

### 7.1 Analysis Engine

The engine is a **clean public module API**, structured results only — the panel, HUD tracker,
and export are merely its first consumers. Other mods may build on the analysis; API stability
follows SemVer. Structurally, the plugin ships two modules: `CriticalPathEngine` (Runtime,
analysis only, no UI dependencies of any kind) and the UI module consuming its public headers —
so a mod wanting the data links the engine and ignores the panel entirely. Engine results are
JSON-serializable (required by export, and by any external tooling surface), and the engine is
**game-type aware**: central storage is a server-side subsystem and stays available in every
mode; pocket inventory and player-anchored queries (selected/nearby) require a player context —
absent ones report as absent, never as zero — and location-anchored queries take an explicit
anchor parameter, defaulting to the local player when one exists. Dedicated-server consumers are
first-class.

- **Snapshot** the configured factory: per-product aggregates (configured capacity, producing/idle
  counts by reason, machine type), per-item configured demand, raw-resource extraction supply,
  owned totals (storage containers + central storage + player inventory), and objective costs.
- **Recipe edges** from machines actually in use, so alternates are discovered rather than assumed.
- **Typed flow graph**: machines, recipes, buffers, objectives, belts, pipes, and scheduled
  transport become a bounded plain-data network. Capture completeness and unknown capacities stay
  attached to the evidence.
- **Dual solve**: current flow represents observed operating conditions; design flow represents
  configured potential under the same captured topology. The interface names which basis supports
  each claim.
- **Conservative diagnosis**: no producer, research gap, insufficient connected capacity, no
  reachable route, fluid/head-lift constraint, and incomplete connectivity remain distinct.
- **Quantitative interpretation** (see [docs/AnalysisModel.md](docs/AnalysisModel.md) and
  [docs/FlowModel.md](docs/FlowModel.md)): connected sufficiency, limiter walk, objective ETA,
  buffer runway, capacity gaps, byproduct disposal, and prospective recipe grafts are derived
  from solved flow rather than global production totals.
- **Verdict**: time-limited, degraded, blocked, planned, or unknown, with the strongest supported
  action and the assumptions that make it valid.
- Bounded and non-blocking: hard caps on buildings scanned and walk depth; must not hitch the
  game thread on a large save (see §11).

### 7.2 The Panel

Opened by a rebindable key. The current panel is a centred modal workspace (up to 1080 × 860 UI
units) with a header, visible data age, two objective columns, and a shared action/verdict region.
Phase C keeps that frame and introduces one report-level tab strip: **Path · Balance · Plan**. The
visible information contract is:

- **Objectives without mode hiding** — current Space Elevator phase and active milestone appear
  side by side on Path. A future arbitrary-item query may add search without replacing those
  summaries.
- **The path** — one row per hop, rendered as an indented chain (not a graph). Each row shows the
  item, status, the *machine type and count* producing it, need-vs-make rates, and live state.
  Fulfilled branches collapse to a single row; broken branches expand to the blocker and stop.
- **Verdict line** — the ETA or the fix, in one sentence.
- **List + inspector pattern**: rows stay 5-second legible (icon · glyph · name · ledger · one
  chip); selecting a row opens a detail pane with recipe stoichiometry and sufficiency bars,
  connectivity split, buffer ledger with runway, and producing machines. Item **tooltips**
  (vanilla-style card: icon, localized name, description) appear on hover; selecting a row shows
  the same information in the inspector, so nothing is hover-exclusive.
- **Real item/machine icons** from descriptors on every row.
- **Lifecycle states are specified, not improvised**: loading ("analyzing…" while async work
  runs), empty (no objective selected / nothing needed), error (engine unavailable), and
  truncated (caps hit: "scanned N of M buildings" — the UI never presents a capped scan as
  complete). Data age is visible, with an explicit refresh affordance.
- **Current versus design evidence** — rate labels and expanded details make the basis explicit;
  approximate transport capacity and incomplete scans never masquerade as measured truth.
- **Item balance** — relevant ingredients use one compact comparison: demand, installed
  production, input/power-supported production, and delivered supply. A separate buffer strip
  expresses stock and deficit runway without presenting inventory as a sustained rate. Selecting
  an objective/path ingredient opens this evidence on Balance; Phase C does not add a global item
  browser.
- **Action layer** — the Plan section gives bottom-up build tiers; the selected blocker/limiter
  provides its present capacity gap, supported fix, and (when coordinates exist) a locate/ping
  action.
- **Bounded programmatic rows** — objective caps keep the two-column report bounded, so the current
  implementation uses vertical rows inside internal scrolling rather than virtualization.
- **Progressive disclosure** — the default is distilled; deep evidence, recipe math, network
  scope, transport contribution, storage runway, and machine locations expand on demand. Future
  Augments or Logistics families may become dedicated views when their content warrants it.

### 7.3 Interface Quality Bar

This is a first-class requirement, not polish. Prior Finalomega UI work had to retrofit a shared
style layer after per-file colour drift, shipped a font that could not render several supported
languages, and patched Blueprint button styles at runtime because the authored asset was wrong.
None of that is acceptable here.

- **One design system, defined before the first widget.** A single style header owns palette,
  typography scale, spacing, and control styles. No literal colours in feature widgets.
- **Correct font from day one** — the in-game runtime multi-script font, so every language
  Satisfactory ships renders (no tofu).
- **All player-facing text is `FText`** and localization-ready from the first commit; no
  string concatenation that assumes English word order.
- **Contrast is a gate**: body text >= 4.5:1, large text and glyphs >= 3:1 against the actual
  rendered background, verified in the authoring loop via rendered thumbnails.
- **Status must not rely on colour alone.** Every state carries a glyph/shape as well as a
  colour (colour-blind accessibility; also survives screenshots and compression).
- **Authored correctly in UMG**, verified visually before shipping — not styled at runtime to
  compensate for a bad asset.
- **Mouse + keyboard first.** Hover and click are primary; the keyboard carries power shortcuts.
  Controller/Steam Deck support is opportunistic (sane focus order costs nothing) but is never
  allowed to complicate or slow the M+K experience.
- **Resolution/DPI independent**: no fixed pixel layouts; must survive ultrawide and 4K.
- **Reads at a glance in ≤5 seconds** for the common case (one goal, one blocker).

## 8. P1 Requirements

### 8.1 World Grounding — scanner-style pings (remaining)

Every finding with coordinates gets a **Ping** action that behaves like the resource scanner:
a temporary compass/map representation carrying the relevant item's own icon (wasted Somersloop
-> sloop icon pulsing on the compass; blocked machines -> the blocked item's icon), timed or
dismissable, multiple pings for multi-machine findings. Familiar vanilla UX, implemented through
the actor-representation system. This is the feature that turns "your problem is Stator" into
"your problem is *those three Constructors, over there*."

For **payable parts**, the ping answers "where do I collect?": subtract depot/pocket stock first
(it already travels with the player — no ping, just "already with you"), then ping the **minimal
covering set** of storage containers — fewest containers, largest first, whose sum covers the
remaining requirement. Never "all containers holding the item" (compass spam) and never only the
largest (may not cover the errand). A multi-part payable objective renders as a collection run:
each item's covering set, distinguished by item icon. Engine support: per-container location
breakdowns are retained **only for items on active objectives**, keeping snapshots lean.

### 8.2 HUD Tracker (remaining)

An opt-in compact line near the vanilla objective tracker: `Main Body · ~72 min · blocked: Stator`.
Turns the mod from a screen you visit into a checker that tells you when something changes.

### 8.3 Export (implemented foundation)

Write the current path to disk as Markdown/Mermaid for sharing, wikis, and debugging. Falls out of
the engine for free and doubles as the developer's inspection tool.

### 8.4 Augment Audit (remaining)

Somersloops and Power Shards are finite allocations; the audit enumerates every one (slotted /
stored / carried), flags **wasted amplification** (augments in idle, saturated, or standby
machines — free recovery), protects **permanent-value placements** (chains rooted in
world-gathered inputs, where doubling compounds into value that cannot otherwise exist), and
ranks the rest against the objective's marginal value. Scarcity is computed from whether a
producing recipe is unlocked, so recommendations soften as augments become renewable.

## 9. Future (P2)

- Multiple goals tracked at once, with a combined "what should I fix first" ranking.
- Historical trend: is this blocker getting better or worse?
- Power as a first-class dependency (a browning-out grid is a blocker like any other).
- Hypothetical new-line impact analysis: inject proposed demand into a selected supply domain and
  compare the result without changing the world.

## 10. Multiplayer

The engine is game-type aware and can capture on an authoritative host or dedicated server. It does
not assume a local pawn: central storage and world topology remain available, while pocket inventory
and player-relative queries report absent without an explicit player context. The v1 panel targets
single-player and hosted-session clients; dedicated-server structured consumers remain first-class.
Central storage is session-shared, so player-facing ownership copy must distinguish shared stock
from the selected player's pocket inventory.

## 11. Performance

Large saves make this data genuinely big; performance is an engine design axis, not a tuning
pass at the end.

- **On-demand, never per-tick**: analysis runs on panel open / explicit refresh / API query; the
  HUD tracker refreshes on an interval.
- **Layered cost with lazy evaluation**: cheap aggregates separate from graph construction and
  solve work; topology and result caching are release requirements, not assumed current behavior.
- **Dirty-tracking over rescanning**: topology changes only on build/dismantle (rare, hookable —
  invalidates the graph cache); machine state changes often but never touches topology. Two
  caches, two invalidation rates.
- **Game-thread extraction, async analysis**: UObject reads stay on the game thread under a
  per-frame time budget; BFS, ratios, and walks run on extracted plain-data arrays and may move
  to a worker thread, publishing results when done.
- **Server discipline**: on dedicated servers the tick budget is the scarcest resource; every
  phase is timed, and the external API surface doubles as the profiling harness.
- Current stress-save baseline (21,888 buildings, 4,588 nodes / 5,214 edges, nothing truncated):
  snapshot capture about 60 ms, flow capture about 34 ms, dual solve about 100 ms. The solve runs
  on the thread pool, so the game thread pays roughly 95 ms — about six frames, once, on an
  explicit open or refresh. An earlier baseline recorded the solve near 3 seconds; that predates
  sparse item iteration.
  Release gate: panel-open work must be visibly asynchronous, cancellation-safe, and free of a
  perceptible game-thread hitch; large saves degrade through honest caps/time slicing. The first
  two are met by construction; the third is a judgement on ~95 ms, and caps are not being hit.

## 12. Configuration & Persistence

Configurable: the panel keybind (rebindable), HUD tracker opt-in (default off), refresh interval
for interval-driven surfaces, and analysis caps for low-end machines. **Nothing is written to the
save file** — UI preferences and settings live in mod config only, so the mod adds zero
save-compatibility risk and can be removed from a save without residue.

## 13. Testing

The analysis core is plain-data in / structured-results out with no UObject access, so it is
**unit-testable with synthetic factories**. A regression suite encodes the pathological cases the
implementation discovered the hard way: cycles, disconnected islands, exactly-funded batch runs,
byproduct stalls, saturated chains, pure-standby reserves, transport-degraded links, empty
statistics. Live-save testing (via the external API surface) validates extraction; the synthetic
suite validates reasoning. The current engine suite contains 46 passing cases covering snapshot
truth, connectivity, prospective chains, dual flow, fluids, storage, and transport topology. Exit
code is not sufficient evidence: the game/editor log must contain the expected completed-test count
and no failure token.

## 14. Build Order

Requirements above are cumulative. This is the current delivery state, not the original forecast:

- **M0 — Engine parity — complete**: bounded snapshot, statuses, root-cause walk, ETA, structured
  public results, and an external inspection surface.
- **M1 — Panel on real data — complete foundation**: real Elevator/Milestone columns, path rows,
  lifecycle states, verdicts, Plan tiers, icons, scrolling, and export. Implementation deliberately
  uses bounded programmatic rows and shows both objective families instead of hiding one behind a
  selector.
- **M2 — Quantitative interpretation — complete**: prospective dependency grafts, physical
  connectivity islands, connected sufficiency, limiter walk, flow runway, marginal estimate,
  byproduct disposal, research payment plans, and bottom-up build tiers. These interpretations
  remain supported while Phase C changes their evidence source from legacy aggregates to solved
  flow.
- **M3 — Solved factory awareness — active**:
  - **Phase A — complete:** pure dual-flow solver, machine/recipe/buffer/objective graph, belts,
    pipes, fluid handling, head-lift feasibility, storage semantics, and synthetic coverage.
  - **Phase T — complete topology foundation:** drone, train, and truck schedule edges; live train
    and truck routes have carried positive solved flow. Measured throughput/health metadata and
    live drone-flow proof remain.
  - **Phase C — current work:** couple machine output to supplied inputs; derive item balance,
    sufficiency, blocker selection, runway, limiter, capacity gaps, verdicts, structured results,
    and panel evidence from solver-backed truth. Replace the crowded M2 body with the Path ·
    Balance · Plan tab shell, while preserving the five-second default Path view. Add generator
    modeling and make every approximation/unknown visible. No hypothetical factory projection or
    arbitrary-item browser is required. Item balance and public sufficiency now use disjoint solved
    domains with explicit current/design bases. Path rate/connectivity blockers and limiters now
    follow solved domains through parent-machine identity; limiter evidence distinguishes installed
    capacity, upstream input support, a proven saturated belt/pipe, and unresolved routing. The
    primary actionable limiter now receives one exact machine-or-edge marginal re-solve; planning
    graft headroom now uses the best sustainable spare capacity in one disjoint design domain
    without summing disconnected lines.
  - **Awareness remainder:** scanner pings and augment audit on the same result/location model.
- **M4 — Product finish:** complete the rich evidence/inspector interactions, HUD tracker,
  arbitrary-item querying decision, vanilla theming pass, performance hardening, localization and
  DPI/accessibility validation, packaging, and release-save acceptance.

## 15. Success Criteria

1. On a stalled objective, the named blocker matches what an experienced player would conclude
   after manually auditing the chain.
2. On a healthy objective, the ETA is within ~10% of observed completion.
3. No false alarms from known-benign states: saturation, deliberate standby, transient input
   buffers, post-load statistics gaps, unused input ports.
4. Current and configured-potential rates cannot be confused, and transport/incomplete evidence is
   explicitly labeled.
5. The common diagnosis is legible within five seconds; mouse and keyboard are complete, with sane
   focus behavior for controller use.
6. A named blocker can be located in the world when the captured evidence has coordinates.
7. Zero mutations of world state, ever.

## 16. Open Questions

- Should arbitrary-item search follow the objective-scoped item-balance view after v1?
- Should multiple independent blockers appear as a short ranked action queue, or only as expanded
  branches beneath the primary limiter?
- What minimum evidence makes a transport edge "measured" rather than scheduled/approximate?
- Is the HUD tracker in the first public release, and if so should it remain opt-in by default?
- Should augment recommendations share the main Plan section or live in a dedicated audit view?
