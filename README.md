# Critical Path

**Critical Path** is a goal-based factory diagnostic for Satisfactory, developed by Finalomega
Labs. Production planners are excellent at designing factories, and save-aware web tools can
inspect remarkably detailed snapshots of factories already built. Critical Path occupies a
different niche: **what is stopping this objective in the factory running right now?**

The mod reads the running world's configured machines, recipes, clocks, inventories, belts, pipes,
splitters, storage, and vehicle routes. It then presents the current Space Elevator phase and HUB
milestone side by side, collapses healthy branches, expands broken ones to their deepest cause,
and produces an ordered plan for what to research, build, connect, or repair.

Because the analysis runs in-game, rebuilding a connection, changing a recipe, adjusting a clock,
or restoring a route can be reflected on the next refresh without saving, uploading, and
reinterpreting another external snapshot.

## What it can explain

- what remains to be delivered and what is already banked;
- whether a line is missing, stopped, under-fed, output-blocked, or merely time-limited;
- which planned dependency lines are required before an objective can progress;
- the actual steady-state delivery reaching consumers through belts and pipes;
- the difference between the factory's current operating state and its configured design;
- fluid routes that are blocked by a closed valve or insufficient head lift;
- train, truck, and drone routes that bridge otherwise disconnected logistics networks;
- the limiting machine or conveyance path, buffer runway, and value of the next upgrade.

Critical Path is deterministic and read-only. It never builds, configures, dismantles, or mutates
the factory, and it does not depend on an AI model to reach its conclusions.

## Current development state

The engine, in-game panel, prospective build plan, quantitative analysis, steady-state flow
solver, and train/truck/drone topology capture are implemented and live-tested. Development is
now integrating solver results into the player-facing sufficiency, limiter, runway, and
prescription surfaces, followed by the remaining awareness and release-polish work.

The plugin has two modules:

- **CriticalPathEngine** - live-game capture plus UObject-free analysis and structured results.
- **CriticalPath** - the in-game interface consuming those results.

See [PRD.md](PRD.md) for product scope and roadmap, [docs/AnalysisModel.md](docs/AnalysisModel.md)
for the decision model, [docs/FlowModel.md](docs/FlowModel.md) for the solver contract, and
[docs/Validation.md](docs/Validation.md) for the evidence required before a result is trusted.

## License

Source-available; see [LICENSE.md](LICENSE.md). Official releases come only from
[ficsit.app](https://ficsit.app) and this repository.
