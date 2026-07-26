# <img src="https://github.com/majormer/CriticalPath/blob/main/images/CriticalPath-Logo.png?raw=true" width="150" alt="Critical Path Logo"> Critical Path

![Status](https://img.shields.io/badge/Status-Released-brightgreen) ![Version](https://img.shields.io/badge/Version-1.0.1-blue) ![Satisfactory](https://img.shields.io/badge/Satisfactory-1.2-blue) ![Engine](https://img.shields.io/badge/Engine-UE%205.6-blue) ![SML](https://img.shields.io/badge/SML-3.12-blue) ![Multiplayer](https://img.shields.io/badge/Multiplayer-Supported-brightgreen) ![AI Assisted Development Used](https://img.shields.io/badge/AI%20Assisted%20Development%20Used-Disclosure%20Below-blue)

> **Multiplayer:** Critical Path works in single-player and multiplayer, including dedicated servers. The **host** measures the factory and sends the report to joined clients, so everyone sees the same answer - labelled as the host's reading.

**Quick links:** [What is it?](#-what-is-critical-path) • [The three tabs](#-the-three-tabs) • [Should you install?](#-should-you-install-critical-path) • [Controls](#-controls) • [Wiki](https://github.com/majormer/CriticalPath/wiki) • [Source](https://github.com/majormer/CriticalPath) • [Issues](https://github.com/majormer/CriticalPath/issues) • [Discord](https://discord.gg/SgXY4CwXYw)

---

## 🆕 What's new in 1.0.1

**Critical Path now works on a finished factory.** The first release was sized for a mid-game base: it stopped measuring after 2,000 machines, which on an endgame save is about a quarter of the base. Lines you had built for hours were reported as "nothing produces it", and on a large enough factory opening the panel could crash the game outright. Both are fixed, and the limits are now set well above the size of a completed factory.

- **20 languages.** The whole panel is translated: German, Spanish, French, Italian, Japanese, Korean, Polish, Brazilian Portuguese, Russian, Turkish, Simplified and Traditional Chinese, Bulgarian, Hungarian, Norwegian, Ukrainian, Vietnamese, Arabic, Persian, and Thai. Corrections are welcome on the Issues page.
- **Filter BALANCE by what is wrong.** A dropdown narrows the list to lines that need attention, or specifically those with no power, missing input, jammed output, or paused machines. It stacks with the search box.
- **Honest answers where it used to guess.** A line fed by a teleporter, portal or modded loader is no longer reported as missing production. A machine that is stopped is no longer reported as "nothing is using this". A full scan no longer claims it was partial.

Full detail in the [changelog](https://github.com/majormer/CriticalPath/blob/main/CHANGELOG.md).

---

## 🧭 What is Critical Path?

Production planners are excellent at designing factories. Save-aware web tools can inspect a factory you already built. Critical Path occupies a different niche:

**What is stopping this objective, in the factory that is running right now?**

Press `F10` and it reads the live world - machines, recipes, clocks, inventories, belts, pipes, splitters, storage and vehicle routes - then shows your current Space Elevator phase and HUB milestone side by side, with the reason each outstanding part is not finished.

Because it runs in game, changing a recipe, restoring a connection, adjusting a clock or repairing a line is reflected on the next refresh. No saving, uploading and re-reading somebody else's snapshot.

<div align="center">

<img src="https://github.com/majormer/CriticalPath/blob/main/images/CriticalPath-Path.png?raw=true" width="820" alt="The PATH tab showing both objectives and their outstanding parts">

*Both objectives side by side. Green is delivered, blue is outstanding work, violet is waiting on research.*

</div>

Critical Path is **read-only**. It never builds, dismantles, configures or moves anything, and it stores nothing in your save. It is **deterministic** - the same save produces the same answer - and **no AI model is involved at runtime**.

It is a **Finalomega Labs** mod and it is **standalone**: it does not require, and does not interact with, any other mod.

---

## 📊 The three tabs

### PATH - what is stopping each objective part

Expand any part to walk its dependency chain down to the deepest cause. Blockers are attributed to a **specific machine**, not to the product, so you are sent to the thing that is actually wrong rather than the thing that looks wrong. A machine that is unpowered *and* unfed *and* clogged reports all three, rather than only the first condition the game mentions.

It also catches the failure people miss most often: a machine fed by a **full belt** that still starves, because the item at the head of that belt is something it cannot use and nothing behind it can pass. Critical Path names the offending item instead of reporting a vague shortage.

### PLAN - an ordered build sequence

<div align="center">

<img src="https://github.com/majormer/CriticalPath/blob/main/images/CriticalPath-Plan.png?raw=true" width="820" alt="The PLAN tab showing an ordered build sequence including a research unlock step">

*Step 1 is always buildable right now. Research that gates a later step appears in sequence, not as a footnote.*

</div>

Steps are grouped by dependency depth, so everything inside a step is independent of everything else in it and **step 1 is always buildable today** against what you already produce. Each item states what it is for individually.

Research that gates a later part appears as its own numbered **Unlock** step, in order - including the case where one milestone unlocks both a part *and* an ingredient that part needs, so the second production line is not a surprise after you finish researching.

### BALANCE - what each line needs, and what is actually flowing

<div align="center">

<img src="https://github.com/majormer/CriticalPath/blob/main/images/CriticalPath-Balance.png?raw=true" width="820" alt="The BALANCE tab showing four-rate meters for each item">

*Four measures of the same quantity: what consumers need, what you built, and what is actually happening.*

</div>

Built capacity does not shrink when a machine stalls - it describes what you placed, not what it is doing this second - so the gap between capacity and current output *is* the problem, stated plainly. Rows also show the supply route, what the line would manage at full speed, and roughly how long stored stock covers the shortfall.

---

## 🎮 Controls

| Action | Default | Notes |
|---|---|---|
| Open / close the panel | `F10` | Rebindable under **Options → Keybindings → Critical Path** |
| Close the panel | `Esc` | |
| Re-measure | **REFRESH** button | The header always shows how old the current reading is |
| Drop a map marker | **LOCATE** on a row | **CLEAR PINGS** removes them |

---

## 🧭 Should You Install Critical Path?

### Critical Path is probably for you if you want to:

- Find out why an objective part is stuck, without walking the whole factory.
- See the deepest cause of a stoppage rather than the first symptom.
- Know which lines are keeping up and which are quietly running on stored stock.
- Get an ordered list of what to build next, with research placed where it belongs.
- Diagnose a factory you already built, rather than plan one you have not.

### What Critical Path does **not** do:

- **It does not build, change or dismantle anything.** It is a diagnostic, not a building tool. It has no creative mode, no scaling, no blueprints.
- **It does not give resources, reveal the map, or unlock anything.** It reports on your factory and your own objectives.
- **It does not guess.** Where it cannot measure something it says so, rather than showing a plausible number. See below.
- **It does not replace a production planner.** Planners tell you what to build in the abstract; this tells you what is wrong with what you actually built.

---

## 🔍 Unknowns stay unknown

This is the rule the whole mod is built around, and it is worth knowing before you install.

A factory tool that guesses is worse than useless, because a confident number gets acted on while a blank gets investigated. So:

- **Vehicle routes** report as connected with **unmeasured throughput** rather than being assigned an invented rate. Anything whose supply crosses one is marked not-known.
- **A scan that hits its limit** tells you how much it examined, instead of presenting a partial reading as complete.
- **A calculation that does not settle** keeps its values but marks them unknown.

Some things people expect to be fuzzy are not: pipe feasibility is a hard pass/fail on head lift, and storage is transparent at steady state rather than being mistaken for an endless consumer.

---

## 🌐 Multiplayer

<div align="center">

<img src="https://github.com/majormer/CriticalPath/blob/main/images/CriticalPath-MultiplayerRelay.png?raw=true" width="820" alt="A joined client showing a report relayed from the host">

*A joined client, showing the host's report. Note the header: "from the host, just now".*

</div>

Critical Path works in multiplayer and on **dedicated servers**. The host measures the factory and sends the report to joined clients, so a client gets the complete report - PATH, BALANCE and PLAN - rather than a reduced summary.

Why the host does the measuring: machine inventories, storage levels, station contents and fluid buffers are all server-side. A client can see buildings but reads empty values for their contents, so a client-side analysis would solve perfectly cleanly and be completely wrong - every machine would look starved. Rather than guess, the client asks the host and displays what comes back, labelled as the host's reading.

The host shares one measurement with everyone asking at around the same time, so several players refreshing at once costs it no more than one would. The honesty rules travel with the report: if the host's scan hit its limits, your copy says so too.

Install it on the server as well as on clients, and keep the versions matched - a host on a build too old to send reports will say exactly that.

## ⚡ Performance

Opening or refreshing the panel reads the world on the game thread and then does the heavy work in the background. Measured on an endgame save with **8,808 machines and 65,868 buildings**, a full refresh takes about **three quarters of a second**, of which roughly **a third of a second** is on the game thread, with nothing truncated. Nothing re-runs on a timer - a report is produced only when you ask for one.

---

## 🧩 Compatibility and Save Safety

- **Save-safe.** Critical Path stores nothing in your save and can be removed at any time; the save is unchanged.
- **Modded content is measured, not assumed.** Recipes, items and unlocks are read from the running game rather than from a built-in table, so modded recipes and modded objectives are handled on the same path as vanilla.
- **Standalone.** No dependencies beyond SML.

---

## 🧪 Finalomega Labs

Critical Path is a **Finalomega Labs** mod, targeting **Satisfactory 1.2** on **Unreal Engine 5.6** with **SML 3.12**. Its analysis engine is native C++.

Finalomega also develops the **Smart!** build-assist mod and **Air Build**. Critical Path is a separate, standalone project - it shares no code with them and does not require them.

---

## 🤖 AI Disclosure

Critical Path uses AI-assisted development. AI tools help with investigation, drafting, refactoring, documentation and debugging support. Design decisions, testing, release preparation and maintenance remain the responsibility of the project maintainer.

**The mod itself contains no AI at runtime.** The analysis is deterministic and reproducible: the same save produces the same answer, every time, with no model involved.

AI assistance does not replace community testing. Critical Path is validated through developer review, in-game testing on large factories, and bug reports.

---

## 💬 Getting Help

Critical Path is **v1.0.1**. If something behaves unexpectedly, please report it with your Satisfactory version, SML version, session type (single-player, hosting, or joined), and clear reproduction steps. A screenshot of the panel is enormously helpful.

- **Wiki:** https://github.com/majormer/CriticalPath/wiki
- **Report bugs / track issues:** https://github.com/majormer/CriticalPath/issues
- **Source:** https://github.com/majormer/CriticalPath
- **Discord (community & support):** https://discord.gg/SgXY4CwXYw

---

## ☕ Support

If Critical Path is useful to you, you can support development on **[Ko-fi](https://ko-fi.com/finalomega)**. Thank you!
