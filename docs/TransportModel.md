# Critical Path — Transport Model

This document is the durable contract for train, truck, and drone logistics in the flow graph.
The topology layer is implemented and merged. Transport is modeled as batched service between
ports, not as a belt island, and uncertainty about throughput remains explicit.

## 1. Common contract

Every cargo platform, docking station, and drone port is a TransportPort node. Its ordinary belt
or pipe connections place it in the local factory graph. A directed transport edge bridges a
load-side port to an unload-side port when a standing service proves that route exists.

Transport edges:

- preserve direction;
- participate in current and design solves;
- carry solids or fluids according to their ports;
- do not merge the source and destination into one continuous belt/pipe island;
- use measured/estimated throughput only when the game exposes a trustworthy value;
- otherwise report “connected via transport; rate unknown.”

The current implementation encodes unknown capacity with a large internal sentinel. Phase C must
convert that into explicit metadata before any player-facing rate is derived from it.

## 2. Drones

Enumeration:

- drone subsystem → station info actors;
- station info → paired outbound station and inbound connected stations;
- server-side station buildable → input, output, and fuel inventories.

The graph creates an edge from a station to its explicit paired destination. Drone info actors
also provide the best transport telemetry of the three modes:

- estimated round-trip time and rate per fuel;
- latest/average/median trip statistics;
- average incoming and outgoing item rates.

Topology is implemented. Positive-flow live validation remains desirable on a save where a drone
route is the only path for an item.

## 3. Trains

Enumeration:

- railroad subsystem → trains;
- train → timetable → ordered stops and docking rules;
- station identifier → station buildable → platform chain;
- cargo platform → load/unload mode and belt/pipe connections.

For each standing train service, every discovered load cargo platform bridges to every discovered
unload platform on that timetable. The capture currently treats this as connectivity-only.

The game also exposes:

- per-stop load and unload item filters;
- docking behavior and dwell rules;
- wagon cargo form and inventories;
- platform item/fluid transfer rates.

Those filters and measurements are the next metadata pass. Train ETA remains out of scope because
timetables do not store travel time and the useful ATC data is server-runtime state.

### Live evidence

A forced-routing oil experiment closed the competing direct-pipe path. The solver then carried
approximately 509.6 m³/min of Crude Oil over train edges. In a later simultaneous train/truck
probe it carried approximately 465.7 m³/min by train and 171.5 m³/min by truck.

## 4. Trucks

Satisfactory 1.2 uses identifier actors and GUID routes; the old target-point linked-list model
exists only for legacy conversion.

Enumeration:

- vehicle subsystem → wheeled-vehicle identifiers;
- autopilot-enabled identifier → ordered path-node GUIDs;
- GUID → docking-station identifier → station buildable;
- station load/unload mode plus belt/pipe connections.

Only autopilot routes are standing services. Manually driven vehicles do not create durable
transport edges. Truck stations have no item filters; the load-side graph determines eligible
items.

The game exposes station transfer statistics and per-vehicle docking history. These are candidates
for measured-rate annotation, not permission to synthesize a theoretical rate.

### Live evidence

A fluid truck was made the only path for part of an oil supply. The first apparent result was
rejected because a train platform actor name had been misclassified as a docking station. After
class-correct capture and a physical plumbing correction at the truck station, the solver measured
approximately 171.5 m³/min of Crude Oil over the truck edge while train traffic remained active.

## 5. Health and item eligibility

The completed topology proves that a route exists. Release-quality analysis must add:

| Mode | Item eligibility | Health evidence | Rate evidence |
|---|---|---|---|
| Drone | source output inventory / port graph | paired station, fuel, trip state | replicated trip estimates and averages |
| Train | per-stop filters, otherwise load-platform graph | assigned train, timetable, docking/platform state | platform transfer rates; no invented ETA |
| Truck | load-station graph | autopilot route, fuel, station sequence | station transfer/docking averages |

A failed health signal creates a degraded transport link. It does not erase the route from
topology, and it does not become a generic “producer disconnected” verdict.

## 6. Replication and game-type rules

- Identifier/info actors, timetables, route GUIDs, pairings, modes, and many statistics replicate.
- Station buildable back-pointers and inventories are server-side.
- Full capture runs on the authoritative host/server.
- A client-only consumer degrades to identifier-level topology and marks buffer/rate evidence
  unknown.
- No path assumes a local player pawn; dedicated servers are first-class.

## 7. Defensive traversal

- Guard every peer and station dereference.
- Use GUIDs rather than volatile network IDs.
- Skip zombie/degenerate conveyor chains.
- Hop through foundation/wall pipe passthroughs and other connection-only actors.
- Keep transport mode as typed metadata; never classify by actor-name substrings.
- Retain actor name and world location for offline replay and player-facing locate actions.

## 8. Remaining work

1. Add transport mode, carrier/service identity, item eligibility, and measurement provenance to
   public flow-edge results.
2. Replace the internal unknown-capacity sentinel with explicit unknown semantics at the analysis
   and interface boundary.
3. Use measured/estimated rates when trustworthy and label their basis.
4. Surface transport-fed, degraded, and rate-unknown states in Phase C panel rows.
5. Add positive drone-route validation and retain train/truck forced-routing regressions.
6. Keep path splines and ETA prediction outside the MVP; they may support future map visualization.
