// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "Types/SlateEnums.h" // ETextCommit::Type on the filter's commit handler
#include "CPEngineTypes.h"
#include "Representation/FGMapMarkerRepresentation.h"
#include "CPPanelWidget.generated.h"

class UBorder;
class UComboBoxString;
class UEditableTextBox;
class UFGActorRepresentation;
class UScrollBox;
class UTextBlock;
class UTexture2D;
class UVerticalBox;
class UCPPanelWidget;
class UCPBalanceLocator;

DECLARE_MULTICAST_DELEGATE(FCPOnPanelCloseRequested);
DECLARE_MULTICAST_DELEGATE(FCPOnPanelRefreshRequested);

/** A temporary locate marker rendered through the game's icon-database-backed map marker path. */
UCLASS()
class CRITICALPATH_API UCPNamedPingRepresentation : public UFGMapMarkerRepresentation
{
	GENERATED_BODY()

public:
	virtual bool IsImportantCompassRepresentation() const override { return true; }
};

/** Per-row click handler payload: dynamic delegates carry no sender, so each collapsible part
 *  row binds its border's mouse-down to one of these holding the chain box it toggles. */
UCLASS()
class UCPChainToggle : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UWidget> Target;

	/** The row's disclosure chevron (▼): rotated -90° while Target is collapsed. */
	UPROPERTY()
	TObjectPtr<UTextBlock> Chevron;

	/** Optional lazy population. A balance row's endpoint list is the expensive part of the panel
	 *  (one line per producer and per consumer) and it starts collapsed, so building it up front
	 *  spends most of the panel's UObject budget on rows nobody opened. When these are set, the
	 *  endpoint rows are built on the FIRST expand instead, and never at all if the row is never
	 *  opened. LazyBalanceIndex indexes UCPPanelWidget::CachedReport.ItemBalances. */
	UPROPERTY()
	TObjectPtr<UCPPanelWidget> LazyOwner;

	int32 LazyBalanceIndex = INDEX_NONE;
	bool bLazyBuilt = false;

	UFUNCTION()
	FEventReply OnRowMouseDown(FGeometry InGeometry, const FPointerEvent& InMouseEvent);
};

/**
 * The Critical Path panel (M1 slice): a programmatic widget tree styled entirely from CPStyle,
 * populated from FCPAnalysisResult. Layout mirrors the design-system mockup: header band,
 * summary band, scrollable path list (part rows + indented blocker-chain rows), verdict band.
 *
 * Rows are built programmatically on purpose (no ListView: row count is bounded by the
 * objective-item cap, so virtualization buys nothing). Part rows with a blocker chain toggle
 * the chain on click; rows carry tooltips with the owned-stock breakdown and rates.
 */
UCLASS()
class CRITICALPATH_API UCPPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Fired when the user asks to close (Esc). The owning subsystem removes the widget. */
	FCPOnPanelCloseRequested OnCloseRequested;

	/** The world keeps running while the panel is open, so every report is stale the moment it is
	 *  built. Lets the player re-measure without closing and reopening. */
	FCPOnPanelRefreshRequested OnRefreshRequested;

	/** Populate from an analysis result. DataAgeText e.g. "just now" / "42s ago".
	 *  QUEUES the repopulation; the tree is rewritten on the next NativeTick. REFRESH reaches
	 *  this from inside a Slate button handler, and rebuilding there destroys widgets Slate is
	 *  still holding for the event it is dispatching. See NativeTick. */
	void SetReport(const FCPAnalysisResult& Result, const FText& DataAgeText, bool bBalancePending = false);

	/** Clear every view and state, in place of a report, WHY no report can be produced. Used when
	 *  the evidence is not readable here at all — e.g. a remote client, where production data is
	 *  server-side. Showing an empty or zeroed report instead would read as a broken factory.
	 *  Queued on the same frame boundary as SetReport, and for the same reason. */
	void SetUnavailable(const FText& Headline, const FText& Detail);

	/** Creates a long-lived map/compass marker owned by this panel. */
	void CreateLocationPing(const FVector& Location, const FCPItemRef& IconItem, const FText& Label);

	/** Removes only the location markers created by this panel. */
	UFUNCTION()
	void ClearLocationPings();

	/** Asks the owning subsystem to re-measure the world and repopulate this panel. */
	UFUNCTION()
	void RequestRefresh();

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual FReply NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent) override;

	// Fully modal while open: the fullscreen root consumes every mouse event so nothing reaches
	// the game or its menus underneath (a click-through save-menu soft-locked the UI).
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	void AddSectionHeader(const FText& Title);
	/** A full-width secondary-text row for section-level states (no milestone selected, etc.). */
	void AddNoticeRow(const FText& Notice);
	/** ResearchName: the unlocking research for a RecipeLocked part (empty when none/known).
	 *  Returns the row border — the click target when the part has a collapsible chain.
	 *  OutChevron receives the disclosure triangle (null when bHasChain is false). */
	UBorder* AddPartRow(const FCPPartReport& Part, const FString& ResearchName, bool bHasChain, UTextBlock*& OutChevron);
	/** Resolve the item's descriptor icon and prepend it to Box; no-op when unresolvable. */
	void AddItemIcon(class UHorizontalBox* Box, const FCPItemRef& Item, float IconSize);
	/** Builds the indented chain rows inside their own container and returns it (toggle target). */
	/** One full-width red reason row (at-machine issue or at-part blocker). */
	void AddReasonRow(const FCPBlocker& Reason, UVerticalBox* Target);
	/** bSuppressAtPartReason: caller already rendered the at-part conditions (issue list). */
	class UVerticalBox* AddChainRows(const FCPBlocker& Blocker, bool bSuppressAtPartReason);
	/** M2 prospective chain: renders planned-line/graft/raw/locked rows into Target (the same
	 *  collapsible container as the blocker chain). */
	void AddPlannedRows(const TArray<FCPPlannedNode>& Chain, UVerticalBox* Target);
	/** M2.4: the worst-ratio chain link, when it adds information beyond the blocker. */
	void AddLimiterRow(const FCPLimiter& Limiter, UVerticalBox* Target);
	/** M2.7b: renders a research gap's unlock-cost ledger rows into the part's chain box. */
	void AddResearchPlanRows(const FCPResearchGapReport& Plan, UVerticalBox* Target);
	/** The Plan rows: one numbered row per tier action (Fix = red, Build = accent), rendered
	 *  into the band's own independently scrolling list. */
	void AddPlanRows(const FCPAnalysisResult& Result, UVerticalBox* Target);
	/** Objective parts the plan cannot cover (locked recipes), stated as their own separate work. */
	void AddPlanExclusionRows(const FCPAnalysisResult& Result,
		const TMap<FString, FString>& ResearchByItem, UVerticalBox* Target);
	/** Solver-backed, objective-scoped four-rate rows for the Balance tab. */
	void AddBalanceRows(const FCPAnalysisResult& Result, bool bBalancePending);
	void AddResearchGapRow(const FCPResearchGap& Gap);
	FText MakeVerdict(const FCPAnalysisResult& Result) const;

	UFUNCTION() void ShowPathTab();
	UFUNCTION() void ShowBalanceTab();
	UFUNCTION() void ShowPlanTab();
	UFUNCTION() void OnBalanceFilterCommitted(const FText& Text, ETextCommit::Type CommitMethod);

	/** Dropdown selection: ALL LINES / NEEDS ATTENTION / NO POWER / MISSING INPUT / JAMMED / PAUSED. */
	UFUNCTION() void OnBalanceProblemFilterSelected(FString SelectedItem, ESelectInfo::Type SelectionType);

	static constexpr int32 BalanceProblemFilterModeCount = 6;
	static FText BalanceProblemFilterLabel(int32 Mode);

	/** Problem-state test for one line, applied during candidate selection so it narrows the set
	 *  BEFORE the render cap rather than filtering whatever survived it. */
	bool BalanceMatchesProblemFilter(const FCPItemBalance& Balance) const;
	UFUNCTION() void ToggleBalanceSort();
	UFUNCTION() void ToggleBalanceAugmentFilter();
	void SetActiveTab(int32 TabIndex);
	void RebuildBalanceRows();
	void ApplyBalanceFilter();

	/** Text-filter test for one line, run during candidate selection so the search covers the whole
	 *  report rather than only the rows that survived the render cap. */
	bool BalanceMatchesFilter(const FCPItemBalance& Balance) const;

public:
	/** Populates a balance row's PRODUCERS/CONSUMERS lists on first expand. Called from NativeTick,
	 *  never from an input handler: see MaxBalanceRowsRendered for why it is lazy, and NativeTick
	 *  for why it is deferred. */
	void BuildEndpointSections(int32 BalanceIndex, UVerticalBox* Target);

	/** Requests the above for the next tick. Safe to call from a mouse handler. */
	void QueueEndpointBuild(int32 BalanceIndex, UVerticalBox* Target);

private:

	UPROPERTY() TObjectPtr<UTextBlock> SummaryText;
	UPROPERTY() TObjectPtr<UTextBlock> DataAgeTextBlock;
	UPROPERTY() TObjectPtr<UVerticalBox> ElevatorColumn;
	UPROPERTY() TObjectPtr<UVerticalBox> MilestoneColumn;
	UPROPERTY() TObjectPtr<UVerticalBox> GapList;
	UPROPERTY() TObjectPtr<UScrollBox> PathScroll;
	UPROPERTY() TObjectPtr<UScrollBox> BalanceScroll;
	UPROPERTY() TObjectPtr<UWidget> BalanceTools;
	UPROPERTY() TObjectPtr<UScrollBox> PlanScroll;
	UPROPERTY() TObjectPtr<UVerticalBox> BalanceList;
	UPROPERTY() TObjectPtr<UTextBlock> BalanceSortText;
	UPROPERTY() TObjectPtr<UTextBlock> BalanceAugmentFilterText;
	UPROPERTY() TObjectPtr<UComboBoxString> BalanceProblemFilterCombo;

	/** Deferred widget-tree work, serviced in NativeTick. Mutating the tree from inside a click or
	 *  commit handler is what faulted Slate's paint pass. */
	bool bBalanceRebuildQueued = false;
	UPROPERTY() TObjectPtr<UVerticalBox> PendingEndpointTarget;
	int32 PendingEndpointBalanceIndex = INDEX_NONE;
	UPROPERTY() TArray<TObjectPtr<UBorder>> BalanceFilterRows;
	TArray<FString> BalanceFilterKeys;
	UPROPERTY() TObjectPtr<UBorder> BalanceNoMatchesRow;
	FCPAnalysisResult CachedReport;
	FString BalanceFilter;
	bool bCachedBalancePending = false;
	bool bBalanceNeedsFirst = false;
	/** 0 = all, 1 = Power Shards, 2 = Somersloops. */
	int32 BalanceAugmentFilter = 0;

	/** 0 all, 1 needs attention, 2 no power, 3 missing input, 4 jammed, 5 paused. */
	int32 BalanceProblemFilter = 0;

	/** Item names with at least one producer in ANY domain. Lets a line with demand and no local
	 *  producer say "produced elsewhere" instead of "build more machines" when a teleporter,
	 *  portal or loader carries it in by a route the flow walk cannot follow. */
	TSet<FString> ItemsProducedSomewhere;

	/** The objective's deliverables. Zero machine demand is the EXPECTED state for these, since
	 *  the Space Elevator and the HUB are not belt-connected consumers. */
	TSet<FString> ObjectiveItems;

	/** Items no recipe produces (power slugs, alien remains). Never advise building production. */
	TSet<FString> WorldGatheredItems;

	/** The column Add* row builders currently append to (valid only inside SetReport). */
	UVerticalBox* CurrentList = nullptr;

	/** Keeps the per-row chain-collapse handlers alive for the report's lifetime. */
	UPROPERTY() TArray<TObjectPtr<UCPChainToggle>> ChainToggles;
	/** Keeps per-row locate handlers alive for the report's lifetime. */
	UPROPERTY() TArray<TObjectPtr<UCPBalanceLocator>> BalanceLocators;
	/** Retained so Clear Pings never removes ordinary player/team pings. */
	UPROPERTY(Transient) TArray<TObjectPtr<UFGActorRepresentation>> ActiveLocationPings;

	/** Status glyphs spun by NativeTick: motion = the line is actively producing. */
	UPROPERTY() TArray<TObjectPtr<UWidget>> Spinners;

	/** Icon textures referenced by brushes anywhere in this panel. A UImage's brush is the only
	 *  thing keeping its texture alive, and every refresh calls ClearChildren() and orphans the
	 *  old images -- if GC then collects them while Slate still paints a cached element, the
	 *  brush's ResourceObject dangles and SImage::OnPaint reads freed memory. Rooting the
	 *  textures for the panel's lifetime removes that window entirely. */
	UPROPERTY() TArray<TObjectPtr<UTexture2D>> RootedIconTextures;
	float SpinnerAngle = 0.0f;
	UPROPERTY() TObjectPtr<UBorder> VerdictBand;
	UPROPERTY() TObjectPtr<UTextBlock> VerdictText;
	/** The Plan list inside the verdict band (independently scrollable). */
	UPROPERTY() TObjectPtr<UVerticalBox> PlanList;

	/** Queued repopulation, applied by NativeTick. Only the newest is kept: two refreshes in one
	 *  frame should render the newer answer once, not both in sequence. */
	enum class EPendingView : uint8 { None, Report, Unavailable };
	EPendingView PendingView = EPendingView::None;
	FCPAnalysisResult PendingResult;
	FText PendingAgeText;
	FText PendingHeadline;
	FText PendingDetail;
	bool bPendingBalancePending = false;

	/** The tree edits SetReport/SetUnavailable used to perform inline. */
	void ApplyReport(const FCPAnalysisResult& Result, const FText& DataAgeText, bool bBalancePending);
	void ApplyUnavailable(const FText& Headline, const FText& Detail);
};

/** Per-balance-row handler that turns a captured endpoint into a temporary world ping. */
UCLASS()
class UCPBalanceLocator : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	TObjectPtr<UCPPanelWidget> Owner;

	UPROPERTY()
	FVector Location = FVector::ZeroVector;

	UPROPERTY()
	FCPItemRef IconItem;

	UPROPERTY()
	FText Label;

	UFUNCTION()
	void Locate();
};
