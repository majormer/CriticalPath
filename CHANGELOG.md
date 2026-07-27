# Changelog

All notable changes to Critical Path will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

> **Audience note:** This changelog is read by players, not developers. Entries describe what you experience in game - what it shows you, and why. Class names, internal APIs and implementation details belong in code comments, not here. Unless an entry says otherwise, everything applies to both single-player and multiplayer.

---

## [1.0.2] - unreleased

### Fixed

- **One awkward corner of the factory no longer blanks out every reading** - Critical Path works out what is flowing by solving your factory as a set of independent transport networks. If any single one of them failed to settle on an answer, the whole calculation was thrown away and every line in the factory reported "could not measure what is arriving" instead of the numbers it already had. On a Phase 4 save this meant one steel loop erased all 108 item readings, including lines where a single belt ran from one machine to the next. Each network is now reported on its own: the one that could not be solved says so, and everything else shows what it measured. The advice attached to those rows was misleading too, since it sent you to inspect belts that were never the problem.

- **Pressing REFRESH no longer risks taking the game to desktop** - Refreshing rebuilt the entire panel from inside the button's own click handler, which threw away rows the game was still holding on to for the click it was in the middle of delivering. The next time the screen drew, it could walk into one of those discarded rows and crash. The rebuild now happens a fraction of a second later, once the click has finished being handled, which is how every other part of the panel already worked. Nothing looks different.

- **The partial-scan warning is gone for good this time** - On an ordinary mid-game save the header still said "only 606 of 609 checked - too many to scan" when nothing whatsoever had been skipped. The count of machines examined was only being raised for machines that had a recipe set, while the total it was compared against included every machine you own, so three placed-but-not-yet-configured constructors were enough to make the panel announce that your factory was too big to read. An idle machine is now counted as looked at, because it was. The same counting error meant unconfigured machines did not count toward the scan limit either, so that limit now means what it says.

---

## [1.0.1] - 2026-07-26

> *Critical Path now works on a finished factory. The first release was sized for a mid-game base and quietly stopped measuring long before an endgame save was covered, which made it both wrong and, on a large enough factory, fatal.*

### Added

- **Critical Path now speaks 20 languages** - The whole panel is translated: German, Spanish, French, Italian, Japanese, Korean, Polish, Brazilian Portuguese, Russian, Turkish, Simplified and Traditional Chinese, Bulgarian, Hungarian, Norwegian, Ukrainian, Vietnamese, Arabic, Persian, and Thai. Game terms follow Satisfactory's own wording in each language rather than a dictionary translation, so a belt is called what the game calls it. Counted text uses each language's real plural rules instead of an English-shaped guess. Item and building names are left alone, since the base game already translates those. Spot something that reads wrong in your language? Corrections are very welcome on the GitHub Issues page. (Issue #1, requested by dmeyster on the Smart! Discord for friends who do not read English)

- **Filter BALANCE by what is actually wrong** - A new dropdown filters the list to **ALL LINES**, **NEEDS ATTENTION**, **NO POWER**, **MISSING INPUT**, **JAMMED** or **PAUSED**. On a finished factory thousands of lines feed your objective and almost all of them are fine, so scrolling for the broken one is the wrong job to give a person. Pick a state and you get only the lines in it, and it stacks with the search box, so "concrete" plus NEEDS ATTENTION goes straight to the concrete line that is underperforming. If nothing matches, it says so plainly rather than leaving you wondering whether it looked. (Requested by Dead-Again on the Smart! Discord)

- **The search box applies on Enter** - Searching now waits for you to finish typing instead of rebuilding the list on every keystroke, which on a large factory was both slow and a real risk of running the game out of memory.

### Fixed

- **Opening the panel on a large factory no longer crashes the game** - On an endgame base, pressing `F10` could take the game to desktop with "Maximum number of UObjects exceeded". Every line in the BALANCE tab was building a row for each of its producers and consumers up front, then hiding them behind the expand arrow, so a factory with thousands of machines on one item paid for thousands of rows nobody had asked to see. Those lists are now built to a fixed budget, and when a list is shortened the row says how many it is not showing rather than pretending that is all of them. (Reported by Dead-Again on the Satisfactory Modding Discord)

- **Your whole factory is measured now, not the first slice of it** - Critical Path stopped after 2,000 machines. On a normal endgame save that is roughly a quarter of the base, and everything past the cut-off simply did not exist as far as the analysis was concerned: lines you had built for hours were reported as "nothing produces it", and the plan told you to build them again. The limits are now set well above the size of a finished factory, and the parts of the analysis that map out how your factory is connected were raised to match. On the save this was found with, the number of dependencies it could not account for dropped from 90 to 17. (Reported by Dead-Again on the Satisfactory Modding Discord)

- **The header no longer claims a partial scan when nothing was skipped** - The panel could report "only 8,808 of 8,808 checked - too many to scan", which is both self-contradictory and alarming. Several separate limits shared one warning, so hitting any of them told you your factory was too big to read even when every machine had been counted. The header now only says the scan was cut short when it actually was, and says something accurate about the other limits otherwise.

- **A stopped machine is no longer reported as "nothing is using this"** - When the only machine drawing from a line was stalled, backed up, unpowered or starved, it drew nothing, so Critical Path concluded nothing was connected and told you the line was just filling storage. Both halves of that were wrong: something was using it, and nothing was being stored. The row now says the consumers are stopped, how much they would draw if they ran, and when the cause is their own output backing up it says that too.

- **Lines fed by a teleporter are no longer reported as missing** - If something carries an item to a line without a belt or pipe, a teleporter, portal, or a modded loader, Critical Path could not follow that route and concluded nothing produced the item. It then told you to build more machines you had already built and which were running perfectly well at the other end of the link. It now recognises that the item is produced elsewhere in your factory and says so, instead of inventing a shortage. (Found on Dead-Again's save, where a Fluid Teleporter feeds a line 650 m from its source)

- **Recipe coverage no longer runs out on modded saves** - The recipe map behind the plan was capped low enough that a modded recipe set could exhaust it, which reads downstream as a missing production path. On heavily modded saves this showed up as advice to build a machine you would never build, including modded catch-all machines standing in for basic parts.

---

## [1.0.0] - 2026-07-25

> *First release. Critical Path reads the factory you are standing in and answers one question: what is actually stopping your current objective?*

### Added

- **The Critical Path panel, on `F10`.** Opens over the game, shows your current Space Elevator phase and HUB milestone side by side, and closes with `F10` or `Esc`. The key is rebindable under **Options → Keybindings → Critical Path**. Press **REFRESH** to re-measure; the header always tells you how old the reading is.

- **PATH - every objective part, with the reason it is not finished.** Expand a part to walk its dependency chain down to the deepest cause. Blockers are attributed to a specific machine rather than to the product, so you are sent to the thing that is actually wrong instead of the thing that looks wrong. A machine that is unpowered *and* unfed *and* clogged reports all three, rather than only the first one the game happens to mention.

- **BALANCE - what each line needs, what you built, and what is actually flowing.** Four rates per item: consumer need, built capacity, estimated current output and estimated current use. Built capacity does not shrink when a machine stalls, so the gap between capacity and output is the problem, stated plainly. Rows also show the supply route, what the line would manage at full speed, and how long your stored buffer covers the shortfall.

- **PLAN - an ordered build sequence.** Steps are grouped by dependency depth, so everything inside a step is independent and step 1 is always buildable right now against what you already produce. Each item says what it is for individually, and terminal parts say which objective they deliver.

- **Research appears as a step in the plan, not a footnote.** When completing the plan's own lines is what finishes the research that unlocks a later part, that is one chain and it reads as one - including the case where a single milestone unlocks both a part and an ingredient that part needs, so the second line is not a surprise afterwards.

- **Belt jams are identified as jams.** A machine fed by a full belt still starves if the item at the head of that belt is something it cannot use, because nothing behind it can pass either. Critical Path names the offending item instead of reporting a generic shortage - the usual culprit being stray ore left on an ingot line.

- **Locate markers.** Rows that identify a building can drop a marker on your map and compass, cleared again with **CLEAR PINGS**.

- **Fluid routes are checked for physical plausibility.** Pump head, reservoir columns, closed valves, reverse traversal and the highest point of each pipe run all feed a pass/fail check made before any rate is calculated. A pipe that cannot climb is reported as exactly that, rather than as a vague connectivity problem.

- **Nuclear waste is treated as production.** A reactor is modelled as a producer of its own waste, so a chain that consumes reactor waste can see where it comes from and at what rate.

- **Multiplayer, with the host measuring and sending the report.** Works on dedicated servers. A joined client gets the complete report, labelled as the host's reading. It is done this way because machine contents and storage are server-side: a client measuring for itself would read every machine as empty and report a healthy factory as starved. The host shares one measurement with everyone asking at around the same time, and the honesty rules travel with it - if the host's scan hit its limits, your copy says so too.

- **Unknowns stay unknown.** Vehicle routes report as connected with unmeasured throughput rather than being assigned a plausible number. A scan that hits its collection limit says how much it examined. A solve that does not settle keeps its values but marks them unknown. The rule throughout is that a wrong number gets acted on, while a blank gets investigated.

- **Read-only and save-safe.** Critical Path never builds, dismantles, configures or moves anything, stores nothing in your save, and can be removed at any time. The analysis is deterministic - the same save produces the same answer, with no AI model involved at runtime.

- **Modded content is measured, not assumed.** Recipes, items and unlocks are read from the running game rather than from a built-in table, so modded recipes and modded objectives are handled on the same path as vanilla.
