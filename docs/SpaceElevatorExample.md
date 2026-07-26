# Space Elevator: Main Body — Configured Production Lines

> Historical design snapshot (2026-07-20). This example established the distilled-path UX, but its
> rates and aggregate status colours predate the solved flow graph, fluid modeling, and transport
> edges. It is not current validation evidence. See [AnalysisModel.md](AnalysisModel.md),
> [FlowModel.md](FlowModel.md), and [Validation.md](Validation.md) for the current contracts.

Snapshot of the live save at that time (standard recipes throughout, clocks as configured).
Node colors: **blue** = a machine produces it but supply < demand, **green** = fully supplied
(supply ≥ demand), **red** = no producer configured.

```mermaid
flowchart TD
    ELEVATOR["Space Elevator: Main Body<br/>2,000 VF / 480 Engines / 99 ACUs still missing"]

    VF["Versatile Framework<br/>2x Assembler @ 250% - 25/min<br/>fulfilled (inputs collapsed)"]
    ENGINE["Modular Engine<br/>1x Manufacturer @ 100% - 1/min<br/>running"]
    ACU["Adaptive Control Unit<br/>1x Manufacturer @ 250% - 2.5/min<br/>running"]

    ELEVATOR --> VF
    ELEVATOR --> ENGINE
    ELEVATOR --> ACU

    %% Versatile Framework inputs fulfilled - children collapsed

    %% Modular Engine inputs
    MOTOR["Motor<br/>need 2/min - make 12.5/min"]
    RUBBER["Rubber<br/>need 15/min - make 150/min"]
    PLATING["Smart Plating<br/>1x Assembler @ 250%<br/>need 2/min - make 5/min"]
    ENGINE --> MOTOR
    ENGINE --> RUBBER
    ENGINE --> PLATING

    %% Adaptive Control Unit inputs
    WIRING["Automated Wiring<br/>1x Assembler @ 250%<br/>need 12.5/min - make 2.5/min<br/>idle: missing input"]
    CIRCUIT["Circuit Board<br/>need 12.5/min - make 18.75/min"]
    COMPUTER["Computer<br/>need 5/min - NO PRODUCER"]
    HMF["Heavy Modular Frame<br/>need 2.5/min - NO PRODUCER"]
    ACU --> WIRING
    ACU --> CIRCUIT
    ACU --> COMPUTER
    ACU --> HMF

    classDef goal fill:#8a4a2f,stroke:#e8916a,stroke-width:2px,color:#ffffff
    classDef produced fill:#1f4e79,stroke:#6aaBe0,stroke-width:2px,color:#ffffff
    classDef supplied fill:#1f5c30,stroke:#5cb874,stroke-width:2px,color:#ffffff
    classDef missing fill:#7a2a24,stroke:#e07a70,stroke-width:2px,color:#ffffff

    class ELEVATOR goal
    class ENGINE,ACU,WIRING produced
    class VF,MOTOR,RUBBER,PLATING,CIRCUIT supplied
    class COMPUTER,HMF missing
```

## Reading the board

| Status | Parts | Action |
|---|---|---|
| 🔴 No producer | Computer, Heavy Modular Frame | Build lines. The ACU Manufacturer is draining the 123-Computer stockpile at 5/min (~25 min of runway). |
| 🔵 Under-supplied | Automated Wiring (2.5 vs 12.5/min) | AW needs ~5x capacity (its own Assembler idles on missing Stator/Cable input). |
| 🟢 Covered | Versatile Framework (chain collapsed), Motor, Rubber, Smart Plating, Circuit Board | VF and the whole Modular Engine chain are healthy. |

Data sources: prototype snapshot tooling (recipe edges, configured capacities/clocks, storage
totals) cross-checked against a live configured-production scan. Circuit Board margin assumes no
other consumers come online; a Computer line would add 12.5+/min of new Circuit Board demand.
