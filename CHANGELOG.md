# Changelog

## [1.0.0] - unreleased

### Added

- A single typed `FCPWorldAnalysis::Analyze` entry point for engine consumers. It owns bounded
  live-world capture, base analysis, both current and configured-design flow passes, and Phase C
  enrichment so dependent mods do not reproduce Critical Path's orchestration internally.

- A factory-wide flow model that follows configured recipes through belts, pipes, storage, and
  scheduled transport instead of treating production as one global pool.
- Separate current-flow and configured-design analyses, so a line that is presently starved can be
  distinguished from one that lacks enough installed capacity.
- Priority-merger input ordering in solved flow: higher configured groups take constrained output
  first, while equal-priority inputs retain steady-state fair sharing.
- A Balance view with alphabetically sorted, text-filterable production networks, an optional
  needs-attention-first order, and item-specific location markers that can be cleared from the panel.
- Fuel-generator demand, including configured fuel and supplemental fluid consumption, in the
  factory flow graph so power-generation shortages appear in connected item balances.
- Fluid-network analysis with pipe capacity and conservative head-lift feasibility; uncertain or
  incomplete connectivity remains visible instead of becoming a false hard diagnosis.
- Scheduled drone, train, and truck links in the logistics graph. Train and truck routes have been
  confirmed carrying positive flow on a live factory; throughput remains labeled as unknown until
  route measurements are added.
- Prospective dependency chains that expand missing production through unlocked default recipes
  and graft onto existing factory lines.
- Item-specific belt and pipe connectivity capture, including connected capacity and producer
  counts with explicit graph-cap completeness metadata.
- Network-scoped configured-sufficiency results: connected supply, configured demand, aggregate
  ratio, limiting-island ratio, consumer activity context, and explicit unknown completeness.
- Prospective graft rows now carry scaled per-minute requirements and compare them with spare
  sustainable capacity in the best single solved design domain. Disconnected factory lines are
  never summed, and incomplete or unmeasured domains remain explicitly unknown.
- Limiter walk (M2.4): every part with a configured producer reports the single worst-ratio
  chain link — limiting-network scope when connectivity is complete, aggregate fallback
  otherwise, with links of unknown ratio disclosed by count rather than guessed. Ties resolve
  to the deeper link. The panel shows the limiter (amber tachometer row) only when it names a
  different link than the blocker; the Markdown export includes it.
- Flow runway (M2.5): sufficiency rows and the limiter report how long stored stock covers the
  configured deficit ("Stored buffer covers ~N min" / "Stored buffer is dry") — a lower bound,
  since machine-internal buffers are invisible to the owned ledger.
- Marginal value (M2.6): the limiter reports what one more machine on its line buys ("+1
  Constructor -> 100%"), using the line's average per-machine rate at current clocks and
  assuming the new machine can be fed and routed like the existing ones.
- Byproduct disposal (M2.7): a fully stalled line with outputs backed up and a consumer-less
  co-product names that co-product as the suspected clog ("Outputs full -- Polymer Resin has no
  configured consumer and may be backing up"). Facts (stall + no configured consumer) are
  certain; which output is physically full is the stated inference -- storage/sink disposal is
  invisible to the demand table.
- Research payment plans (M2.7b): each research gap's unlocking schematic expands into its full
  item-cost ledger, run through the same pipeline as objective parts (banked vs to-produce,
  status, prospective chains) -- and those chains join the build tiers, so milestone payments
  appear in the same build order as production lines. One schematic level only; suppressed when
  the schematic is the currently selected milestone (its live ledger is the milestone column).
- Aggregated build-order verdict: planned lines fold into deduplicated bottom-up tiers (tier 1
  is buildable against existing production today); the verdict band and Markdown export show
  the sequence instead of a single blocked part.

### Changed

- The plan now sees past a research wall. Locked recipes are captured from the game's own recipe
  manager and flagged as locked, so the plan can show what a part will need once it unlocks —
  including an ingredient gated by the same research, which previously appeared only after the
  unlock as a surprise. Modded recipes and modded research work identically; nothing is
  hardcoded. A captured locked recipe never counts as a buildable line.

- Research that gates a tracked objective is now a numbered step in the plan rather than separate
  work listed alongside it. When completing the plan's own lines is what finishes the research,
  that is one chain and it now reads as one. If the timing cannot be proven, the plan says the
  part is not covered instead of guessing where it belongs.

- Plan steps say what each item is for individually, including the objectives a step satisfies
  directly. Previously a step listed consumers for the whole row, so a part that was itself a
  deliverable read as an ingredient of its neighbours, and a line that satisfied a milestone
  never mentioned it.

- "Blocked" is no longer used for work that has simply not been done yet. A red alarm is reserved
  for a line that exists and has stopped — starved, jammed, unpowered — which the player can go
  and fix. A part that has not been built yet, or whose recipe is not researched, now reads as
  outstanding work rather than a fault. Summary counts drop their zeroes.

- Long plan steps wrap instead of running past the edge of the panel.

- Nuclear generators are modelled as producers of their waste, so a chain that consumes reactor
  waste can see where it comes from and at what rate. The waste item and its per-rod count are
  read from the fuel itself, so modded nuclear fuels work the same way.

- The flow-graph diagnostic dump no longer writes itself. It previously appeared in the save
  folder whenever a solve failed to settle, without being asked for and without saying so; it is
  now off unless `cp.DumpFlowGraph` is turned on, and announces itself in the log when it writes.

- Quantitative sufficiency, routing blockers, limiters, prospective graft headroom, and runway
  now come exclusively from disjoint flow domains. If those domains are unavailable, the base
  report keeps the value unknown instead of falling back to overlapping connectivity-island math.

- Objective ETAs now use the observed rate only through the limiting ingredient's stored-stock
  runway, then finish at the input-supported production rate. If that evidence is unavailable,
  the panel withholds the estimate instead of extending a temporary rate indefinitely.
- Limiter text now labels total connected-network demand rather than implying that one objective
  machine requests the entire network rate.

- Path and Balance copy now describes factory conditions and suggested actions in player terms.
  Internal analysis vocabulary such as solver, convergence, domains, and re-solving remains in
  diagnostics instead of leaking into the in-game panel.
- Bottleneck tracing now stops at a fully supplied or observably healthy intermediate instead of
  continuing into an unrelated shortage elsewhere on that intermediate's shared input network.
- Priority mergers now participate as merger nodes in the captured belt graph. They no longer
  sever an otherwise connected production line between an upstream merger and downstream
  consumers. Industrial storage remains a distinct buffered storage node with both inputs and
  outputs represented.

- Public item sufficiency now comes from disjoint solved flow domains with separate current and
  configured-design evidence. Compatibility scalar fields temporarily alias the design basis,
  and unknown-rate scheduled transport keeps the result explicitly unknown instead of becoming an
  infinite-throughput claim.
- Path rate/connectivity blockers and limiters now use solved configured-design domains. Recipe
  hops are matched through the parent machine's captured actor identity, preventing an unrelated
  worse island for the same ingredient from being reported as this chain's limiter; current
  no-power/input/output facts remain snapshot observations.
- Unknown Path delivery now carries explicit provenance. A proven scheduled-vehicle route says
  its route is connected but throughput is not measured, while incomplete/non-converged solves and
  missing parent-machine attribution retain separate unresolved wording.
- Solver-backed limiters now name the constraining stage: insufficient installed machine capacity,
  upstream inputs that cannot sustain the installed line, a proven saturated belt/pipe edge, or a
  broader delivery/routing constraint when no single saturated edge is supported by the solve.
- Marginal value now performs one exact design re-solve for the report's primary actionable
  limiter. It evaluates either one additional average configured machine or the next standard
  belt/pipe tier on the proven edge, allowing the next topology constraint to cap the result.
- Fulfilled objective parts now discard stale limiter evidence. Zero-installed domains without a
  real producer are treated as missing/disconnected supply rather than displayed as limiters, and
  no longer consume the report's one exact marginal re-solve.
- Solver damping and relative convergence thresholds now apply per connected component. A loop or
  very large producer elsewhere in the save can no longer make a smaller healthy line stop at a
  false zero or partial input-supported rate.
- Passive solid-storage/merger loops are modeled as buffered delivery domains instead of
  circulating flow forever. Their stored stock and external belt ingress/delivery gates retain
  their real capacities, while filtered splitter semantics remain outside the collapsed domain.
- Recipe input/output pairs now participate in causal solver ordering, and only nodes inside a
  true dependency cycle receive damping. Deep configured factory chains no longer advance only
  one recipe tier per pass or inherit damping merely because an upstream buffer contains a loop.
- Transport-domain convergence now uses 0.1% relative precision, still below displayed and
  observable factory-rate precision, rather than spending hundreds of passes rebalancing
  sub-item-per-minute shares inside large merger manifolds.
- Storage is treated as transparent for steady-state rate solving while its contents remain
  available to objective ledgers and runway calculations. Banked inventory is no longer mistaken
  for perpetual production.
- Supply checks use connected capacity when the connectivity graph is complete, so stranded
  producers no longer satisfy factory demand.
- A fully stranded producer now reports a definitive not-connected blocker; incomplete scans
  report unknown instead of guessing at a logistics fault.
- Supply ratios are calculated inside each physical transport island before aggregation, so a
  disconnected surplus cannot conceal another island's shortage. Presently idle consumers keep
  their configured design demand and are reported separately as current-state context.
- Item transport form is captured explicitly before analysis, preventing liquid products from
  being split into false belt domains when a refinery also exposes a conveyor connection.
- Balance filtering and sorting remain fixed below the tabs while rows scroll, and every rate now
  displays explicit items/min or m³/min units.
- Balance rows now lead with observed machine-state estimates instead of presenting solver
  allocations as measured throughput. Each connected line expands into problem-first producer and
  consumer endpoints with per-building status, productivity, estimated rate, local fuel buffer,
  and locate actions.
- Balance line chevrons now initialize in their collapsed orientation, Locate keeps the panel open
  for queuing multiple markers, and an augment filter finds lines and individual machines holding
  Power Shards or Somersloops with icon and count evidence.
- Queued Locate markers now use the game's icon-database-backed map-marker rendering path rather
  than its fixed warning ping or texture-less generic marker. They carry the captured buildable
  icon and a machine/item/index caption so nearby machines remain distinguishable.
- Transparent fluid-buffer manifolds are contracted before solving while preserving stored fluid,
  eliminating a live Polymer Resin tank-loop oscillation that made every current rate unknown.
- Machine-state rates remain visible when solver-derived support and delivery are unavailable.
- Output-blocked manufacturers no longer contribute current input demand; their configured demand
  remains in the design basis.
- Prospective-chain rows now place their status and sufficiency verdict beneath the item name and
  wrap within the owning column, matching objective-row hierarchy without cross-column overlap.

### Validation

- A lifecycle and unknown-state matrix covers what the analysis does when it CANNOT answer: base
  analysis that has run no flow pass, an empty world, a capture that hit its cap, a solve that did
  not settle, and the separation between current and design readings. A confident zero in any of
  those is worse than a blank, because the player acts on it.
- Fluid feasibility is binary and is verified as such: a route either clears head lift or is
  blocked, and a blocked edge carries nothing. Because that is settled before any rate is
  computed, fluid rates rest on the same steady-state basis as belt rates and are not hedged in
  presentation.

- All 56 synthetic engine tests pass, covering bounded capture, disconnected networks,
  prospective chains, current/design flow, disjoint-domain sufficiency, unknown-rate transport,
  machine-attributed Path blockers/limiters, storage, fluids, head lift, transport topology, and
  the lifecycle/unknown-state matrix.
- A live 21,888-building factory (4,588 nodes / 5,214 edges) completes both current and design
  solves with nothing truncated: snapshot capture about 60 ms, flow capture about 34 ms, dual
  solve about 100 ms. The solve runs off the game thread, so an open or refresh costs roughly
  95 ms of game-thread time.
- Live before/after testing confirmed that connecting a previously isolated Versatile Framework
  line changes consumer-reachable capacity from 0/12.5 to 12.5/12.5 per minute; the external
  connection walker independently confirmed the resulting directed route.
- The full `FactoryEditor` target builds cleanly.
- M2.3 live validation complete (2026-07-22, repackaged game + external API): the prospective-row
  layout fix renders correctly (name above status/rate, wrapped verdicts, no cross-column
  overlap); naturally occurring disconnected islands prove aggregate sufficiency cannot hide an
  island shortage (Coal aggregate 1.10 across 4 networks with a 585/min island at limiting ratio
  0.00; Iron Plate 1.60 aggregate / 0.00 limiting; Sulfur 2.67 / 0.00); graft verdicts show scaled
  requirements against spare connected capacity (Cable 249% vs Computer 0%); consumer inactivity
  reports separately without erasing configured demand. This evidence predates scheduled transport
  edges and remains useful as the regression baseline for disconnected physical networks.
