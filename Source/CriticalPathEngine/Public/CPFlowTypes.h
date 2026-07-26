// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPEngineTypes.h"
#include "CPFlowTypes.generated.h"

// ============================================================================================
// Flow-graph types (FlowModel.md §2) — the typed factory graph the solver runs on.
// Plain data: capture (game thread) builds it, the solver (any thread) consumes it.
// All rates in display units/min (fluids m³/min).
// ============================================================================================

UENUM(BlueprintType)
enum class ECPFlowNodeKind : uint8
{
	Producer,      // one output port of a machine/extractor: item set + max rate at current clocks
	Consumer,      // a machine's input side: demand rates at current clocks
	Splitter,      // vanilla or smart/programmable — OutRules describe each out edge
	Merger,
	Storage,       // transparent at steady state; inventory is handled by ledgers/runway (FlowModel §5.3)
	Sink,          // AWESOME sink: absorbs unboundedly
	Passthrough,   // lifts, junctions, open valves
	TransportPort  // train platform / truck station / drone port (Phase T)
};

/** Routing rule for ONE splitter output (parallel to that splitter's out edges). */
UENUM(BlueprintType)
enum class ECPSplitterOutRule : uint8
{
	Any,           // vanilla behavior: equal share of everything
	AnyUndefined,  // items not matched by a specific filter elsewhere on this splitter
	Overflow,      // only what the other outputs refuse
	None,          // closed
	Filtered       // only the items listed
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPSplitterRule
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPSplitterOutRule Rule = ECPSplitterOutRule::Any;

	/** For Filtered: the item names this output accepts. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FString> Items;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPBufferedItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	/** Items for solids, m3 for liquids/gases. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float Amount = 0.0f;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPFlowNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPFlowNodeKind Kind = ECPFlowNodeKind::Passthrough;

	/** Debug/report label (machine display name, item, etc.). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString Label;

	/** World actor name + location (cm) — lets offline analysis ping/locate the building. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FVector Location = FVector::ZeroVector;

	/** Descriptor for the represented buildable. Captured so presentation can use the actual
	 *  machine/building icon without retaining a UObject or actor reference. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef BuildingItem;

	/** Manufacturer transform pairing: Consumer input node <-> Producer output node for the same
	 *  actor. INDEX_NONE on raw producers and routing nodes. The solver constrains sustainable
	 *  output by the least-supplied required input. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 PairedNodeIndex = INDEX_NONE;

	/** CURRENT-state rates: producer outputs / consumer demands with dead (unpowered/paused)
	 *  machines contributing zero (FlowModel §3 machine-state coupling). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRate> Rates;

	/** DESIGN-basis rates: as configured, regardless of machine state. Empty on routing nodes.
	 *  The dual solve (FlowModel §9) answers "does the design close?" against these. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRate> DesignRates;

	/** Reachable on-network inventory at capture time. Amount is items for solids and m3 for
	 *  fluids; it is resilience evidence, not a steady-state production rate. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPBufferedItem> Stocks;

	/** Snapshot machine-state evidence for producer nodes. These are observations from the
	 *  factory actor, independent of the steady-state flow model. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bMachineStateKnown = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bProducing = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bNoPower = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bPaused = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bMissingInput = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bOutputBlocked = false;

	/** Rolling machine productivity reported by the game, normalized to 0..100. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ProductivityPercent = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 PowerShardCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 SomersloopCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef PowerShardItem;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef SomersloopItem;

	/** Splitter only: rule per out edge, indexed by FCPFlowEdge::FromPortIndex. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPSplitterRule> OutRules;

	/** Merger whose input ordering is configured explicitly rather than vanilla round-robin. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bPriorityMerger = false;

	/** The ingredient this machine is actually short of. "Missing input" alone sends the player
	 *  hunting; naming the item is the difference between a symptom and an instruction. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString StarvedOfItemName;

	/** Non-empty when an item this machine's recipe cannot use is sitting on a feeding belt.
	 *  Belts are FIFO, so it never enters and nothing behind it moves: a permanent jam that
	 *  looks identical to a shortage from every other angle. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString JammedByItemName;

	/** Storage only: full storage stops absorbing and becomes pass-through. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bStorageFull = false;

	/** Pure routing node (pipe junction) that may be merged with connected peers: junction
	 *  manifolds create bidirectional cycle webs the fixed point crawls through — collapsing
	 *  them (CollapseContractibleClusters) removes the cycles without changing semantics. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bContractible = false;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPFlowEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 FromNode = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ToNode = INDEX_NONE;

	/** Belt mk / pipe mk throughput. Shared across all items on the edge. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float CapacityPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bFluid = false;

	/** Head-lift capped or closed valve (FlowModel §11): geometrically present, cannot flow. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bBlocked = false;

	/** Index into the source splitter's OutRules (INDEX_NONE for non-splitter sources). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 FromPortIndex = INDEX_NONE;

	/** Priority configured on this edge's destination merger input. Larger values are served
	 *  first; INDEX_NONE means ordinary merger/routing behavior. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 MergerInputPriority = INDEX_NONE;

	/** Vehicle route (drone pairing / train timetable / truck route), not a belt or pipe.
	 *  Batched delivery, not continuous throughput: capacity is a route-rate estimate when
	 *  the game measures one, otherwise unbounded ("connected, rate unknown"). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bTransport = false;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPFlowGraph
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPFlowNode> Nodes;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPFlowEdge> Edges;

	/** Capture hit a cap — flows for this graph must report unknown, never guessed. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bTruncated = false;

	/** False on a remote client, where inventories, station back-pointers and fluid boxes are not
	 *  replicated. Such a graph solves cleanly while missing half the world, so convergence must
	 *  NOT be mistaken for knowledge — see FCPFactorySnapshot::bAuthoritative. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bAuthoritative = false;
};

// ============================================================================================
// Solver results
// ============================================================================================

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPEdgeItemFlow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 EdgeIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ItemName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float RatePerMinute = 0.0f;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPConsumerDelivery
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 NodeIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ItemName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float DeliveredPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float DemandPerMinute = 0.0f;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPFlowSolveResult
{
	GENERATED_BODY()

	/** False = iteration cap hit; flows are UNKNOWN and must not be presented as truth. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bConverged = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 Iterations = 0;

	/** Diagnostics: max per-edge flow delta after each iteration (oscillation vs slow decay). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<float> DeltaHistory;

	/** Diagnostics: the edge (and item) carrying the largest delta on the FINAL iteration —
	 *  names the slow mode when the fixed point stalls. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 LastMaxDeltaEdge = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString LastMaxDeltaItem;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPEdgeItemFlow> EdgeFlows;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPConsumerDelivery> Deliveries;

	/** Producer actual output vs max (index = node index, only Producer nodes populated). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPEdgeItemFlow> ProducerActuals; // EdgeIndex field carries the NODE index here

	/** Producer capacity supported by delivered inputs before downstream backpressure. Raw
	 *  producers equal their configured/current rate. Index convention matches ProducerActuals. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPEdgeItemFlow> ProducerSustainable;

	float DeliveredTo(int32 NodeIndex, const FString& Item) const
	{
		for (const FCPConsumerDelivery& D : Deliveries)
		{
			if (D.NodeIndex == NodeIndex && D.ItemName.Equals(Item, ESearchCase::IgnoreCase))
			{
				return D.DeliveredPerMinute;
			}
		}
		return 0.0f;
	}
};
