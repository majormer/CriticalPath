# Flow Model - steady-state factory flow simulation

This is the implemented architecture and the contract for completing its product integration.
Phase A (typed capture, solids, fluids, dual solve, head-lift gate) and Phase T (drone, train,
and truck edges) are merged and live-validated. Phase C is replacing M2's overlapping
reachability/island heuristics with views derived from this one solved graph.

## 1. Goal

Compute, for the real factory as built, the steady-state item flow on every belt/pipe link and
therefore the ACTUAL delivered rate at every consumer - demand-driven, capacity-constrained,
respecting splitter semantics. Replace "reachable capacity" approximations with numbers.

Non-goals (explicitly): transient/startup dynamics, item-level positional simulation, train
ETA math, exact fluid dynamics (see §5.4), power grid simulation.

## 2. The graph (Layer 0 - capture, game thread)

One typed directed graph per snapshot. All amounts in display units/min (fluids ÷1000).

### Node kinds

| Kind | Source actors | Data captured |
|---|---|---|
| Producer port | manufacturer/extractor output connection | item set (recipe products), max output rate at current clocks (Somersloop on outputs only), machine state (issues) |
| Consumer port | manufacturer input connection | item demand rates at current clocks, machine state |
| Splitter | `AFGBuildableAttachmentSplitter`, `AFGBuildableSplitterSmart` (smart + programmable) | per-output sort rules for smart: item filters, Any, AnyUndefined, Overflow, None |
| Merger | `AFGBuildableAttachmentMerger`, `AFGBuildableMergerPriority` | vanilla inputs share sustained contention; configured priority groups exhaust higher inputs first |
| Storage | `AFGBuildableStorage`, industrial containers, buffers | current fill per item, capacity |
| Sink | `AFGBuildableResourceSink` | infinite acceptor |
| Transport port | train cargo platform, truck station, drone port | mode (load/unload), transport edge link (Phase T, §8) |
| Passthrough | lifts, poles-with-belts, valves (open), pipeline junctions | - |

### Edges

- Conveyor edge: capacity = belt mk throughput (60/120/270/480/780/1200). Whole belt RUNS
  collapse to one edge via `AFGConveyorChainActor::GetChainSegments()`;
  capacity = min over the chain's segments. Zombie/degenerate chains (0 segments, phantom
  items) are skipped defensively.
- Pipe edge: capacity = pipe mk throughput (300/600) - with the fluid caveats of §5.4.
- Direction from connection component direction (input/output); bidirectional pipe segments
  get a single undirected edge resolved by the solver's net-flow direction (§5.4).

### Caps & honesty

Node/edge caps carry explicit truncation flags. A truncated graph yields UNKNOWN flows - never a
guessed number.

Measured on the large live-validation save (21,888 buildings, 4,588 nodes / 5,214 edges, no cap
hit): capture about 60 ms, flow capture about 34 ms, and the dual solve about 100 ms. The solve
figure previously recorded here was near 3 seconds; sparse item iteration - each node iterating
only the items it can actually carry - accounts for the difference. The panel runs the solve on
the thread pool, so the game-thread cost is roughly 95 ms on an explicit open or refresh. See
`Validation.md` §7 for the full breakdown and the remaining exposure.

Re-measured 2026-07-26 on a community endgame save (Dead-Again's, 8,808 manufacturers / 65,868
buildings / 20,683 storage containers, 39,245 nodes / 19,840 edges, `bAnyCapHit: false` after the
1.0.1 cap raise): collect 171 ms, flow capture 183 ms, dual solve 406 ms, total 760 ms, of which
354 ms is game thread. That is the number to quote for a finished factory - the 21,888-building
figures above came from a save with only 948 production buildings, and the old caps were bailing
out early on anything larger, so they flattered the result.

## 3. The solver (Layer 1 - pure, UObject-free, unit-testable)

### Semantics

Steady state, demand-driven, capacity-constrained:

- A consumer PULLS up to its demand rate.
- A producer PUSHES up to its max rate; actual output = min(max, what downstream absorbs)
  - backpressure is first-class (this is how Satisfactory works: full outputs stall lines).
- A vanilla splitter divides inflow EVENLY across outputs that can absorb it; when an output
  saturates (edge capacity or downstream refusal), its residual redistributes evenly to the
  remaining outputs (water-filling).
- A smart/programmable splitter routes per its rules first (item filters, None = closed),
  Overflow ports take only what filtered/Any ports refuse; Any behaves as vanilla among its
  peers; AnyUndefined = items not matched by a specific filter elsewhere on the splitter.
- A merger sums inflows up to its outflow capacity; when the output saturates, input
  admission is proportional-fair (approximation of the game's round-robin; see §5.2).
- Storage is transparent at steady state (§5.3): pass-through node; fill LEVEL feeds runway.
- The awesome sink absorbs unboundedly.

### Algorithm

Per weakly-connected component (island), per item where separable (splitters make items
interact only at smart splitters; the solver operates on multi-item flows at those nodes):

1. Initialize every consumer's request = its demand; propagate requests upstream
   (demand pass), splitting per splitter semantics in reverse.
2. Propagate availability downstream from producers (supply pass), honoring capacities and
   splitter distribution (water-filling at each splitter given downstream acceptance).
3. Alternate demand/supply passes until the largest per-edge delta is below the absolute
   epsilon or 0.1% of transport-domain producer supply. Damping applies only inside true
   dependency cycles; recipe input-to-output seams participate in the condensed causal order.
   The default iteration cap is 96. Non-convergence publishes no flow numbers as truth.
4. Outputs: per-edge flow, per-consumer delivered/demand, per-producer actual/max, convergence
   history, and the worst-changing edge/item for replay diagnostics.

Monotonicity note: with fixed acceptance the water-filling step is monotone and bounded, so
plain factories converge in a handful of passes. Strongly connected passive solid-storage/merger
loops collapse into one buffered routing domain: stock aggregates, internal circulation
disappears, and external belt edges remain capacity-bearing ingress/delivery gates. Splitters are
not collapsed because their port rules remain semantically significant.

### Machine-state coupling

A machine's current demand exists iff it could run (powered, unpaused, not hard-stalled by its
own outputs). Independent Issues capture gates the current basis while retaining configured
rates. The engine runs both solves today:

- current basis - dead/unavailable machines contribute zero;
- design basis - every configured rate remains present.

The Balance view also publishes a separate machine-state estimate. Producer output and consumer
use are configured rates multiplied by the game's rolling productivity; fuel-generator current
rates also incorporate grid load. Generator endpoint evidence includes its local fuel inventory.
These estimates prove that machines are operating and consuming, but are not metered belt/pipe
throughput. Solver support and delivery remain
explicitly labeled as modeled values. Output-blocked manufacturers contribute zero current input
demand while retaining their configured design demand.

Presentation must name the basis and never combine current supply with design demand.

## 4. Derived views (Layer 2 - subsumption map)

| Today (M2) | Becomes |
|---|---|
| Reverse-reachability connectivity | flow-possible (edge on any path with positive residual) - DELETED as separate code |
| FlowNetworks islands | connected components of the graph - kept for grouping/keys only |
| Sufficiency (limiting island ratio) | per-consumer delivered/demand, aggregated per item; island hedge GONE |
| Limiter (worst island factory-wide) | min-ratio solved domain ON THE PATH, attributed to installed capacity, upstream support, or a proven saturated delivery edge |
| ProducerNotConnected | producer with zero absorbable output despite demand elsewhere |
| Runway (stock/deficit) | storage fill ÷ solved net drain at that storage |
| Marginal value | one exact re-solve for the primary actionable limiter: +1 average line machine or next-tier proven edge |
| Byproduct suspicion | solver shows the byproduct edge at zero absorption - proven, not suspected |

Public result structs keep their shapes; fields that lose meaning are deprecated explicitly.
Blockers/issues/planned chains/tiers/Plan are unchanged in concept, quantified from flows.

## 5. Approximations (each labeled in output, never silent)

1. **Solids solver fidelity**: splitter water-filling and proportional-fair mergers are
   steady-state idealizations of item-granular round-robin. Error is bounded and small at
   scale; per-item granularity artifacts (e.g., alternating singles) are out of scope.
2. **Mergers**: equal-priority inputs use proportional-fair admission vs the game's round-robin  - 
   equal under sustained pressure, differing briefly under sparse flow. Priority mergers preserve
   the configured high-to-low group order while retaining that approximation within each group.
3. **Storage**: transparent pass-through at steady state. A container simultaneously filling
   and feeding is represented by net drift, not by its transient buffering behavior.
   Bidirectionally connected fluid buffers collapse with their pipe manifold before solving;
   their captured stock is merged into the contracted node rather than discarded.
4. **Fluids**: junctions modeled as vanilla splitters; head lift enters as a CONNECTIVITY
   GATE (§11), not a physics sim - a line that cannot climb is not connected. Flow amounts
   are tagged `bFluidApproximate`. If validation shows unacceptable error, fluids degrade to
   M2.3-style island aggregates rather than shipping wrong numbers.
5. **No local player / client**: capture is server-side (inventories and back-pointers are
   server-only); client-side degradation follows the identifier-only rule from the transport
   research.

## 6. Validation plan (the long pole, by design)

1. Synthetic suite: hand-built graphs with known closed-form answers - even split, overflow
   redistribution, smart filter routing, merger contention, loops, capacity pinch, storage
   drift, non-convergence honesty. These are the new unit tests.
2. Cross-check vs M2.3 during Phase C: comparison found M2.3 double-counting consumers through
   overlapping network rows; the raw snapshot demand table agreed with the flow analysis on all
   91 items after fluid capture. Controlled Phase 3 and Phase 5 saves then validated path-scoped
   blockers, priority-merger routing, buffered runway, and objective ETA. The overlapping-island
   calculation has been retired from public quantitative results.
3. **Observed-statistics diff**: when `FGStatisticsSubsystem` data is healthy, compare solved
   per-item production/consumption against the game's own measured numbers on live saves  - 
   the solver self-reports its error. Persistent unexplained error > ~10% on solids blocks
   the swap-over.
4. Live probes on both reference saves (Phase 4 sparse and Phase 5 megabase) via the API.
5. Forced-routing experiments for every transport mode and closed-valve/head-lift cases.

## 7. Migration plan

- **Phase A - complete and merged:** typed capture, solids, fluids, head-lift gate, pure solver,
  dual current/design basis, replay diagnostics, and synthetic tests.
- **Phase B - evidence established, continues as a regression gate:** raw-demand parity,
  explained M2.3 divergences, two live saves, fluids, tank farms, closed valves, and real loops.
- **Phase T - topology complete and merged:** drone, train, and truck services bridge transport
  ports. Trains and trucks have positive forced-routing flow validation.
- **Phase C - active:** public item sufficiency now aggregates disjoint solver domains with
  explicit current/design bases and unknown-rate transport honesty. Path rate/connectivity blockers
  and limiters use those domains and follow parent-machine identity across recipe hops. A limiter
  walk stops when an intermediate's required delivery is satisfied, or when every producer on the
  matched line is observably producing/backpressured with enough installed capacity; shortages
  elsewhere on that intermediate's shared upstream network are not attributed to the objective
  chain. Limiters
  classify installed-capacity, upstream-input, saturated belt/pipe, and unresolved routing
  constraints from staged design rates and shared edge flow. The report's primary actionable
  limiter gets one exact machine/edge perturbation re-solve. Prospective graft headroom now uses
  one disjoint sustainable design domain without summing disconnected lines. Controlled
  comparison on both reference saves is complete; public quantitative results no longer depend
  on the M2 reachability/island calculation.
- **Hardening:** measured transport metadata, generator capture, diagnostic gating,
  performance, scanner/locate actions, augment audit, and release UI validation.

## 8. Transport integration

Train platforms, truck stations, and drone ports are routing nodes with ordinary belt/pipe
connections. The capture currently creates directed service edges:

- drone station → explicitly paired station;
- every load cargo platform on a train's timetable → every unload platform in that service;
- load truck station → unload truck station for each autopilot GUID route.

These edges currently use a deliberately unbounded numeric capacity to mean “connected, batched
rate unknown.” That sentinel must not become a displayed throughput claim. The hardening step
adds explicit transport mode, carrier/service identity, item filters, measured-rate provenance,
and degraded health. See [TransportModel.md](TransportModel.md).

## 9. Remaining model extensions

- measured/estimated transport rates with provenance and an explicit unknown state;
- two-snapshot storage drift for transient insight;
- player-facing splitter-starvation rows;

Hypothetical machine/conveyance injection and public scenario queries are future work, not part of
Phase C or the current release target.

## 10. Decisions (settled with maintainer, 2026-07-22)

1. **Fluids: approximate-and-labeled.** No attempt is made to replicate transient fluid physics.
   Head lift is nevertheless a connectivity gate: a line that cannot climb does not count as
   connected. The implemented gate follows the mechanics recorded in §11.
2. **Marginal value: full re-solve, including belt-upgrade suggestions.** When the pinch is
   an EDGE (belt/pipe capacity) rather than a machine, the prescription says so: "+1 mk on
   this belt run → N%" alongside "+1 machine → N%".
3. **Splitter-starvation diagnostics: in the MVP panel** (rows, not API-only).

## 11. Head-lift mechanics (researched 2026-07-22 - code truth, corrects community lore)

Source: FGFluidIntegrantInterface.h (FFluidBox - no FGFluidBox.h exists), FGPipeNetwork.h,
FGPipeSubsystem.h, Buildables/FGBuildablePipelinePump.h, FGBuildablePipeReservoir.h.
All fluid-sim distances are METERS (world cm / 100). The sim's .cpp is closed-source; this is
reconstructed from header fields + CSS's own comments.

Key findings (surprises bolded):

- **Producers DO contribute inherent head lift - via settings, not their own classes**
  (CORRECTED 2026-07-22 after modding-Discord intel from Ionmaster987/MadCat256/AngryBeaver/
  AniMouse, verified in code): `UFGFactorySettings::mAddedPipeProductionPressure` - "acts as
  a default pressure as if there was a pump inside every producing buildable" - applied to
  every producer-type pipe output unless opted out per-connection via
  `UFGPipeConnectionFactory::mApplyAdditionalPressure` (the "checkbox on fluid outputs").
  The VALUE is CDO data (community: 10m; capture reads the real number at runtime). This is
  why water extractors push fluid slightly uphill without a pump. The initial header sweep
  missed it because the property lives on the settings singleton, not the producer classes.
- Pumps are the only ACTIVE head source: `mMaxPressure` / `mDesignPressure` in meters
  (`GetMaxHeadLift()` / `GetDesignHeadLift()`). **The numeric Mk1/Mk2 values are Blueprint
  CDO data, not header constants** - capture must read them from the pump CDO/instance at
  runtime (community reference ~20m/~50m, but read, don't assume).
- **Valves are pumps**: no valve class exists; a valve is `AFGBuildablePipelinePump` with
  `mMaxPressure = 0` plus `mUserFlowLimit` (replicated; -1 = open, 0 = CLOSED - a closed
  valve blocks flow even on a "reachable" line; valves are also one-way).
- **Fluid buffers/towers: passive column head only.** A reservoir is just a big fluid box;
  its fill height + overfill converts to a pressure column (that's the "tower head" lore),
  but it does NOT add pump-style `AddedPressure` and there is NO head-reset flag. Static
  model: a full reservoir acts as an elevation-head source at its top-of-fluid Z.
- The sim's own model per pressure group: reachable ⇔
  `HighestPumpZ + pumpHead ≥ HighestElevationZ` (FPressureGroup fields), with groups broken
  at pumps. Gas fluids skip gravity entirely (any connected gas line is reachable).

### The static connectivity gate (what Phase A implements)

Walk the pipe graph source→consumer with a running head budget:
1. budget starts at the source's elevation (m) PLUS the factory-settings production
   pressure (`mAddedPipeProductionPressure`, ~10m) when the source connection has
   `mApplyAdditionalPressure` set; full reservoirs start at top-of-fluid Z (their column
   head varies with fullness - the code-confirmed "storage tank exception").
2. Each pump traversed IN ITS FLOW DIRECTION adds `GetDesignHeadLift()` (conservative; max
   head is the optimistic bound) and BREAKS the budget group (mirror
   `FPressureGroup.Redirect` / `ShouldBreakPressureGroup`); reverse traversal through a pump
   or valve is forbidden.
3. At every step require budget ≥ the MAX spline Z along each pipe segment (not endpoint Z  - 
   humps count; pipes also float DEFAULT_PIPE_HEIGHT=175cm above ground).
4. `mUserFlowLimit == 0` anywhere on the path ⇒ blocked regardless of head.
5. Fail ⇒ the line is head-capped: NOT connected, reported as its own reason
   ("pipe cannot climb - needs a pump / +Nm of head") rather than generic disconnection.

Known error sources (accepted, documented): overfill pressure gives ~+10% transient column
the static model ignores (slightly pessimistic near the cap); measured cross-check available
via REPLICATED indicator flows (`AFGBuildablePipeline::GetIndicatorFlow()`, pump indicator
head/flow, extractor `mReplicatedFlowRate`) - positive indicator flow at the consumer is
ground truth that overrides a pessimistic static verdict.
