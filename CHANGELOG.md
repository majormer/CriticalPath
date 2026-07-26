# Changelog

All notable changes to Critical Path will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Audience note:** This changelog is read by players, not developers. Entries describe what you experience in game — what it shows you, and why. Class names, internal APIs and implementation details belong in code comments, not here. Unless an entry says otherwise, everything applies to both single-player and multiplayer.

---

## [1.0.0] - 2026-07-25

> *First release. Critical Path reads the factory you are standing in and answers one question: what is actually stopping your current objective?*

### Added

- **The Critical Path panel, on `F10`.** Opens over the game, shows your current Space Elevator phase and HUB milestone side by side, and closes with `F10` or `Esc`. The key is rebindable under **Options → Keybindings → Critical Path**. Press **REFRESH** to re-measure; the header always tells you how old the reading is.

- **PATH — every objective part, with the reason it is not finished.** Expand a part to walk its dependency chain down to the deepest cause. Blockers are attributed to a specific machine rather than to the product, so you are sent to the thing that is actually wrong instead of the thing that looks wrong. A machine that is unpowered *and* unfed *and* clogged reports all three, rather than only the first one the game happens to mention.

- **BALANCE — what each line needs, what you built, and what is actually flowing.** Four rates per item: consumer need, built capacity, estimated current output and estimated current use. Built capacity does not shrink when a machine stalls, so the gap between capacity and output is the problem, stated plainly. Rows also show the supply route, what the line would manage at full speed, and how long your stored buffer covers the shortfall.

- **PLAN — an ordered build sequence.** Steps are grouped by dependency depth, so everything inside a step is independent and step 1 is always buildable right now against what you already produce. Each item says what it is for individually, and terminal parts say which objective they deliver.

- **Research appears as a step in the plan, not a footnote.** When completing the plan's own lines is what finishes the research that unlocks a later part, that is one chain and it reads as one — including the case where a single milestone unlocks both a part and an ingredient that part needs, so the second line is not a surprise afterwards.

- **Belt jams are identified as jams.** A machine fed by a full belt still starves if the item at the head of that belt is something it cannot use, because nothing behind it can pass either. Critical Path names the offending item instead of reporting a generic shortage — the usual culprit being stray ore left on an ingot line.

- **Locate markers.** Rows that identify a building can drop a marker on your map and compass, cleared again with **CLEAR PINGS**.

- **Fluid routes are checked for physical plausibility.** Pump head, reservoir columns, closed valves, reverse traversal and the highest point of each pipe run all feed a pass/fail check made before any rate is calculated. A pipe that cannot climb is reported as exactly that, rather than as a vague connectivity problem.

- **Nuclear waste is treated as production.** A reactor is modelled as a producer of its own waste, so a chain that consumes reactor waste can see where it comes from and at what rate.

- **Multiplayer, with analysis performed by the host.** Works on dedicated servers. A joined client says it cannot measure the factory rather than showing a report built from data it cannot see — machine contents and storage are server-side, so a client-side reading would be confidently wrong.

- **Unknowns stay unknown.** Vehicle routes report as connected with unmeasured throughput rather than being assigned a plausible number. A scan that hits its collection limit says how much it examined. A solve that does not settle keeps its values but marks them unknown. The rule throughout is that a wrong number gets acted on, while a blank gets investigated.

- **Read-only and save-safe.** Critical Path never builds, dismantles, configures or moves anything, stores nothing in your save, and can be removed at any time. The analysis is deterministic — the same save produces the same answer, with no AI model involved at runtime.

- **Modded content is measured, not assumed.** Recipes, items and unlocks are read from the running game rather than from a built-in table, so modded recipes and modded objectives are handled on the same path as vanilla.
