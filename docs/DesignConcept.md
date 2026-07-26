# Critical Path - Product and Interface Concept

Critical Path is a live-factory diagnostic, not a recipe-tree viewer. It begins with a player's
real objective and real configured factory, explains why progress is or is not happening, turns
that evidence into the smallest useful action, and helps the player find the relevant place in
the world.

The concept originated in a live-save investigation documented by
[SpaceElevatorExample.md](SpaceElevatorExample.md). That prototype proved the core interaction:
healthy branches become quiet; the path that needs attention becomes legible.

## The product promise

The complete experience answers four questions in order:

1. **What do I owe?** Remaining objective cost, banked stock, and research payments.
2. **What is happening?** Current delivered rate, configured design demand, and completion time.
3. **Why?** The deepest blocker, every concurrent machine issue, and the actual limiting path.
4. **What should I do and where?** A build/fix/research prescription plus a locate or ping action.

That sequence is the richer vision for the mod. A correct engine without a clear visible answer is
unfinished; an attractive panel backed by aggregate guesses is equally unfinished.

## The critical path is a distillation

The factory is a graph, but the default interface is not. Rendering the whole graph would bury the
answer in infrastructure.

- Fulfilled branches collapse to one row.
- Broken or planned branches expand only as far as the evidence requires.
- The limiter is emphasized only on a supplied-but-slow path.
- Hard stops use blocker and issue language, never a speedometer metaphor.
- A dead machine is a window into its missing inputs, not a wall that hides the build plan.

The player can expand a healthy branch for evidence, but the five-second default view remains an
objective, a verdict, and the one path that deserves attention.

## Four explanations that must remain separate

Live testing established an important information contract:

- **Blocker** explains the dependency chain: the deepest reason the objective cannot progress.
- **Issues** explain the machines at the selected part: no power, inputs starved, outputs full,
  standby, or another simultaneous condition.
- **Plan** explains what does not exist yet: research payments and bottom-up build tiers.
- **Limiter** explains a path that is flowing but slower than required.

None may swallow another. A machine can be unpowered, input-starved, and output-blocked at the
same time. The interface reports those facts together while still showing the planned upstream
work.

## Two truths: current operation and configured design

The live factory has two legitimate interpretations:

- **Current** asks, “Are the machines that can run right now actually being fed?”
- **Design** asks, “If every configured machine were available, would this factory close?”

An unpowered consumer has zero current demand but retains configured design demand. An idle
producer contributes zero current supply but keeps its configured capacity. The interface must
name the basis near every rate conclusion; it may not silently mix the two.

The default objective verdict leads with the actionable current state. Design sufficiency appears
as supporting evidence and becomes primary when the player is evaluating whether the built layout
is fundamentally large enough.

## From reachability to solved flow

The first quantitative implementation partitioned producers and consumers into belt/pipe islands.
That removed global false confidence, but overlapping network rows double-counted consumers and a
reachable producer could still deliver nothing through a bottleneck.

The implemented flow graph is the durable model:

- producer and consumer ports carry current and design rates;
- belt and pipe edges carry real capacity;
- splitters, mergers, storage, sinks, valves, pumps, and junctions retain their routing semantics;
- train, truck, and drone services bridge their load and unload ports;
- a pure fixed-point solver reports per-edge flow and per-consumer delivery.

Connectivity, sufficiency, limiter attribution, runway, and marginal value become views over one
answer instead of independent heuristics. The old island analysis remains only as a migration
comparison until Phase C proves and removes it.

## Visible information architecture

The current panel is a centred modal workspace opened by the configured key. Phase C organizes its
increasingly rich evidence into one report-level tab strip - **Path · Balance · Plan** - rather than
extending the old M2 body indefinitely:

1. **Header** - title, data age, refresh/close affordances, and lifecycle state.
2. **Path** - Space Elevator phase and selected HUB milestone side by side; part rows, concurrent
   issues, blocker path, limiter, and concise verdict remain the five-second default.
3. **Balance** - the ingredient selected from Path; four-rate balance, supply-domain identity,
   producer/consumer evidence, buffer composition, and runway. It is objective-scoped in Phase C.
4. **Plan** - research payments, prospective chains, and deduplicated bottom-up build tiers.
5. **Verdict band** - the highest-value present-factory action or trustworthy completion estimate
   remains visible across tabs.

The tab strip is navigation within one report, not three independent dashboards. Selecting an
ingredient on Path sets the Balance context; returning to Path preserves the selected objective
and scroll position. Each part row must be able to disclose:

- delivered / demanded rate on the current basis;
- configured design rate and whether the design closes;
- whether transport is involved and whether its throughput is measured or unknown;
- machine versus belt, pipe, splitter, valve, head-lift, or transport limitation;
- the four-rate item balance and buffered runway;
- a locate/ping action when the evidence has world coordinates.

## World grounding

“Fix Stator” is incomplete when the player owns hundreds of Constructors. Findings retain actor
identity and world location so the interface can create temporary compass/map pings for:

- the blocking or limiting machine;
- the belt, pipe, valve, pump, splitter, or transport port carrying the constraint;
- the minimal set of storage containers covering a payable objective;
- recoverable scarce augments.

Pings are timed, dismissable, and use the relevant item or status icon. They are guidance, never
world mutation.

## Objective and stock semantics

- Delivered objective progress is authoritative and never double-counted with banked output.
- Pocket and dimensional-depot stock is player-available, not machine-reachable.
- An exactly funded batch is “funded - reaches zero at completion,” not an alarming drain.
- Storage containers are transparent routing nodes at steady state; their contents explain
  runway, collection errands, and transient resilience.
- Manual transfer is a truthful plan step only when owned stock covers the remaining need and
  production no longer gates completion.

## Scope boundary

Critical Path names the present gap - insufficient installed production, unsupported producers, or
delivery loss - and may identify the existing machine or route responsible. It does not project a
hypothetical new factory, design layouts, or build anything. It reports only facts the player could
discover by auditing the same factory.

The engine is deterministic and UI-independent. The panel, optional tracker, export, and external
consumers are views over the same structured result; no presentation layer owns a second analysis.
