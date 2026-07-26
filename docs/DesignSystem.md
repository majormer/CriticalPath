# Critical Path - Design System

This is the human-readable contract implemented by `CPStyle.h` and the panel widgets. Feature
widgets do not hard-code colours, sizes, or spacing; new visual evidence extends these tokens rather
than creating a parallel language.

## Palette

| Token | Hex | RGBA (linear-ish, as authored) | Use |
|---|---|---|---|
| `Backdrop` | `#0B0E13` | 0.043, 0.055, 0.075, 0.97 | Panel background |
| `Surface` | `#141922` | 0.078, 0.098, 0.133, 1 | Header / verdict bands |
| `Row` | `#1B2230` | 0.106, 0.133, 0.188, 1 | Path rows |
| `Divider` | `#2A3341` | 0.165, 0.200, 0.255, 1 | Separators, row outlines |
| `Accent` | `#3FB6C9` | 0.247, 0.714, 0.788, 1 | Brand accent, selection, focus |
| `TextPrimary` | `#E8EDF2` | 0.910, 0.929, 0.949, 1 | Item names, verdict |
| `TextSecondary` | `#9AA7B4` | 0.604, 0.655, 0.706, 1 | Rates, machine counts, labels |
| `TextOnAccent` | `#0B0E13` | 0.043, 0.055, 0.075, 1 | Text on accent fill |

**Why a cool accent rather than the usual orange:** status uses amber for under-supplied. A warm
brand accent would collide with a status colour, which is exactly the kind of ambiguity this panel
exists to remove.

## Status tokens

Status is **never conveyed by colour alone** - every row names its status in text. Colour is the
fast read; the words are the accessible one (survives screenshots, compression, colour-blindness).

Semantic colour model (settled with the maintainer, 2026-07-22):

| Colour | Hex | Meaning |
|---|---|---|
| GREEN | `#4CAF6D` | Delivered / completed. |
| BLUE | `#4FA3E3` | Banked - covered by owned stock; manual delivery is the action. |
| AMBER | `#E0A33E` | Degraded but flowing (under-supplied). |
| RED | `#E06C60` | Hard stop - no producer, no power, jammed upstream. |
| VIOLET | `#9B8BD6` | Research needed (row names the unlock + source: milestone/MAM/Shop/hard drive). |
| NEUTRAL | `#7A8797` | Working as intended (time-limited, saturated, standby, extraction) - visible, not shouting. |

Rendered as a 4px left-edge stripe on every row, plus a reserved fixed-size (20px) **activity
slot**: motion in that slot means the line is actively producing.

### RED is reserved for a fault, not for unfinished work

The status enum alone cannot decide the colour. `ECPNodeStatus::Blocked` covers two situations a
player treats completely differently, and `CPStyle::StatusColor` cannot tell them apart because it
only sees the status:

| Situation | Colour | Row says | Icon |
|---|---|---|---|
| A line EXISTS and has stalled - starved, jammed, unpowered, unrouted | RED | "Needs a fix" | warning triangle |
| A line was never built | BLUE | "Not built yet" | none |
| The route could not be measured | BLUE | "Route not measured" | none |
| Recipe not unlocked | VIOLET | names the unlock + source | question mark |

`PartRowColor` in the panel layers this on top of `StatusColor`; the blocker reason stays out of
the design system, which has no business knowing what `ByproductBackedUp` means.

The reason is the whole point of a status palette: **an alarm that is always on is not an alarm.**
Painting every unbuilt objective part red meant a fresh save and a burning factory looked
identical, from the first hour of a playthrough to the last. Unfinished is the normal condition
of an unfinished game. "Not built yet" also gets no icon deliberately - the stripe and the text
already carry it, and an alert triangle on every unstarted part is the same false alarm in
pictorial form.

## Verdict bands

Three bands, not two, for the same reason:

| Token | Hex | When |
|---|---|---|
| `VerdictBlockedBand` | `#2B1313` | A factory fault the player can go fix. Text in RED. |
| `VerdictNextBand` | `#111B28` | Work remains, nothing is wrong - not built, or not researched. Text in BLUE. |
| `VerdictReadyBand` | `#0F2117` | Everything outstanding is banked. Text in GREEN. |

## Meter series (Balance four-rate bars)

These four bars are four MEASURES OF ONE QUANTITY, so their colours carry **identity, not
judgement**. Status hues are deliberately excluded: a short green "current output" bar reads as
healthy when it is precisely the problem, and spending the alarm vocabulary on routine data
devalues it where it does mean something.

Hues come from Satisfactory's own art direction so the panel reads as part of the game rather
than a web dashboard - FICSIT orange, the FICSIT mug blue, crystal violet, and a warm equipment
tan.

| Token | Hex | Bar |
|---|---|---|
| `MeterReference` | `#CCB69D` | Consumer need - a target, kept recessive |
| `MeterCapacity` | `#3EA2D3` | Built capacity |
| `MeterOutput` | `#FD9426` | Est. current output |
| `MeterUse` | `#B250D0` | Est. current use |
| `MeterTrack` | `#0E1219` | Unfilled track (UProgressBar's default background is near-white) |

**Order is load-bearing.** Bars render need → capacity → output → use, so those are the adjacent
pairs that must separate. Validated with the palette checker against the dark track: worst
adjacent pair protan ΔE 13.7 / tritan 15.8 / normal-vision 19.4. The previous set failed at
5.3 / 2.5 / 6.6 - built capacity and consumer need were indistinguishable even with full colour
vision. A cool grey reference beside a blue capacity bar fails every time regardless of shade,
which is why the reference is warm; blue beside violet collapses to ΔE 0.5 under deuteranopia,
which is why orange sits between them.

Bar values wear **text tokens, not the series colour**: the bar already carries identity, and the
recessive reference colour is unreadable as 9pt type.

History: a font-glyph status set (`■●▲×◆`) shipped briefly and was rejected - shapes need a
legend, and mixed glyph widths broke column alignment. Font-glyph note kept for reference: the
runtime font covers Geometric Shapes/Latin-1 but NOT Dingbats (`✔✖` = tofu). The `▼`
chain-collapse affordance is the only font glyph still in use.

### Status icons from game textures

The game's monochrome UI set (`/Game/FactoryGame/Interface/UI/Assets/MonochromeIcons/…`) is
white/tintable - perfect status icons for the activity slot, static or rotating. Maintainer-picked
candidates from the in-panel evaluation strip:

| Icon | Use |
|---|---|
| `TXUI_MIcon_Cogwheel` (rotating) | actively producing - the chosen activity indicator direction |
| `TXUI_MIcon_Warning` (triangle-!) | alert / blocked / degraded |
| `TXUI_MIcon_SortRule_Overflow` (stack + out-arrow) | banked - manual delivery needed |
| `TXUI_MIcon_Speedometer` (tachometer) | producing slowly / time-limited |
| `TXUI_MIcon_QuestionMark` | research needed |
| `UI/Loading/SpinnerForeground` (+`SpinnerBackground`) | the load-screen spinner pair; alternative producing indicator |

The current panel uses these soft-loaded assets. The icon replaces nothing - stripe colour + status
text stay; the icon is the third redundant channel and the motion carrier. The fixed-size slot keeps
alignment independent of the icon and a missing asset falls back without breaking the row.

**Icon reference library (local only).** A full dump of the game's UI textures
(`/Game/FactoryGame/Interface`, 542 as of 2026-07-22) with per-folder contact sheets and the
tinted status sheet lives under `docs/icons/` - **gitignored**: these are first-party Coffee
Stain assets, referenced by path at runtime (fine) but not redistributed in the repo. To
regenerate: temporarily re-enable `DumpStatusIconsOnce` in `CPPanelSubsystem.cpp`, open the
panel once in the shipping game (writes PNGs to `Saved/CriticalPath/icons/`), then run the
contact-sheet/status-sheet compositor. Icons are referenced in code by their `/Game/...` path,
so nothing here ships in the mod.

## Typography

All text uses the **in-game runtime multi-script font**
(`/Game/FactoryGame/Interface/Font/DescriptionText`), never the stylized offline FactoryFont, so
every language Satisfactory ships renders correctly. One helper owns font creation; no widget
grabs the UMG default.

| Role | Size | Weight | Colour |
|---|---|---|---|
| Panel title | 20 | Bold | TextPrimary |
| Objective / verdict | 14 | Bold | TextPrimary |
| Item name | 13 | Regular | TextPrimary |
| Detail (rates, machines) | 11 | Regular | TextSecondary |
| Caption / hint | 10 | Regular | TextSecondary |

## Spacing & layout

- Spacing scale: **4 / 8 / 12 / 16** px. Nothing else.
- Row height: content-driven, min 28px; indent per depth level: **18px**.
- Panel: centred modal, max width **1080px**, max height **860px**; scrolls internally.
- Primary body: one report-level **Path · Balance · Plan** tab strip. Path retains the side-by-side
  **Space Elevator** and **Milestone** objective columns; Balance uses the ingredient selected on
  Path; Plan owns research/build tiers. Header and verdict context remain stable across tabs.
- No fixed pixel layouts for the panel frame - anchors and fills only, so ultrawide and 4K survive.

## Navigation & density (progressive disclosure, not zoom)

The report is bigger than any screen; the screen shows a *distillation*:

- **Default view is the distilled view**: collapsed green branches, problem branches expanded to
  their blocker. The common case (one goal, one blocker) fits without scrolling.
- **Interactive evidence**: objective rows and their planned chains expose detail without turning
  the default surface into a graph. Expanded content may include recipe math, delivery basis,
  transport contribution, buffer runway, and producing-machine locations.
- **ScrollBox** for deep chains; mouse wheel scrolls, PgUp/PgDn/Home/End work.
- **Phase C tab contract:** Path is the default and preserves both primary objective summaries;
  Balance is objective-scoped evidence for the selected ingredient; Plan owns research and build
  tiers. One tab bar only - no nested tabs. Augments and Logistics do not get placeholders before
  their content exists.
- **Query-scoped opening**: launched from a specific finding, the panel opens pre-focused on that
  subtree, everything else collapsed.
- **No zoom.** This is a list+inspector UI, not a canvas; zoom only becomes relevant if a graph
  view ever ships.

## Contrast (hard requirements)

- Body text ≥ **4.5:1** against its actual rendered background; large text (≥18px bold) and
  status glyphs ≥ **3:1**; disabled states still ≥ 3:1.
- Verified against **rendered thumbnails**, not token names - "light gray on white" class
  failures must be caught in the authoring loop, never in a screenshot from a player.
- Every new background/text token pair gets checked at introduction time.

## Vanilla theming strategy

Goal: look native to Satisfactory without depending on its UI internals.

- **Soft-load vanilla chrome assets** (panel/bevel textures, window backdrops) by path into our
  brushes - same resilience pattern as the font: if an update moves an asset, fall back to the
  flat design-system colors and log, never break.
- **Never reparent to the game's widget classes** - fragile across updates.
- Body text stays on the runtime multi-script font regardless of theme. The stylized offline
  FactoryFont is at most a Latin-only decorative title option, and only with a localization
  fallback.
- Known tension: FICSIT orange chrome vs. our amber "under-supplied" status. Resolve by
  thumbnail A/B; glyphs stay the primary status channel either way.

## Lifecycle states

Every surface defines all five states up front: **loading** ("analyzing…"),
**empty** (no objective / nothing required), **error** (engine unavailable - say so plainly),
**truncated** ("scanned N of M" whenever any cap was hit; a capped result never presents as
complete), and **stale** (visible data age + refresh affordance). The current report is bounded by
objective/item caps and renders programmatic vertical rows in an internal ScrollBox. Revisit
virtualization only if arbitrary-item or multi-goal reports remove that bound.

## Evidence vocabulary

Phase C distributes evidence across the Path · Balance · Plan workspace; it does not add a second
dashboard:

- Every quantitative claim identifies **Current** or **Design** basis.
- Scheduled transport with no measured throughput says **Scheduled / capacity unknown**; an
  effectively unbounded placeholder is never shown as real capacity.
- Incomplete capture says **Unknown** and names the cap or missing evidence.
- A blocker is the deepest hard stop; a limiter is the tightest flowing link; the Plan is the
  ordered repair/build sequence. These labels are not interchangeable.
- Storage is shown as banked stock/runway, not as a perpetual source in steady-state flow.
- Rows with world coordinates expose one consistent **Locate** action. Multiple locations use a
  compact count and a deliberate ping/selection flow rather than compass spam.

Player-facing copy describes the factory, never the implementation. Terms such as **solver**,
**solved**, **converged**, **domain**, **iteration**, and **re-solve** belong in logs and API
diagnostics only. The panel says what is reaching the line, what the line needs, what could not be
measured, and what the player can inspect or change. Unknown evidence must remain honest without
turning an internal failure mode into the player's problem.

## Interaction (mouse + keyboard first)

Mouse and keyboard are the design target. Controller support is opportunistic - welcome where it
falls out of good focus hygiene, but **never at the cost of M+K ease of use**.

- **Hover is a first-class channel**: tooltips, row highlights, and inspector previews may be
  hover-driven. Critical information should also be reachable by click/selection (so it isn't
  hover-exclusive), but hover is the fast path.
- **Click is the primary verb**; keyboard provides power shortcuts (expand/collapse, tab
  switching, Esc to close, arrows to move selection), not a required traversal mode.
- Focused/selected elements show the `Accent` outline - never colour-fill alone.
- Keep sane focus order as a matter of hygiene; if a gamepad works because of it, good - but no
  M+K interaction is redesigned to make that true.
