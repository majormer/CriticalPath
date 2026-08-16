# Critical Path - Validation Evidence and Release Gates

This reference records the evidence behind Critical Path's model and the checks required before a
new analysis path becomes player-facing. It is organized by invariant rather than development
session so the proof remains useful after branches and milestones change.

## 1. Validation doctrine

- A successful compile is not behavioral validation.
- A headless Unreal exit code is not test success; count explicit Test Completed lines and inspect
  every result.
- A packaged-game screenshot proves presentation, not analysis.
- An API result proves capture/analysis, not player-facing wording.
- Unknown, capped, unavailable, or non-converged data must remain unknown.
- Legacy/new divergences are explained before deleting the legacy path.
- Live-save conclusions require a controlled topology or state change when possible.

## 2. Automated baseline

The analysis and flow suites run headlessly with the editor closed:

    UnrealEditor-Cmd.exe <uproject> -ExecCmds="Automation RunTests CriticalPath; Quit" \
      -unattended -nullrhi -nosound

The latest Phase C baseline contains **58/58 successful tests**. Coverage includes:

- exactly funded objectives and benign saturation/standby behavior;
- disconnected and capped connectivity;
- prospective chains, research payments, and design-domain graft headroom that chooses one
  attachable factory line without summing disconnected producers or hiding unknown routes;
- limiter selection and deep-tie behavior;
- belt capacity, splitter redistribution and filters, ordinary merger contention, and configured
  priority-merger group ordering with fair sharing inside equal-priority groups;
- storage transparency and full-storage backpressure;
- multi-item shared edges;
- current/design dual basis;
- manufacturer input/output coupling, including multi-input limiting ratios;
- four-rate item balance and reachable-buffer runway;
- objective ETA that transitions from the observed rate to input-supported output when an
  upstream buffer runs dry;
- solver-derived public sufficiency across disjoint domains, with separate current/design totals
  and limiting-domain ratios that do not reproduce overlapping M2 network inflation;
- unknown-rate transport propagation from a converged topology solve into public sufficiency;
- solver-backed Path blockers and limiting-domain ratios along configured recipe dependencies;
- parent-machine identity attribution, proving that a worse unrelated item domain does not become
  the objective chain's limiter;
- healthy-intermediate stopping, proving that a fulfilled branch cannot donate an unrelated deeper
  shortage to the objective path;
- observed producer corroboration, proving that a fully operating/backpressured line with enough
  installed capacity is not overridden by a pessimistic shared-manifold allocation;
- limiter constraint attribution across installed capacity, upstream input support, and a named
  saturated delivery edge without inventing edge evidence for a machine-side loss;
- exact primary-limiter perturbation solves for an additional average machine and a next-tier edge,
  including a downstream belt that prevents the machine scenario from reaching its arithmetic rate;
- fulfilled-part limiter suppression, consumer-only-domain limiter rejection, and primary-candidate
  rejection for zero-installed domains that have no producer to perturb;
- typed unknown-flow provenance, including proof that a known vehicle route reports unmeasured
  throughput while an incomplete solve does not masquerade as a transport-only qualification;
- descriptor-backed fluid-form domains despite incidental conveyor connections on fluid machines;
- domain-scoped producer/consumer machine-state aggregation, estimated current rates, and
  endpoint-local fuel buffers;
- blocked/head-capped edges;
- cycles and relative convergence;
- component-local damping and convergence tolerances, so an unrelated cycle or high-throughput
  domain cannot truncate a smaller acyclic line;
- component-local stall detection, so a domain that stops improving is damped even when its
  topology is not a cycle;
- domain-scoped non-convergence, so one stalled transport network cannot erase known results from
  independent networks;
- naturally gathered items with no manufacturing recipe, which remain legitimate inputs rather
  than becoming impossible production-gap advice;
- causal ordering across a synthetic 110-tier configured recipe chain, deeper than the normal
  iteration guard;
- passive solid-storage/merger loop contraction with retained aggregate stock and exact external
  ingress/delivery gate capacities;
- pipe-junction contraction;
- fluid-buffer manifold contraction with retained stock;
- slow multi-hop path bootstrap;
- truncation/non-convergence honesty;
- offline replay of a captured real graph.

The Phase C megafactory replay captured 1,342 nodes and 1,380 edges. A 36-container/12-merger Iron
Ore buffer loop previously failed to settle after 256 passes. Passive-loop contraction reduced the
solve graph to 1,294 nodes and 1,306 edges while retaining its external gates; causal recipe
ordering and transport-local tolerance then converged the same graph in 58 passes.

Do not claim a new total from process exit alone. The latest log must show every Test Completed
result as Success.

## 3. M0/M1 engine and panel evidence

- The public engine and optional modular-feature JSON boundary were built and consumed without
  introducing UI dependencies into CriticalPathEngine.
- The panel opens from its keybind, uses live objective results, exports Markdown, and closes with
  Escape in preview/tunnel input handling.
- The root is fully modal after click-through caused a reproducible save-menu soft lock.
- The internal fullscreen canvas and centred frame are required; viewport-slot anchoring placed
  the panel partly off-screen.
- Runtime rows must be populated after AddToViewport because RebuildWidget runs on first add.
- The shipping game, not the editor, is the visual truth for base-game textures.

Do not re-test the old click-through failure by deliberately opening menus behind the panel; the
modal contract is the regression.

## 4. M2 quantitative evidence

### Directed connectivity

Connecting a previously isolated Versatile Framework producer changed consumer-reachable capacity
from 0/12.5 to 12.5/12.5 per minute. An independent connection walk confirmed the seven-actor
route. The complete 21,471-building capture took approximately 52 ms with no cap hit.

### Network-scoped sufficiency

Live disconnected islands proved that global surplus cannot establish local sufficiency:

- Coal: aggregate 1380/1260 across four networks, while one 585/min-demand island had zero
  belt/pipe supply.
- Iron Plate: aggregate 360/225, with a separate 112.5/min unfed island.
- Sulfur: aggregate 240/90, with a separate 45/min unfed island.

The panel simultaneously showed scaled prospective grafts: Cable had 140/min spare against
56.25/min need while Computer had 0/min spare against 7.5/min need.

### Known legacy defect

M2 flow-network rows overlap. Summing them double-counts consumers and sometimes producers:
Computer appeared as 60 consumers / 320 per minute while the raw snapshot and new solver both
reported 17 consumers / 85 per minute. This is confirmed evidence for Phase C replacement.
Do not tune the solver to reproduce the inflated M2 aggregate.

## 5. Flow solver evidence

### First real solves

- A 4,292-node / 4,565-edge solids graph converged in 32 iterations with no truncation and
  published 1,276 consumer-item deliveries.
- The first multi-item failure was caused by deducting shared capacity in both supply and
  acceptance passes. The corrected acceptance pass is a per-item ceiling.
- Adding fluids produced a 5,169-node / 6,336-edge graph whose delta stalled. Offline replay
  showed the slowest mode was an Aluminum Ingot splitter-to-splitter belt-balancer loop, not the
  pipe junctions initially suspected.
- Contracting pipe-junction manifolds plus the 0.01%-of-total-supply relative tolerance made the
  captured graph converge in 55 iterations.

### Current/design parity

The initial solver/M2 comparison exposed a basis mismatch: current-state machines legitimately
produce and demand less than configured design. After the dual solve was added:

- raw snapshot demand and solver demand matched for 81/91 items before fluids;
- all ten misses were fluid descriptors;
- after fluid capture, demand matched 91/91.

The observed-statistics subsystem was unavailable on the reference saves, so measured-statistics
parity remains an open release gate whenever the game provides healthy data.

### Controlled objective-path repair

A packaged Phase C build was exercised against a Phase 3 Versatile Framework line using a shared
Wire manifold and a priority merger:

- Omitting the priority merger from capture first severed the Wire path. Capturing it as a merger
  restored the connected line.
- The first limiter walk then crossed a fully supplied Reinforced Iron Plate branch and incorrectly
  named shortages on shared Caterium Ingot, then Iron Rod networks. The walk now stops at a supplied
  intermediate and treats a fully operating, adequately sized matched producer set as stronger
  evidence than pessimistic shared-manifold allocation.
- With the false deep branches removed, the report identified the actual direct fault: the target
  assembler occupied a consumer-only Steel Beam domain with 75/min demand and zero connected
  producers.
- Connecting that Steel Beam belt joined the target to the producing domain on the next capture.
  The blocker, limiter, and missing-input issue cleared; the assembler sustained 12.5/min at 100%
  productivity. The first 190.7-minute ETA extended that buffered rate indefinitely; a later live
  report exposed only about 52 minutes of Steel Beam runway. ETA now transitions to the objective
  producers' input-supported output after that runway instead of promising the temporary rate.

This is controlled before/after evidence for path attribution, topology invalidation, player-facing
diagnosis, and buffered-rate interpretation. Configured priority input ordering is implemented and
synthetically covered; changing the priority settings on this exact live merger remains an open
controlled experiment.

### Fluids and storage

- Fluid capture uses m³/min; an M2 extractor path that left litres unconverted was corrected.
- Head budgets include factory-settings production pressure, pumps, reservoir column head,
  highest spline elevation, closed valves, and gas exemption.
- Tank farms exposed a semantic error: treating non-full storage as an infinite consumer starved
  downstream refineries and transport loaders. Storage is now transparent at steady state.
- After correction, 1499.7 of 1500 m³/min extracted Crude Oil reached consumers in the validation
  save. The remaining deficit against 4320/min demand was a real extraction shortfall.

### Generator waste (live, creative test world)

A Nuclear Power Plant running Plutonium Fuel Rods, captured live:

| Reading | Value |
|---|---|
| Fuel demand | 0.1 Plutonium Fuel Rod / min |
| Waste installed | 1.0 Plutonium Waste / min, current and design basis, both `bKnown` |
| Producer buildings | 1, located at the reactor's own actor |
| Estimated current output | 0.847 / min |

The stoichiometry closes independently of the implementation: a Nuclear Power Plant is 2,500 MW
and a Plutonium Fuel Rod holds 1,500,000 MJ, so 2500 x 60 / 1,500,000 = 0.1 rods/min, and
`GetAmountWasteCreated` is 10 - giving exactly the 1.0/min observed. Both figures come from the
fuel descriptor, so a modded fuel is measured the same way rather than matched against a table.

The 0.847 current output is the load-following path: the reactor was serving roughly 85% of grid
demand. `sustainable` and `delivered` are 0 with `consumerBuildings: 0` because the waste output
was not belted anywhere - nothing absorbs it, which is the correct reading rather than a failure.

Two earlier observations on the main save are worth keeping, because both are correct behaviour
that could be mistaken for a bug:

- An **empty** reactor produces no waste row at all. `ResolveGeneratorFuel` finds no current fuel
  class, no fuel inventory and no valid pipe fluid, so neither fuel demand nor waste is claimed. A
  Nuclear Power Plant burns Uranium **or** Plutonium rods, which yield different waste at
  different rates; with nothing loaded there is no evidence of which, and guessing would invent a
  number.
- A reactor with no fuel still appears in the **Water** balance, because the supplemental-resource
  path does not depend on the fuel class.

## 6. Transport evidence

### Physical-debugging loop

Actor-tagged graph dumps plus a temporary ping identified an oil-buffer outpost that appeared to
receive impossible fluid. Physical inspection proved the outpost had been intentionally bypassed
and was truly disconnected. This is evidence for retaining actor identity/location in results and
for treating model/physical disagreement as an investigation, not automatically a model bug.

### Positive forced routing

- Closing the competing direct path produced approximately 509.6 m³/min of Crude Oil over train
  edges.
- A slow transport path initially remained at zero because offer-proportional acceptance could
  not bootstrap it. The SlowPathBootstraps regression now opens unmet demand to lagging paths.
- A first “truck” result was rejected because train liquid-platform actor names contained a
  docking-station substring. Type-correct classification replaced actor-name classification.
- After correcting a physically mis-plumbed truck station, simultaneous flow measured
  approximately 465.7 m³/min by train and 171.5 m³/min by truck.

Drone topology is implemented; a positive drone-only flow experiment remains open.

### Rate provenance: met. Typed metadata: deferred to post-1.0.0.

The release gate reads "typed transport metadata and honest measured/unknown rate provenance".
These are two requirements and they are in different states.

**Provenance is met, end to end.** A vehicle edge carries a sentinel capacity meaning "connected,
rate unknown" and never a guessed throughput. `bTransportRateKnown = false` propagates into
`bKnown` on **both** the current and design bases, so a balance whose flow crosses a route is
never presented as known - the honesty is enforced in the data, not merely in wording. It is then
explained rather than left as a silent blank:

- blocker text - "Vehicle route connected - delivery rate unknown because throughput is not yet
  measured";
- Balance row notice - "Transport route is known; vehicle throughput is not yet measured";
- the same in the Markdown export;
- `ECPFlowUnknownReason::TransportRateUnknown` carries the reason through the model.

**Typed metadata is deferred**, recorded here so it does not fall silently out of scope. `bTransport`
is a single bool: there is no transport-mode enum, no carrier or service identity, no per-route
item filter, and no degraded-health signal. The deferral is defensible because none of it can
change a number - every route is unknown either way, so typing the edge would improve the
*explanation* ("crosses a train route" rather than "a vehicle route") and nothing else.

**Measured rates are the open policy decision**, not a separate gap: shipping "connected, rate
unknown" is the current behaviour, and measuring throughput would be new capability rather than
the closing of an honesty hole.

## 7. Performance baseline

> **The first baseline below is SUPERSEDED.** It is kept because it is the measurement the
> release gate was written against. The current figures are in "Re-measured after sparse item
> iteration" immediately after it.

### Original Phase T baseline (superseded)

A live Phase T report captured 4,563 nodes and 5,302 edges with no truncation. Current and design
solves both converged in 83 iterations and published 1,481 consumer deliveries.

- Capture: approximately 63 ms.
- Dual flow solve: approximately 2.98 seconds.

The capture meets its current reference budget; the solve does not meet a release-quality
interactive budget. Performance hardening is a release gate, not optional polish.

### Re-measured after sparse item iteration

The 2.98 s figure above predates the solver change that iterates only the items a node can
actually carry. Re-measured on the largest available save - no truncation of any kind
(`bAnyCapHit: false`), so this is a COMPLETE analysis, not a capped one:

| Scope | |
|---|---|
| Production buildings | 936 of 948 |
| Connectivity buildings walked | 21,888 of 21,888 |
| Connectivity states propagated | 22,964 |
| Flow graph | 4,588 nodes / 5,214 edges |
| Flow evidence | Available - both solves converged |

Five consecutive live runs through the modular-feature endpoint:

| Stage | Time | Thread |
|---|---|---|
| Snapshot collect | 58–67 ms | game |
| Flow capture | 33–34 ms | game |
| Base analysis | ~0.1 ms | game |
| Dual solve | 99–105 ms | **thread pool** |
| **Game-thread total** | **92–102 ms** | |
| Wall clock | 191–207 ms | |

The dual solve is therefore about **29x faster** than the figure above, on a comparable graph.

Two structural points this measurement establishes, both of which the single combined
`flowMs` number previously obscured:

- **The panel already solves off the game thread.** `UCPPanelSubsystem` hands the dual solve to
  the thread pool and guards completion with a `RefreshGeneration` counter, so a result from a
  superseded refresh is discarded rather than presented. The asynchronous and cancellation-safe
  requirements are met by construction.
- **What the player feels is ~95 ms, not 2.98 s.** Roughly six frames, once, and only on an
  explicit open or refresh - nothing re-runs the analysis on a timer. `flowMs` lumped a
  game-thread actor walk together with an off-thread solve, which is why the solve cost read as
  stutter it never caused. Artifacts now report `flowCaptureMs`, `solveMs` and `gameThreadMs`
  separately so this stays measurable from a live game instead of estimated.

Remaining exposure: `collect` is ~60 ms of the ~95 ms and scales with building count, so a save
materially larger than 21,888 buildings would push the stall up. Time-slicing the collect walk
across frames is the lever if that becomes necessary; it is not justified by this evidence.

## 6a. Diagnostic controls

Writing the flow graph to disk is a diagnostic action and now requires consent. It was previously
gated only on a solve failing to converge, which meant a player who had never asked for
diagnostics could have several thousand nodes and edges serialised into `Saved/` silently - a
condition is not a control, and nothing announced that it had happened.

`cp.DumpFlowGraph` governs it, defaulting to off:

| Value | Behaviour |
|---|---|
| `0` | Never write. Default. |
| `1` | Write only when a solve fails to converge. |
| `2` | Write on every analysis. |

A successful write is logged with node/edge counts, size and path, so the artifact is
discoverable rather than found by accident; a failed write warns rather than passing silently.

## 7a. Lifecycle and unknown-state matrix

Every other suite asks whether the analysis gets the right answer. This one asks the opposite:
when it CANNOT get an answer, does it say so rather than invent one. A confident zero during a
cold start or a failed capture is worse than a blank, because the player acts on it.

Covered by `CriticalPath.Lifecycle.*`:

| State | Required behaviour | Evidence |
|---|---|---|
| Base analysis only | `FlowEvidence = NotAttempted`, never `Available` | `BaseOnlyDoesNotClaimFlowEvidence` |
| Empty world | No objectives, sufficiency, plan or truncation invented | `EmptyWorldIsNotAFailure` |
| Snapshot cap hit | `Truncation.bAnyCapHit` and its counts survive into the result | `SnapshotCapHitIsDisclosed` |
| Non-converged solve | Derived values remain, every one marked `bKnown = false` | `NonConvergedRatesStayUnknown` |
| Current vs design | Two separate readings; installed capacity basis-invariant | `CurrentAndDesignAreSeparate` |

`CurrentAndDesignAreSeparate` pins a contract that was previously only implied: **installed
capacity is deliberately identical on both bases.** It describes what the placed machines could
make, not what they are doing, so it must not shrink when a machine stalls - otherwise the
panel's "built capacity" bar would drop every time the factory hiccuped. The bases differ in
flow, and the test asserts that separation directly (design delivers 60/min, current 15/min).

### Not covered here, and why

- **World loading** and **stale data age** are owned by `UCPPanelSubsystem`, which is UObject-bound
  and outside this UObject-free module. They need a play-in-editor or live check.
- **Flow capture failure** has a defined contract (`FlowEvidence = CaptureFailed`, base facts
  retained) but no synthetic test: the failure is only reachable through `FCPFlowCapture::Capture`,
  which requires a world. The contract is exercised on the live path, not in the suite.

### Fluid transport: the gate's wording, corrected

The release gate lists "approximate fluid transport" as a state to validate. Examined against the
implementation, **the premise is wrong and no unknown-state work is owed.**

Fluid feasibility is **binary, not uncertain**. `CPFlowCapture` seeds a head budget from
producers, pumps (`GetDesignHeadLift`) and reservoir columns, forbids reverse traversal, treats a
zero-head pump as a valve, and checks each pipe run's highest spline point rather than its
endpoints. If the budget does not clear that high point the edge is marked blocked
(`CPFlowCapture.cpp`, "head-capped: the line cannot climb"). A blocked edge then carries exactly
nothing - asserted by `CriticalPath.Flow.BlockedEdgeCarriesNothing`. There is no marginal band to
express as an approximation, and no derating to hedge.

Because feasibility is settled before rates are computed, a fluid rate rests on the same
steady-state basis as a solid one: pipe throughput is a known constant, as belt throughput is.
Neither reproduces the game's transient behaviour, and neither is presented as if it did.
Labelling only fluids as approximate would imply a softness the model does not have, and would
spend the reader's scepticism in the wrong place.

Residual, and stated plainly: the head-lift arithmetic itself runs during game-thread capture and
is therefore verified live rather than in the synthetic suite, which begins at the flow graph. The
solver-side consequence of that arithmetic is covered.

## 8. Interface evidence

- Elevator and Milestone columns render side by side.
- Prospective rows place the item name above status/rate evidence.
- Long unknown-sufficiency text wraps within its owning column and does not cross the divider.
- Disclosure chevrons rotate with collapse state.
- Concurrent machine issues render as separate red reason rows.
- Hard-stopped lines suppress the amber limiter row.
- Recipe-locked parts either expand a research payment plan or cross-reference the selected
  Milestone column.
- The verdict band renders deduplicated bottom-up build tiers.
- Balance lines expand into producer and consumer endpoints with problem nodes first and a locate
  action per building. Machine-state estimates and solver-modeled rates remain visibly distinct.
- Balance line and endpoint evidence includes installed Power Shard and Somersloop counts; the
  augment filter narrows both line rows and expanded machine rows.

Phase C must repeat packaged-game visual validation for current/design rates, the four-rate item
balance, transport state, edge/head-lift limiters, splitter starvation, buffered runway, and locate
actions.

## 9. Operational safeguards

- Never queue automation tests through editor Python. A previous attempt crashed the editor in
  SlateCore recursion. Editor-open work is Live Coding only; tests use the headless command.
- After an editor crash or close, rebuild on-disk binaries; Live Coding patches are memory-only.
- The editor exposes white placeholder copies of many base-game textures. Verify icons in the
  shipping game.
- Use LogCriticalPath; LogTemp is muted by project configuration.
- Fluids are litres internally and m³ in every public/player-facing result.
- Somersloop production boost scales outputs only; clock scales inputs and outputs.

## 10. Phase C and release gates

Phase C migration gates:

1. Map flow deliveries into public sufficiency structures without double counting. **Implemented
   and compared on the controlled Phase 3 and Phase 5 saves.**
2. Preserve current and design bases explicitly. **Implemented in `FCPItemSufficiency`; the legacy
   scalar fields temporarily alias the design basis for compatibility.**
3. Attribute limiters to the actual path and distinguish machine from edge constraints. **Implemented
   and live-validated on the controlled Versatile Framework repair above:**
   parent-machine identity selects the domain, staged design losses distinguish installed capacity
   from input support and delivery, and only a saturated shared-flow edge is named. Additional
   edge/head-lift and transport wording cases remain.**
4. Run automated tests and dual-path comparisons on both reference saves. **Complete for the
   controlled Phase 3 manifold save and Phase 5 megabase.**
5. Explain every material difference; do not preserve known M2 inflation. **Complete for the
   public sufficiency, blocker, limiter, runway, and ETA swap-over.**
6. Validate packaged panel wording and layout. **Validated for the controlled bottleneck and
   runway cases; transport-specific and edge/head-lift wording remain release gates.**

Before release:

1. Add generator fuel/water demand and waste/byproduct production.
2. Add typed transport metadata and measured/unknown rate provenance.
3. Gate graph dumping behind an explicit diagnostic control.
4. Bring solver performance into an interactive budget without weakening honesty.
5. Complete locate/ping interaction and augment audit or explicitly defer them in the release
   scope.
6. Verify loading, empty, error, truncated, stale, non-converged, current, design, and
   approximate-fluid states.
