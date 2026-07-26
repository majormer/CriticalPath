// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPEngineTypes.generated.h"

/** Engine-side log category. Deliberately separate from the presentation module's
 *  LogCriticalPath: this module must never depend on the UI module (see the rule in
 *  CriticalPathEngine.Build.cs). */
CRITICALPATHENGINE_API DECLARE_LOG_CATEGORY_EXTERN(LogCriticalPathEngine, Log, All);

// ============================================================================================
// CriticalPathEngine public types.
//
// Design rules (see PRD §7.1 and docs/AnalysisModel.md):
//  - Structured results only: numbers, enums, item names — never prose sentences.
//  - Plain data: no live UObject references. Item identity = display name + descriptor class
//    path string (consumers resolve icons themselves). This keeps analysis UObject-free and
//    unit-testable with synthetic snapshots, and results JSON-serializable via
//    FJsonObjectConverter with zero custom code.
//  - Fluid/gas amounts are ALWAYS display units (m^3) — conversion happens at extraction.
// ============================================================================================

/** Transport form captured while descriptor UObjects are available. */
UENUM(BlueprintType)
enum class ECPItemForm : uint8
{
	Unknown,
	Solid,
	Fluid
};

/** Identity of an item, safe to hold off the game thread and to serialize. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPItemRef
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString Name;

	/** Descriptor class path (e.g. for icon lookup by consumers). May be empty for unknowns. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString DescriptorClassPath;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPItemForm Form = ECPItemForm::Unknown;

	bool IsValid() const { return !Name.IsEmpty(); }
	bool operator==(const FCPItemRef& Other) const { return Name.Equals(Other.Name, ESearchCase::IgnoreCase); }
};

/** Why a machine is not currently producing. */
UENUM(BlueprintType)
enum class ECPIdleReason : uint8
{
	None,
	NoPower,
	Paused,
	NotConfigured,
	MissingInput,
	OutputBlocked,
	Standby,
	CannotProduce,
	ProductionError,
	Unknown
};

/** Aggregated producers of one item (manufacturers or extractors). */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPProductRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	/** True when this row aggregates resource extractors (miners/oil/wells/water): rate is fixed
	 *  by node purity and machine tier, never input-limited; standby usually means backed-up
	 *  output, not a deliberate switch-off. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bIsExtraction = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString MachineName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConfiguredBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ProducingBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 NoPowerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 MissingInputBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 OutputBlockedBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 PausedBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 StandbyBuildings = 0;

	/** Machines starved because an item their recipe cannot use is stuck at the delivery end of a
	 *  feeding belt. Belts are FIFO, so nothing behind it moves either: connected, full, and
	 *  permanently jammed. This is NOT a supply shortage — adding upstream capacity cannot fix it. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 InputJammedBuildings = 0;

	/** The foreign item observed doing the jamming (first seen). Empty when none. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString JammedByItemName;

	/** Output capacity at current clocks (includes Somersloop boost — outputs only). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ConfiguredCapacityPerMinute = 0.0f;

	/** Subset of configured capacity whose output can reach at least one configured consumer of
	 *  this item through belts or pipes. A lower bound when bConnectivityComplete is false. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ConnectedCapacityPerMinute = 0.0f;

	/** Producers contributing to ConnectedCapacityPerMinute. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConnectedBuildings = 0;

	/** False when connectivity was not captured or a graph cap was hit. In that case zero
	 *  connected capacity is unknown, never proof that the producers are stranded. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bConnectivityComplete = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float AverageProductivityPercent = 0.0f;
};

/** Aggregated configured demand for one item across all consumers. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPDemandRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConsumingBuildings = 0;

	/** Input draw at current clocks (Somersloop boost deliberately EXCLUDED — inputs draw at
	 *  clock rate). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ConfiguredDemandPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 NoPowerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 MissingInputBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 OutputBlockedBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 PausedBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 StandbyBuildings = 0;
};

/** Configured flow for one item inside one belt/pipe transport island. NetworkId is snapshot-
 *  local and opaque: consumers may group rows by it, but must not persist it across captures. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPFlowNetwork
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 NetworkId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ConnectedSupplyPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ConfiguredDemandPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ProducerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConsumerBuildings = 0;

	/** Current-state context only. Demand remains configured design demand while idle. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 InactiveConsumerBuildings = 0;

	/** False when any endpoint/transport/state cap could have hidden a route. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bConnectivityComplete = false;
};

/** One item flow of a recipe edge, in display units per minute (whole line, all machines). */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPItemRate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float RatePerMinute = 0.0f;

	/** Per single craft, as authored in the recipe (stoichiometry surface). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float AmountPerCraft = 0.0f;
};

/** A recipe actually configured in the factory: ALL products (byproducts included) and all
 *  ingredients, with the machine running it. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPRecipeEdge
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString RecipeName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString MachineName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 MachineCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRate> Ingredients;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRate> Products;
};

/** One item an objective still requires. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPObjectiveItem
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 Required = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 Delivered = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 Remaining = 0;
};

UENUM(BlueprintType)
enum class ECPObjectiveKind : uint8
{
	Milestone,
	SpaceElevatorPhase
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPObjective
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPObjectiveKind Kind = ECPObjectiveKind::Milestone;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPObjectiveItem> Items;
};

/** Owned stock for one item, split by availability semantics (see AnalysisModel: depot is
 *  server-side and shared but PLAYER-retrievable only — never machine-reachable supply). */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPOwnedRow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int64 InStorage = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int64 InDepot = 0;

	/** Only meaningful when the snapshot has a player context. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int64 InPockets = 0;

	int64 Total() const { return InStorage + InDepot + InPockets; }
};

/** One producible item's assumed recipe from the UNLOCKED catalog (default recipe policy):
 *  the capture the prospective-chain walk expands through when no configured line exists.
 *  Rates are per single machine at 100% clock. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPCatalogRecipe
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Product;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString RecipeName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString MachineName;

	/** Product output per minute for ONE machine at 100% clock. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ProductPerMinutePerMachine = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRate> Ingredients;

	/** Other unlocked recipes exist for this product — the default was assumed, a choice exists. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bAlternatesExist = false;

	/** This recipe is NOT unlocked yet: it was captured so the plan can see what the item will
	 *  need once research makes it available. Everything downstream must keep saying "locked" —
	 *  a captured recipe is knowledge of a future line, never evidence of a buildable one. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bRecipeLocked = false;
};

/** Where a piece of research is purchased/earned — so the UI can say which surface to visit. */
UENUM(BlueprintType)
enum class ECPResearchSource : uint8
{
	Unknown,
	Milestone,  // HUB milestone
	MAM,        // MAM research tree
	Shop,       // AWESOME Shop (resource sink)
	HardDrive,  // alternate recipe from a hard drive
	Tutorial
};

/** An item on an objective path that no unlocked recipe can produce, with the purchasable
 *  research that would fix it (when one exists within the phase tier gate). */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPResearchGap
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef NeededItem;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ForObjective;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ViaItem;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString UnlockSchematicName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 UnlockSchematicTier = -1;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPResearchSource UnlockSource = ECPResearchSource::Unknown;

	/** Set when the item has NO recipe by design: it is spent nuclear fuel, obtained by
	 *  burning this fuel item in a Nuclear Power Plant. Research will never unlock it. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ByproductOfFuel;

	/** M2.7b: the unlocking schematic's full item costs (Remaining == Required until it is the
	 *  selected milestone — payment only happens through selection). One schematic level only:
	 *  the schematic may have its own prerequisites this capture does not follow. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPObjectiveItem> UnlockCosts;

	/** The unlocking schematic is the currently selected HUB milestone (its live ledger is
	 *  already the milestone objective — consumers should avoid double-presenting it). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bUnlockIsSelectedMilestone = false;
};

/** Truncation honesty: every cap that was actually hit is reported (a capped result must never
 *  present as complete — PRD lifecycle states). */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPTruncation
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 BuildingsScanned = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 BuildingsAvailable = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 StorageContainersScanned = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConnectivityBuildingsScanned = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConnectivityBuildingsAvailable = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConnectivityStatesPropagated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bConnectivityCapHit = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bAnyCapHit = false;
};

/** The extracted factory state: everything analysis needs, nothing live. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPFactorySnapshot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float WorldTimeSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString NetMode;

	/** False on a remote client. Production evidence — inventory contents, station back-pointers,
	 *  machine potentials, fluid boxes — is server-side, so a client capture reads empty or stale
	 *  values that are indistinguishable from a broken factory. Consumers MUST refuse to present a
	 *  diagnosis when this is false; unknown is the only honest answer (TransportModel.md S6). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bAuthoritative = false;

	/** False when the game's observed-production statistics are absent (post-load warmup or the
	 *  known whole-world outage). Analysis uses configured rates and is immune, but consumers
	 *  must not treat missing observed data as zero. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bObservedStatsAvailable = false;

	/** True when a local player context contributed pockets / anchored data. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bHasPlayerContext = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPProductRow> Products;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPDemandRow> Demands;

	/** Item-specific supply/demand partitioned by physical belt/pipe transport island. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPFlowNetwork> FlowNetworks;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPRecipeEdge> Edges;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPObjective> Objectives;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPOwnedRow> Owned;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPResearchGap> ResearchGaps;

	/** Unlocked-catalog slice reachable from objective seeds via default recipes — the
	 *  prospective-chain walk's raw material. Only covers items with NO configured line. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPCatalogRecipe> Catalog;

	/** Names of raw resources (ore/fluid nodes) encountered by the catalog walk — chain
	 *  leaves that need extraction, not manufacturing. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FString> RawResourceNames;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 HighestUnlockedTier = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 PhaseLockedMilestoneCount = 0;

	/** A milestone is currently selected at the HUB (it may already be fully paid). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bMilestoneSelected = false;

	/** Non-purchased milestones within the current phase's tier gate (selectable right now). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 SelectableMilestoneCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPTruncation Truncation;

	// ---- lookup helpers (analysis-side convenience; linear is fine at these sizes) ----
	const FCPProductRow* FindProduct(const FString& ItemName) const;
	const FCPDemandRow* FindDemand(const FString& ItemName) const;
	const FCPRecipeEdge* FindEdgeProducing(const FString& ItemName) const;
	const FCPOwnedRow* FindOwned(const FString& ItemName) const;
	const FCPCatalogRecipe* FindCatalogRecipe(const FString& ItemName) const;
	bool IsRawResource(const FString& ItemName) const;
};

// ============================================================================================
// Analysis results
// ============================================================================================

/** Status of one item node, per the design-system status tokens. */
UENUM(BlueprintType)
enum class ECPNodeStatus : uint8
{
	Fulfilled,      // supply >= demand; healthy
	ReadyToDeliver, // objective part fully banked: delivery is the only remaining step
	TimeLimited,    // producers at ~max; only time remains
	UnderSupplied,  // producer exists, supply < demand
	Blocked,        // no producer / no power / hard stop
	RecipeLocked,   // no unlocked recipe produces it
	Saturated,      // full outputs (+standby behind them): surplus, resumes on demand
	StandbyReserve, // deliberately disabled reserve line (recipe machines, pure standby)
	Extraction,     // extractor row: rate fixed by purity/tier; see product row for detail
	Delivered,      // objective part fully delivered: nothing left to do
	Unknown
};

UENUM(BlueprintType)
enum class ECPBlockerReason : uint8
{
	None,
	NoProducer,
	NoPower,
	SupplyBelowDemand,
	/** Legacy M1 heuristic value. Retained for API/ordinal compatibility; analysis no longer emits it. */
	LogisticsSuspected UMETA(Hidden),
	ExtractionBelowDemand,
	ProducerNotConnected, // producer exists, but none of its capacity reaches a consumer
	ConnectivityUnknown,  // graph absent/truncated: do not manufacture a routing verdict
	ByproductBackedUp,    // M2.7: producers fully stalled, outputs full; a co-product has no configured consumer
	InputsStarved,        // machines report missing input (not wired, or upstream not delivering)
	OutputsFull           // machines stalled with outputs backed up (no consumer-less co-product identified)
};

/** Why a solver-backed delivery verdict is unknown. Kept separate from the blocker reason so
 *  presentation can distinguish a proven vehicle route from an incomplete solve. */
UENUM(BlueprintType)
enum class ECPFlowUnknownReason : uint8
{
	None,
	IncompleteOrNonConverged,
	TransportRateUnknown,
	PathAttributionMissing
};

/** Whether a report carries solver-backed flow evidence, and if not, why. Flow enrichment is
 *  OPTIONAL: its absence narrows a report to base objective/ledger/machine-state facts and must
 *  never be reported as a failed analysis, nor allowed to masquerade as measured zeroes. */
UENUM(BlueprintType)
enum class ECPFlowEvidenceState : uint8
{
	/** Base analysis only — no flow pass was run (e.g. FCPAnalysis::Analyze used directly). */
	NotAttempted,
	/** Both solves converged; balances, sufficiency, rate blockers and limiters are present. */
	Available,
	/** The flow graph could not be captured; nothing solver-backed is present. */
	CaptureFailed,
	/** Captured and solved, but a component did not settle. Derived values remain, individually
	 *  marked unknown — a non-converged number is never presented as fact. */
	NotConverged
};

/** The deepest broken link on an objective item's chain. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPBlocker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPBlockerReason Reason = ECPBlockerReason::None;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float SupplyPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float DemandPerMinute = 0.0f;

	/** Items from the objective part down to the blocker (refs so consumers can show icons). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRef> Path;

	/** M2.7: for ByproductBackedUp — the co-product suspected of clogging the line. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Byproduct;

	/** For at-machine issues: how many of the line's machines this condition affects. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 AffectedBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 TotalBuildings = 0;

	/** True when the causal rate/connectivity verdict came from disjoint solved flow domains. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bSolverDerived = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPFlowUnknownReason UnknownReason = ECPFlowUnknownReason::None;
};

/** Which stage constrains a solved item domain. */
UENUM(BlueprintType)
enum class ECPFlowConstraintKind : uint8
{
	None,
	InstalledCapacity, // configured consumer need exceeds installed producer capacity
	InputSupport,      // installed producers cannot be sustained by their supplied inputs
	DeliveryEdge,      // a named belt/pipe edge is saturated
	DeliveryRouting    // sustainable output exists but routing/backpressure prevents delivery
};

/** Exact, solver-evaluated change attached to the report's primary actionable limiter. */
UENUM(BlueprintType)
enum class ECPMarginalActionKind : uint8
{
	None,
	AddAverageMachine, // add one average configured machine to the existing producer line
	UpgradeEdge        // raise the proven belt/pipe edge to the next standard capacity tier
};

/** Saturated physical edge carrying the limited item. EdgeIndex is snapshot-local. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPFlowEdgeEvidence
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 EdgeIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString FromLabel;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ToLabel;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FVector Location = FVector::ZeroVector;

	/** Selected item's solved flow on this edge. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ItemFlowPerMinute = 0.0f;

	/** Total solved flow across all items sharing this edge. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float TotalFlowPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float CapacityPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bFluid = false;

	bool IsSet() const { return EdgeIndex != INDEX_NONE; }
};

/** M2.4: the single worst supplied-but-slow link on a part's configured chain. Ratios are
 *  NETWORK-LEVEL evidence (limiting island, or aggregate fallback) — never a claim about
 *  machine-level allocation. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPLimiter
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	/** Supply/demand ratio at the limiter (limiting-network scope when known). < 0 = not found. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float Ratio = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float SupplyPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float DemandPerMinute = 0.0f;

	/** Configured producer output in the attributed solved domain. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float InstalledPerMinute = 0.0f;

	/** Installed output that the captured upstream inputs can sustain. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float SustainablePerMinute = 0.0f;

	/** True when the ratio came from a complete connectivity picture (limiting island);
	 *  false when it fell back to aggregate configured rates. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bNetworkScoped = false;

	/** True when the limiting ratio came from one disjoint solved flow domain. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bSolverDerived = false;

	/** Snapshot-local solver domain that supplied the limiting evidence. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 DomainId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPFlowConstraintKind ConstraintKind = ECPFlowConstraintKind::None;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPFlowEdgeEvidence EdgeEvidence;

	/** Chain links whose ratio could not be established (incomplete connectivity, no demand
	 *  row). The limiter is the worst KNOWN link; unknowns are disclosed, not guessed. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 UnknownLinks = 0;

	/** Items from the part down to the limiter (inclusive). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRef> Path;

	/** M2.5: minutes until stored stock of the limiter item runs out at the current deficit
	 *  (< 0 = no stock buffering it / unknown). Lower bound — machine buffers are invisible. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float BufferMinutes = -1.0f;

	/** Phase C marginal value. Only the report's primary actionable limiter is perturbed so a
	 *  refresh performs at most one additional full design solve. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPMarginalActionKind MarginalAction = ECPMarginalActionKind::None;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString MachineName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float PerMachinePerMinute = 0.0f;

	/** Delivered rate after the exact perturbation solve. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float MarginalDeliveredPerMinute = 0.0f;

	/** Target capacity for UpgradeEdge; zero for AddAverageMachine. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float MarginalEdgeCapacityPerMinute = 0.0f;

	/** Ratio after the exact perturbation (< 0 when not computed or unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float MarginalRatio = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bMarginalSolverDerived = false;

	bool IsSet() const { return Ratio >= 0.0f && Item.IsValid(); }
};

/** What a node in a PROSPECTIVE (planned) chain is. Deliberately distinct from ECPNodeStatus:
 *  planned (never-built) must never blur with broken (built-but-stalled). */
UENUM(BlueprintType)
enum class ECPPlanNodeKind : uint8
{
	PlannedLine,        // no line exists — build one (recipe/machine named)
	ExistingProduction, // graft point: the real factory already produces this
	RawResource,        // chain leaf: extract it, don't manufacture it
	ResearchLocked,     // no unlocked recipe — research first
	Unknown             // no recipe, not raw, not a known gap (modded/edge case)
};

/** One node of the prospective chain below a part with no producer. Flattened tree:
 *  depth-first order with Depth for indentation (same pattern as FCPBlocker::Path). */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPPlannedNode
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPPlanNodeKind Kind = ECPPlanNodeKind::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 Depth = 0;

	/** For PlannedLine: the assumed (default) recipe and its machine. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString RecipeName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString MachineName;

	/** Default recipe was assumed but unlocked alternates exist — a choice the player can make. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bAlternatesExist = false;

	/** The walk stopped here without resolving (depth/node cap) — subtree continues unseen. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bTruncated = false;

	/** ExistingProduction graft whose line is currently FULLY stopped (0 machines producing) —
	 *  "already produced" would be a lie; the line exists but needs fixing, not building. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bLineStopped = false;

	/** For ResearchLocked nodes that are actually spent nuclear fuel: the fuel to burn.
	 *  Research will never unlock these — consumers must not render "not researched". */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ByproductOfFuel;

	/** Requirement inherited from the prospective parent. The root assumes one default machine
	 *  at 100%; descendants are scaled by recipe stoichiometry. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float RequiredPerMinute = 0.0f;

	/** For ExistingProduction grafts: best sustainable spare capacity in one complete item
	 *  domain. The future line must be connected to that domain for this capacity to apply. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float AvailableHeadroomPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float SufficiencyRatio = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bSufficiencyKnown = false;
};

/** Solver-derived sufficiency for one basis, aggregated across disjoint item domains. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPSufficiencyBasis
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float DemandPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float InstalledPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float SustainablePerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float DeliveredPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float AggregateRatio = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float LimitingDomainDeliveredPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float LimitingDomainDemandPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float LimitingDomainRatio = -1.0f;

	/** False when any demanded domain is truncated, non-converged, or crosses transport whose
	 *  throughput is not measured. Numeric fields remain diagnostic model output, not a claim. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bKnown = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 KnownDomainCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 UnknownDomainCount = 0;
};

/** Analysis-side summary for one item. Phase C Current/Design fields are authoritative and use
 *  disjoint solver domains. Legacy fields remain as design-basis aliases during migration. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPItemSufficiency
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPSufficiencyBasis Current;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPSufficiencyBasis Design;

	/** True when Current/Design and the compatibility aliases came from solved flow domains. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bSolverDerived = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ConnectedSupplyPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ConfiguredDemandPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float AggregateRatio = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float LimitingNetworkSupplyPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float LimitingNetworkDemandPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float LimitingNetworkRatio = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 NetworkCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConsumerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 InactiveConsumerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bKnown = false;

	/** M2.5 runway: minutes until stored stock is exhausted at the current configured deficit
	 *  (owned storage / (demand - connected supply)). < 0 = not applicable (no deficit, no
	 *  stock, or ratio unknown). LOWER bound: machine-internal buffers are invisible. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float BufferMinutes = -1.0f;
};

/** One basis of the four-rate item balance. Every value uses the selected item's display units
 *  per minute. Installed is producer capacity before ingredient support; Sustainable is the
 *  capacity supported by producer inputs; Delivered is flow received by consumers. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPItemBalanceBasis
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float DemandPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float InstalledPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float SustainablePerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float DeliveredPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bKnown = false;
};

/** One recognizable building participating in an item balance. This remains plain snapshot data:
 *  the UI may turn the location into distance/direction text or a temporary locate ping. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPBalanceLocation
{
	GENERATED_BODY()

	/** The ingredient this machine is short of, when it is starved rather than jammed. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString StarvedOfItemName;

	/** Set when a feeding belt is jammed by an item this machine cannot use. Reported instead of
	 *  a plain shortage: the material is present and stuck, so more upstream capacity cannot help. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString JammedByItemName;

	/** Player-facing building type, e.g. "Constructor" or "Manufacturer". */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString Label;

	/** Snapshot identity used only for stable ordering/deduplication. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ActorName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FVector Location = FVector::ZeroVector;

	/** Descriptor-backed identity of the buildable, including its display icon. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef BuildingItem;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bProducer = false;

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

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float ProductivityPercent = 0.0f;

	/** State-derived production or consumption for this item. Estimated, not metered flow. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float EstimatedRatePerMinute = 0.0f;

	/** Item buffered directly at this endpoint (items or m3). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float BufferedAmount = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 PowerShardCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 SomersloopCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef PowerShardItem;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef SomersloopItem;
};

/** Item balance inside one item-specific supply domain. DomainId is snapshot-local and opaque. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPItemBalance
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 DomainId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemBalanceBasis Current;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemBalanceBasis Design;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ProducerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ConsumerBuildings = 0;

	/** Producer-state evidence observed directly from factory actors in this domain. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 MachineStateProducerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ProducingProducerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 NoPowerProducerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 PausedProducerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 MissingInputProducerBuildings = 0;

	/** Subset of MissingInputProducerBuildings that are jammed rather than short of supply. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 JammedProducerBuildings = 0;

	/** The item doing the jamming (first observed). Empty when nothing is jammed. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString JammedByItemName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 OutputBlockedProducerBuildings = 0;

	/** Capacity multiplied by each producer's rolling productivity. This is an estimate from
	 *  observed machine state, not measured network throughput. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float EstimatedCurrentOutputPerMinute = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 MachineStateConsumerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 ProducingConsumerBuildings = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 MissingInputConsumerBuildings = 0;

	/** Demand currently evidenced as in use by consumer machine state. Estimated, not metered. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float EstimatedCurrentUsePerMinute = 0.0f;

	/** Recognizable endpoints for line identity and locate actions. One entry per building. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPBalanceLocation> Locations;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bFluid = false;

	/** Direct inventory of this item reachable within the supply domain (items or m3). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float BufferedAmount = 0.0f;

	/** Minutes the direct buffer could cover the current delivered-flow deficit; -1 if there is
	 *  no known deficit. Kept separate from the four rates because stock is not throughput. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float BufferRunwayMinutes = -1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bUsesTransport = false;

	/** False when this domain crosses a scheduled transport edge whose throughput is not measured. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bTransportRateKnown = true;

	/** Strongest configured-design constraint inside this domain. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPFlowConstraintKind DesignConstraintKind = ECPFlowConstraintKind::None;

	/** Populated when DesignConstraintKind is DeliveryEdge. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPFlowEdgeEvidence DesignEdgeEvidence;
};

/** Per-objective-part verdict: ledger, status, ETA or blocker. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPPartReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 Required = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 Remaining = 0;

	/** Owned stock counted toward the requirement (clamped to Remaining): delivery-only. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 Banked = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 StillToProduce = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPNodeStatus Status = ECPNodeStatus::Unknown;

	/** Producing-machine effective output (capacity prorated by producing/configured). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float EffectiveRatePerMinute = 0.0f;

	/** Minutes of production remaining; < 0 when not computable. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	float EtaMinutes = -1.0f;

	/** Fully banked: production done, delivery is the only remaining step. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bPayable = false;

	// Owned-stock breakdown (UNCAPPED totals, unlike Banked which clamps to Remaining) — for
	// inspection surfaces: where the banked parts physically are.
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int64 OwnedInStorage = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int64 OwnedInDepot = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int64 OwnedInPockets = 0;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPBlocker Blocker;

	/** Prospective chain (M2): when the blocker is NoProducer, the planned subtree from the
	 *  missing item down to graft points in the real factory. Empty otherwise. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPPlannedNode> PlannedChain;

	/** M2.4: the worst-ratio link on the configured chain (set when a deficit exists). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPLimiter Limiter;

	/** ALL concurrent at-machine issues on this part's own line, worst first (a machine can be
	 *  unpowered AND unfed AND clogged at once — one reason is half the truth). The deep
	 *  root-cause walk (Blocker) is separate: it explains the chain, this explains the machine. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPBlocker> Issues;
};

/** M2.7b: a research gap expanded into an actionable plan — the unlocking schematic's costs
 *  run through the SAME part-report machinery as objective items (ledger, status, blocker,
 *  prospective chain, limiter), so research payments join the build tiers. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPResearchGapReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPResearchGap Gap;

	/** One entry per unlock-cost item. Empty when the schematic is the selected milestone
	 *  (its live ledger is already the milestone objective) or costs were not captured. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPPartReport> CostParts;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPObjectiveReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString ObjectiveName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPObjectiveKind Kind = ECPObjectiveKind::Milestone;

	/** Every remaining requirement is covered by owned stock: delivery is the only step left. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bPayableNow = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPPartReport> Parts;
};

/** One tier of the aggregated build order: actions whose prerequisites are all satisfied by
 *  earlier tiers (or by existing working production/raw resources). Two action kinds: BUILD a
 *  new line (Items) and FIX an existing broken line (Fixes) — a stopped machine is a step in
 *  the order, not a wall that hides its dependents. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPBuildStep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPItemRef Item;

	/** What this step unblocks: the planned items that consume it, from the tier above. Empty
	 *  when it feeds an objective part directly (the top of the chain). A step listed without
	 *  this is just a name; with it the plan reads as a chain -- X, Y, Z are needed for A, A is
	 *  needed for B -- which is the question a build order actually has to answer. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRef> NeededFor;

	/** Objectives this step IS a requirement of, as opposed to items that consume it.
	 *
	 *  A part the objective asks for directly is a chain ROOT, not anyone's ingredient, so it
	 *  never appears in NeededFor and the step read as purposeless — worst for the pivot lines
	 *  that satisfy a milestone AND feed other steps, where the milestone is the reason a later
	 *  unlock is reachable at all. Bare objective names; the presentation layer knows how to
	 *  label a milestone versus an elevator phase. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FString> NeededForObjectives;
};

/** Research the player must complete for the plan to continue.
 *
 *  This exists because a locked recipe is frequently NOT a separate concern running alongside the
 *  build order -- it is a link in it. The canonical case: a tracked milestone's last outstanding
 *  requirement is itself a planned line, and completing that milestone unlocks a part of the
 *  objective. Presenting that as a parallel track (or omitting it) hides a step the player has to
 *  take, and leaves later steps looking permanently unreachable. */
USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPUnlockStep
{
	GENERATED_BODY()

	/** The milestone/research completed by this step. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FString SchematicName;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPResearchSource Source = ECPResearchSource::Unknown;

	/** Items this step makes buildable — the reason it is in the plan at all. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemRef> Unlocks;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPBuildTier
{
	GENERATED_BODY()

	/** New lines to build. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPBuildStep> Items;

	/** Existing lines to repair (unpowered/starved/paused). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPBuildStep> Fixes;

	/** Research to complete, once everything it requires is built. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPUnlockStep> Unlocks;
};

USTRUCT(BlueprintType)
struct CRITICALPATHENGINE_API FCPAnalysisResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPObjectiveReport> Objectives;

	/** M2 build order: planned lines aggregated ACROSS all objective parts and bucketed by
	 *  dependency tier — tier 0 items depend only on existing production/raw resources; each
	 *  later tier depends on the tiers before it. Deduplicated (an item planned under several
	 *  parts appears once, at its deepest requirement). Empty when nothing needs a new line. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPBuildTier> BuildTiers;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPResearchGap> ResearchGaps;

	/** M2.7b: research gaps expanded into unlock-cost plans (their planned chains feed the
	 *  build tiers alongside the objective parts'). */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPResearchGapReport> ResearchPlans;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemSufficiency> Sufficiency;

	/** Phase C solver-backed four-rate balances, one row per item-specific supply domain. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	TArray<FCPItemBalance> ItemBalances;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	FCPTruncation Truncation;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bObservedStatsAvailable = false;

	/** Whether solver-backed evidence (item balances, sufficiency, rate blockers, limiters) is
	 *  present in this result. Objectives, the owned ledger, machine-state facts and research
	 *  gaps come from the base pass and remain valid in EVERY state — flow enrichment failing
	 *  must narrow the report, never void it. */
	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	ECPFlowEvidenceState FlowEvidence = ECPFlowEvidenceState::NotAttempted;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bHasPlayerContext = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	bool bMilestoneSelected = false;

	UPROPERTY(BlueprintReadOnly, Category = "CriticalPath")
	int32 SelectableMilestoneCount = 0;
};
