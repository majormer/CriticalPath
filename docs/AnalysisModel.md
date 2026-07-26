# Critical Path — Analysis Model

This document defines how Critical Path turns a live Satisfactory factory into an objective
verdict, a causal explanation, and an actionable plan. It is the durable contract between
snapshot capture, pure analysis, the public API, and every visible surface.

The flow graph and dual-basis analysis are implemented. Quantitative sufficiency, routing
blockers, limiters, graft headroom, runway, and ETA now come from disjoint flow domains. Base
snapshot analysis retains ledger, machine-state, research, and prospective-plan facts, but no
longer manufactures quantitative fallbacks from overlapping connectivity islands.

## 1. Evidence boundaries

Critical Path has three layers with one-way dependencies:

1. **Capture, on the game thread** — reads UObjects and produces bounded plain-data snapshots.
2. **Analysis and flow solving** — UObject-free deterministic code over those snapshots.
3. **Presentation and consumers** — panel, Markdown export, tracker, and public API.

UObject access never leaks into analysis. Presentation never recomputes facts from game objects.
Every cap, missing context, non-converged solve, or server-only field remains explicit in the
result. Unknown is a valid answer; a fabricated zero is not.

All content comes from game registries and descriptor reflection. No vanilla item, recipe,
machine, or objective names are hard-coded into the model.

## 2. Captured factory model

### Objective and catalog data

- outstanding Space Elevator and selected milestone costs;
- delivered, owned, and remaining quantities;
- configured recipe catalog, including unlocked alternates;
- one-level research unlock and payment dependencies;
- machine recipes, clock speeds, production boosts, and machine types.

### Machine facts

Machine state is captured as independent facts rather than one mutually exclusive idle reason:

- powered / unpowered;
- paused or deliberate standby;
- inputs starved;
- outputs full;
- configured current and design input/output rates;
- location and identity for later world grounding.

Those counters may overlap. “No power” must not hide “no inputs,” and neither may erase the
prospective chain needed to make those inputs.

### Owned-stock semantics

Owned stock is partitioned by how it can be used:

- **machine-reachable** — storage, buffers, and in-transit stock on the feeding graph;
- **player-available** — pockets and dimensional depot, requiring manual delivery;
- **orphaned** — finite stock with no automated route to a consumer;
- **objective-delivered** — already paid and never counted again.

Dimensional-depot stock is never treated as belt supply. Exactly funded batches remain funded
even when their buffers reach zero at the same moment the objective completes.

## 3. Typed flow graph

The capture layer constructs one graph in display units per minute; fluids are converted from
litres to cubic metres.

### Nodes

- Producer port
- Consumer port
- Splitter, including smart/programmable routing rules
- Merger
- Storage
- AWESOME Sink
- Passthrough junction, lift, valve, or pump
- Train platform, truck station, or drone port

Each producer and consumer carries two rate sets:

- **Current rates** — unavailable machines contribute zero.
- **Design rates** — configured rates regardless of current machine state.

### Edges

- Conveyor runs collapse to their minimum segment capacity.
- Pipe runs carry tier capacity, direction, maximum spline elevation, and blocked state.
- Closed valves and insufficient head lift block an otherwise geometric connection.
- Train, truck, and drone services create directed transport edges from load ports to unload
  ports.
- Transport capacity remains unknown unless the game provides a trustworthy measured rate.

Graph truncation invalidates quantitative answers for the affected scope. The solver never
presents a partially captured graph as complete.

## 4. Dual steady-state solves

The same pure solver runs twice:

- **Current solve**: what reaches consumers under present machine availability?
- **Design solve**: would the configured factory close if all configured machines were available?

Both are demand-driven and capacity-constrained:

- consumers pull up to demand;
- producers supply only what downstream can absorb;
- splitters water-fill among accepting outputs and honor filters/overflow;
- mergers use proportional-fair steady-state admission;
- storage is transparent routing at steady state;
- sinks absorb without backpressure;
- true dependency cycles use damping and a transport-domain relative convergence tolerance;
- strongly connected passive solid-storage/merger loops become one buffered domain whose stock
  and external belt-gate capacities remain explicit;
- configured recipe input/output pairs participate in causal ordering even though they are not
  transport edges.

Non-convergence means the solve is unknown. Diagnostics retain iteration history and the
worst-changing edge/item for offline replay; they are not player-facing partial results.

## 5. Solver-derived product answers

Phase C makes these the authoritative definitions.

`FCPItemSufficiency.Current` and `.Design` now publish these definitions from disjoint solved item
domains. The aggregate and limiting-domain rates count each consumer once. During the migration,
the older scalar sufficiency fields remain compatibility aliases of the design basis. Path blockers
and limiters now use design-solve domains; dependency hops are restricted to domains whose consumer
actor is the same machine as the parent product's producer actor, so an unrelated worse island does
not hijack the chain. Prospective grafts use the best sustainable spare capacity in one disjoint
design-basis producer domain; disconnected domains are never summed, and an unknown producer domain
keeps the graft result unknown. Any demanded domain that is truncated, non-converged, or crosses transport
with unknown throughput makes that basis unknown, even if diagnostic numeric solver output exists.
Solver-backed blockers retain typed unknown provenance: a proven scheduled-transport route with
unmeasured throughput is distinct from an incomplete/non-converged solve and from a dependency hop
whose consumer domain could not be attributed to the parent machine.

### Delivered sufficiency

For a consumer c and item i:

**consumer ratio = delivered(c,i) / demand(c,i)**

Item and objective summaries aggregate consumer deliveries without duplicating a consumer that
appears on multiple graph paths. Global configured capacity is supporting context, not proof that
any particular consumer is fed.

The interface labels the basis:

- Current: “delivered 85/min of 85/min currently requested.”
- Design: “configured delivery 85/min of 320/min required.”

### Connectivity

Connected means a valid directed path exists through an unblocked graph. Positive solved flow is
stronger evidence: it proves that the path can carry this item under the selected basis.

- A producer with demand elsewhere but no absorbable route is stranded.
- A consumer with a route but zero delivery may be capacity-starved or defeated by routing,
  backpressure, a valve, head lift, or transport.
- A transport edge is disclosed as batched logistics; unknown throughput is never rendered as
  infinite real capacity.

### Blocker and issues

The blocker is the deepest causal reason an objective cannot progress. Issues are concurrent
facts at the part's machines. The two remain separate:

- hard machine issues: no power, paused, inputs starved, outputs full;
- missing capability: no producer or recipe locked;
- routing issues: disconnected, closed valve, head-lift capped, transport unavailable;
- rate issues: delivered flow below demand;
- disposal issues: a byproduct has no absorbable output route.

### Limiter

The limiter is defined only for a chain that is flowing but slower than required. It is the
lowest-ratio machine or edge on the actual path feeding the relevant consumer, not the worst
island with the same item elsewhere in the factory.

Within that solved domain, constraint attribution compares the three staged losses in the design
basis: installed capacity versus demand, sustainable output versus installed capacity, and
delivered flow versus sustainable output. The largest proportional loss is named as machine
capacity, upstream input support, or delivery/routing. Delivery names a specific belt or pipe only
when its shared solved flow is at capacity; otherwise it remains a routing/backpressure constraint.
The public limiter retains the attributed domain, staged rates, and optional edge endpoints and
capacity so presentation and API consumers do not have to reconstruct the diagnosis.

Ties prefer the deeper cause. Hard-stopped chains suppress limiter presentation because “slow”
is the wrong explanation for a dead machine.

Marginal value is an exact perturbation solve for the report's single primary actionable limiter,
not a per-row arithmetic estimate. An installed-capacity limiter adds one average configured
machine by scaling the existing line's recipe inputs and outputs on the same topology. A proven
edge limiter raises only that edge to the next standard belt/pipe capacity. The resulting delivery
therefore exposes the next upstream or downstream constraint. Input-support and unresolved-routing
limiters do not suggest adding capacity. At most one extra design solve is allowed per report.

### Item balance and capacity gaps

For an item within an objective-relevant supply domain, analysis reports four comparable rates:

1. demand from the configured consumers;
2. installed production capacity;
3. sustainable production supported by producer inputs and power;
4. delivered supply reaching those consumers.

The gaps attribute the present factory rather than project a hypothetical one: demand above
installed capacity means insufficient producers; installed above sustainable means producer
inputs/power are limiting; sustainable above delivered means logistics are limiting. The current
release does not inject proposed factories or claim what a future layout would do.

### Runway and storage

Storage is not a competing consumer in steady state. Its inventory provides transient evidence:

- runway = usable stock / solved net deficit;
- funded = usable stock covers the complete remaining objective need;
- dry = no usable stock remains on a deficient path.

An objective ETA that crosses a deficient ingredient's runway is piecewise: current observed
objective output applies only until that buffer is exhausted, and the remaining work uses the
objective producers' input-supported steady-state output. If either rate or the relevant runway
is unknown, the interface withholds the ETA instead of extending the current rate indefinitely.

A future two-snapshot measurement may refine per-container drift, but it must not replace solved
steady-state routing with a momentary fill-rate guess.

### Generator waste

A nuclear generator is the one power source that produces something, and its waste matters twice:
it must leave the reactor or the reactor stops, and it is a real ingredient downstream. It is
captured exactly like a manufacturer's output side — a Producer node paired to the fuel Consumer
node — so output edges originate from it and the solver ties waste production to fuel supply. A
reactor with no fuel produces no waste, without a special case.

The waste item and its per-fuel-item count are read from the fuel descriptor
(`GetSpentFuelClass`, `GetAmountWasteCreated`), not from a table, so a modded nuclear fuel with a
modded waste product travels the same path. Non-nuclear generators are unaffected: coal and fuel
burners have no spent-fuel class and remain consumer-only nodes.

Note that waste **supply** and the spent-fuel **explanation** are separate mechanisms and both
apply. If a chain needs waste and no reactor produces it, the research walk still says "no recipe
by design — burn its fuel in a Nuclear Power Plant" rather than sending the player to look for an
unlock that does not exist.

### Byproducts

Every recipe product is represented. A byproduct is proven blocked when its output has zero
absorbable flow while the machine is otherwise able to run. This replaces the legacy inference
based only on output-full state and absence of configured consumers.

## 6. Fluids and head lift

Critical Path solves fluids at steady state and does not reproduce Satisfactory's transient fluid
simulation — priming, sloshing and junction settling are outside the model. That is the same
basis solids are solved on, so **a fluid rate is not softer than a belt rate** and is not hedged
in presentation. An earlier revision of this document described fluid rates as "approximate and
labeled"; nothing was ever labeled, and the hedge was not earned.

What makes the numbers trustworthy is that feasibility is decided **binarily** before any rate is
computed. A route either clears head lift or it does not; there is no marginal band and no
derating. A head-capped or closed-valve edge is marked blocked at capture and carries exactly
nothing through the solve.

It models whether a route is physically plausible:

- producing connections receive the factory-settings production pressure when enabled;
- pumps add their runtime design head and break pressure groups;
- reservoirs contribute a passive column based on fluid level;
- closed valves block;
- reverse pump/valve traversal is forbidden;
- every pipe run checks its highest spline point, not only endpoints;
- gas skips gravity.

Failure is a distinct actionable blocker: the pipe cannot climb, rather than a generic
“not connected” verdict.

## 7. Transport

Transport ports are graph nodes with ordinary belt/pipe connections. Services bridge their load
and unload sides:

- drones use explicit station pairing;
- trains use timetables and cargo-platform chains;
- trucks use autopilot GUID routes resolved to docking stations.

Current transport edges establish connectivity and participate in solved flow, but use unknown
capacity. Phase C and the following hardening pass add mode/carrier identity, measured-rate
metadata, filters, and degraded health without inventing rates. See
[TransportModel.md](TransportModel.md).

## 8. Prospective planning

When production does not exist, Critical Path walks the unlocked default recipe catalog:

- prefer the non-alternate recipe;
- flag available alternates without silently choosing one;
- stop at existing production or raw resources;
- retain research-locked nodes and their payment ledgers;
- scale requirements from a one-default-machine root through recipe stoichiometry;
- deduplicate results into bottom-up build tiers.

Existing-but-stopped production does not terminate the plan. Its missing ingredients remain
visible while the row also says to fix the stopped machine.

### Seeing past a research wall

A locked recipe used to end the walk: the item was known to be locked and nothing more. Anything
the plan placed after the unlock therefore implied the unlock was the last obstacle, which is
frequently false — one milestone commonly gates a part **and** an ingredient that part consumes,
which is an entire additional line to build.

Locked recipes are now captured too, flagged `bRecipeLocked`, and the research-gap walk descends
through them. Everything is read from `AFGRecipeManager` at runtime (`FindRecipesByProduct` with
`onlyAvailableRecipes = false`, plus `IsRecipeAvailable`), so modded recipes behind modded
research behave identically — nothing is hardcoded or table-driven.

**Captured knowledge of a future line is never evidence of a buildable one.** Chain nodes for a
locked recipe stay `ResearchLocked`, so every consumer keeps treating them as gated; the capture
exists only so the plan can answer "what will this need once it unlocks" *before* the unlock.

### Research as a step, not a parallel track

When the research gating a locked part is **itself one of the tracked objectives**, the ordering
is fully determined: build that objective's outstanding lines, the research completes, the part
becomes buildable. The plan emits an `FCPUnlockStep` in sequence rather than describing the
research as separate work, because it is not separate — it is a link in the same chain.

The guard is deliberately strict. An unlock step is emitted **only** when the gating research is a
tracked objective *and* every outstanding requirement of that objective is already a line in this
plan. Otherwise the timing is unproven, and the plan says so instead of inventing a position.

Locked items are then tiered against each other by a fixpoint over the captured recipes: sitting
one tier above the unlock is only a floor, since a single milestone often unlocks a part and an
ingredient of that part, which must be ordered relative to each other.

### What a step is for

Two distinct relationships, kept apart because they answer different questions:

- `NeededFor` — items that consume this step's output; derived from ingredient→parent chain edges.
- `NeededForObjectives` — objectives this step directly satisfies.

The second exists because a part an objective asks for is a chain **root**, not anyone's
ingredient, so it never appears in `NeededFor`. Without it, a terminal deliverable rendered with
no stated purpose at all, and a pivot line that satisfies a milestone while also feeding later
steps concealed the very relationship that makes a later unlock reachable.

Consumers are attributed **per item**, not per step. A step can hold several items that merely
share a tier; a union across the row claimed every item in it fed every consumer named after it,
so a terminal deliverable read as an ingredient of its neighbours.

## 9. Scarce augments

The augment audit is a planned consumer of the same model:

- **wasted** — placed in a stopped, saturated, or standby machine;
- **permanent-value** — compounds finite world-gathered inputs and is protected;
- **throughput** — renewable chain placement ranked against the objective's marginal value.

Scarcity is derived from recipe availability. Power Shards cease to be treated as finite when a
producing recipe is unlocked.

## 10. Interface contract

Rows remain concise; detail is progressive:

- primary line: icon, item, status/activity, and objective ledger;
- secondary line: current delivered/demanded rate and concise cause;
- expanded evidence: design basis, machine issues, blocker path, limiter, transport/head-lift
  annotations, item balance, and runway;
- action: expand/collapse, refresh, and locate/ping where coordinates exist.

Unknown, approximate, current, and design are visible words—not tooltip-only qualifications.

## 11. Migration and compatibility

Phase C compatibility rules:

- current and design flow solves provide the public quantitative result;
- sufficiency, machine-attributed Path blocker/limiter interpretation, prospective-graft
  headroom, and the primary limiter's exact marginal re-solve use disjoint domains; the catalog
  planning walk remains the non-quantitative prospective recipe source;
- compatibility scalars explicitly use design-basis meaning while current evidence is available
  in a separate structured basis;
- public structures keep compatibility fields or explicit deprecations;
- when flow evidence is unavailable, quantitative values remain unknown instead of reviving the
  old reachability/island calculation.

The known M2 defect—overlapping flow-network rows double-counting the same consumers—is a reason
to migrate, not evidence to preserve its answers.
