// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPAnalysis.h"

namespace
{
// Ground-truth thresholds carried over from the proven prototype.
constexpr float NearFullProductivityPercent = 95.0f;
constexpr int32 MaxWalkDepth = 6;

const FCPProductRow* Product(const FCPFactorySnapshot& S, const FString& Name) { return S.FindProduct(Name); }

constexpr int32 MaxPlanDepth = 8;
constexpr int32 MaxPlanNodes = 40;

/** Prospective walk (M2): expand a no-producer item through the captured catalog slice until
 *  each branch grafts onto existing production, a raw resource, or a research gap. Depth-first
 *  so the flattened output renders as an indented tree. OnStack is the recursion path only —
 *  an item may legitimately appear under two sibling branches; only true cycles are cut. */
void ExpandPlannedChain(const FCPFactorySnapshot& S, const FCPItemRef& ItemRef, int32 Depth,
	float RequiredPerMinute, TSet<FString>& OnStack, TArray<FCPPlannedNode>& Out)
{
	if (Out.Num() >= MaxPlanNodes)
	{
		return;
	}

	FCPPlannedNode Node;
	Node.Item = ItemRef;
	Node.Depth = Depth;
	Node.RequiredPerMinute = RequiredPerMinute;

	// A cycle (recycled-plastic style loops) renders once; the repeat is cut, not an error.
	if (OnStack.Contains(ItemRef.Name))
	{
		return;
	}

	if (const FCPProductRow* Row = Product(S, ItemRef.Name))
	{
		// Graft point: the real factory already makes (or extracts) this. Stop here. Quantitative
		// headroom is deliberately left unknown until disjoint flow domains are available; the old
		// overlapping island walk could make an unrelated surplus look usable by this graft.
		Node.Item = Row->Item; // richer ref (descriptor path)
		Node.Kind = ECPPlanNodeKind::ExistingProduction;
		Node.MachineName = Row->MachineName;
		// A BROKEN-stopped line is a graft in name only. Idle-with-full-buffers (saturation)
		// and standby are benign — a Constructor with 140/min spare sitting on full outputs is
		// working as intended, not "stopped, fix it" (live-caught).
		Node.bLineStopped = Row->ConfiguredBuildings > 0 && Row->ProducingBuildings == 0 &&
			(Row->NoPowerBuildings > 0 || Row->MissingInputBuildings > 0 || Row->PausedBuildings > 0);
		Out.Add(MoveTemp(Node));
		return;
	}
	if (const FCPCatalogRecipe* Recipe = S.FindCatalogRecipe(ItemRef.Name))
	{
		Node.Item = Recipe->Product;
		// A captured LOCKED recipe tells us what the line will need; it does not make the line
		// buildable. The node stays ResearchLocked so every consumer keeps treating it as gated,
		// but we descend into its ingredients anyway — that descent is the whole point, because
		// it is how a co-locked ingredient becomes a visible step instead of a nasty surprise.
		Node.Kind = Recipe->bRecipeLocked ? ECPPlanNodeKind::ResearchLocked : ECPPlanNodeKind::PlannedLine;
		if (Recipe->bRecipeLocked)
		{
			if (const FCPResearchGap* LockedGap = S.ResearchGaps.FindByPredicate([&](const FCPResearchGap& G)
				{ return G.NeededItem.Name.Equals(ItemRef.Name, ESearchCase::IgnoreCase); }))
			{
				Node.ByproductOfFuel = LockedGap->ByproductOfFuel;
			}
		}
		Node.RecipeName = Recipe->RecipeName;
		Node.MachineName = Recipe->MachineName;
		Node.bAlternatesExist = Recipe->bAlternatesExist;
		if (Node.RequiredPerMinute <= 0.01f)
		{
			Node.RequiredPerMinute = Recipe->ProductPerMinutePerMachine;
		}
		Node.bTruncated = (Depth + 1 > MaxPlanDepth || Out.Num() + 1 >= MaxPlanNodes) && Recipe->Ingredients.Num() > 0;
		Out.Add(Node);
		if (!Node.bTruncated)
		{
			OnStack.Add(ItemRef.Name);
			for (const FCPItemRate& Ingredient : Recipe->Ingredients)
			{
				const float Scale = Recipe->ProductPerMinutePerMachine > 0.01f
					? Node.RequiredPerMinute / Recipe->ProductPerMinutePerMachine : 1.0f;
				ExpandPlannedChain(S, Ingredient.Item, Depth + 1,
					Ingredient.RatePerMinute * Scale, OnStack, Out);
			}
			OnStack.Remove(ItemRef.Name);
		}
		return;
	}
	if (S.IsRawResource(ItemRef.Name))
	{
		Node.Kind = ECPPlanNodeKind::RawResource;
		Out.Add(MoveTemp(Node));
		return;
	}
	const FCPResearchGap* Gap = S.ResearchGaps.FindByPredicate([&](const FCPResearchGap& G)
	{
		return G.NeededItem.Name.Equals(ItemRef.Name, ESearchCase::IgnoreCase);
	});
	Node.Kind = Gap ? ECPPlanNodeKind::ResearchLocked : ECPPlanNodeKind::Unknown;
	if (Gap)
	{
		// Spent nuclear fuel: research will never unlock it — carry the truth to consumers.
		Node.ByproductOfFuel = Gap->ByproductOfFuel;
	}
	Out.Add(MoveTemp(Node));
}

float EffectiveRate(const FCPProductRow& Row)
{
	if (Row.ConfiguredBuildings <= 0)
	{
		return 0.0f;
	}
	return Row.ConfiguredCapacityPerMinute * (static_cast<float>(Row.ProducingBuildings) / static_cast<float>(Row.ConfiguredBuildings));
}

// Saturation: idle only because outputs are full (standby behind them counts as the same
// backpressure). Deliberate reserve: every idle machine in standby with no other issue.
bool IsSaturated(const FCPProductRow& Row)
{
	return Row.OutputBlockedBuildings > 0 &&
		Row.NoPowerBuildings == 0 &&
		Row.PausedBuildings == 0 &&
		Row.MissingInputBuildings == 0;
}

bool IsStandbyReserve(const FCPProductRow& Row)
{
	return Row.StandbyBuildings > 0 &&
		Row.OutputBlockedBuildings == 0 &&
		Row.NoPowerBuildings == 0 &&
		Row.PausedBuildings == 0 &&
		Row.MissingInputBuildings == 0;
}

bool DescendToBlocker(const FCPFactorySnapshot& S, const FCPItemRef& ItemRef, TSet<FString>& Visited, TArray<FCPItemRef>& Path, FCPBlocker& Out)
{
	const FString& ItemName = ItemRef.Name;
	if (Visited.Contains(ItemName) || Path.Num() > MaxWalkDepth)
	{
		return false;
	}
	Visited.Add(ItemName);

	const FCPProductRow* Row = Product(S, ItemName);
	// Prefer the product row's ref (it carries the descriptor path even when the caller's doesn't).
	Path.Add(Row ? Row->Item : ItemRef);

	if (!Row)
	{
		Out.Item = ItemRef;
		// "Nothing produces it" is only actionable when something COULD produce it. For an item
		// no recipe makes, the honest answer is where to find it, not how many machines to build.
		Out.Reason = S.WorldGatheredItemNames.Contains(ItemName)
			? ECPBlockerReason::WorldGatheredOnly
			: ECPBlockerReason::NoProducer;
		Out.Path = Path;
		return true;
	}

	Out.Item = Row->Item;

	if (Row->NoPowerBuildings > 0 && Row->ProducingBuildings == 0)
	{
		Out.Reason = ECPBlockerReason::NoPower;
		Out.Path = Path;
		return true;
	}

	// M2.7 byproduct disposal: the line is FULLY stalled with outputs backed up (not starved,
	// not unpowered) and its recipe emits a co-product with no configured consumer — the
	// co-product is the suspected clog. Facts (stall + no consumer) are certain; which output
	// is physically full is the stated inference. Storage/sink disposal is invisible to the
	// demand table, hence "check its disposal", not "broken".
	if (Row->ProducingBuildings == 0 && Row->OutputBlockedBuildings > 0 && Row->MissingInputBuildings == 0)
	{
		if (const FCPRecipeEdge* Edge = S.FindEdgeProducing(ItemName))
		{
			for (const FCPItemRate& CoProduct : Edge->Products)
			{
				if (!CoProduct.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase) && !S.FindDemand(CoProduct.Item.Name))
				{
					Out.Reason = ECPBlockerReason::ByproductBackedUp;
					Out.Byproduct = CoProduct.Item;
					Out.Path = Path;
					return true;
				}
			}
		}
	}

	if (Row->MissingInputBuildings > 0)
	{
		// Starved: the true cause is deeper — descend into this item's configured ingredients.
		if (const FCPRecipeEdge* Edge = S.FindEdgeProducing(ItemName))
		{
			for (const FCPItemRate& Ingredient : Edge->Ingredients)
			{
				if (DescendToBlocker(S, Ingredient.Item, Visited, Path, Out))
				{
					return true;
				}
			}
		}
		// Item aggregates prove a fully stranded producer, but cannot prove routing to this one
		// consumer when the item also feeds other branches. Never revive the old heuristic here.
		Out.Item = Row->Item;
		Out.Reason = ECPBlockerReason::ConnectivityUnknown;
		Out.Path = Path;
		return true;
	}

	Path.Pop();
	return false;
}
/** Build-tier aggregation: fold every part's planned chain into one deduplicated build order.
 *  A planned line's tier is 1 + the max tier of its planned children (grafts/raws/locked
 *  children contribute 0), so tier 1 = buildable against existing production right now, and
 *  each later tier waits on the ones before it. An item planned under several parts lands at
 *  its DEEPEST requirement. Recursive over the depth-flattened chain. */
int32 FoldPlanTiers(const TArray<FCPPlannedNode>& Chain, int32& Index, TMap<FString, TPair<int32, FCPItemRef>>& ItemTiers,
	const TMap<FString, TPair<int32, FCPItemRef>>* FixTiers = nullptr,
	TMap<FString, TArray<FCPItemRef>>* NeededFor = nullptr, const FCPItemRef* Parent = nullptr)
{
	const FCPPlannedNode Node = Chain[Index];
	Index++;
	// A planned node is an INGREDIENT of its parent, so the parent is what it unblocks. Recorded
	// here because the tier fold below flattens the chain and the linkage is unrecoverable after.
	// Research-locked nodes carry attribution too, now that a captured locked recipe gives them
	// real ingredient edges. Without this a step that exists ONLY to feed a locked part (the
	// Singularity Cell case) renders with no stated purpose, which is the one thing the plan is
	// supposed to prevent. The self-check guards the chain root, where node and parent coincide.
	const bool bAttributable = Node.Kind == ECPPlanNodeKind::PlannedLine || Node.Kind == ECPPlanNodeKind::ResearchLocked;
	if (NeededFor && Parent && bAttributable && !Node.Item.Name.IsEmpty() && Node.Item.Name != Parent->Name)
	{
		TArray<FCPItemRef>& Consumers = NeededFor->FindOrAdd(Node.Item.Name);
		if (!Consumers.ContainsByPredicate([Parent](const FCPItemRef& Existing) { return Existing.Name == Parent->Name; }))
		{
			Consumers.Add(*Parent);
		}
	}
	const FCPItemRef* ChildParent = bAttributable ? &Node.Item : Parent;
	int32 MaxChild = 0;
	while (Index < Chain.Num() && Chain[Index].Depth > Node.Depth)
	{
		if (Chain[Index].Depth == Node.Depth + 1)
		{
			MaxChild = FMath::Max(MaxChild, FoldPlanTiers(Chain, Index, ItemTiers, FixTiers, NeededFor, ChildParent));
		}
		else
		{
			Index++; // malformed depth jump — skip defensively
		}
	}
	if (Node.Kind != ECPPlanNodeKind::PlannedLine)
	{
		// A BROKEN graft is a dependency like any other: fixing that line is a build-order
		// step, so its dependents sort after it.
		if (FixTiers && Node.Kind == ECPPlanNodeKind::ExistingProduction && Node.bLineStopped)
		{
			if (const TPair<int32, FCPItemRef>* Fix = FixTiers->Find(Node.Item.Name))
			{
				return Fix->Key;
			}
		}
		return 0;
	}
	const int32 Tier = MaxChild + 1;
	TPair<int32, FCPItemRef>& Entry = ItemTiers.FindOrAdd(Node.Item.Name, TPair<int32, FCPItemRef>(0, Node.Item));
	if (Tier > Entry.Key)
	{
		Entry = TPair<int32, FCPItemRef>(Tier, Node.Item);
	}
	return Tier;
}

} // namespace

const FCPProductRow* FCPFactorySnapshot::FindProduct(const FString& ItemName) const
{
	return Products.FindByPredicate([&](const FCPProductRow& R) { return R.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase); });
}

const FCPDemandRow* FCPFactorySnapshot::FindDemand(const FString& ItemName) const
{
	return Demands.FindByPredicate([&](const FCPDemandRow& R) { return R.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase); });
}

const FCPRecipeEdge* FCPFactorySnapshot::FindEdgeProducing(const FString& ItemName) const
{
	return Edges.FindByPredicate([&](const FCPRecipeEdge& E)
	{
		return E.Products.ContainsByPredicate([&](const FCPItemRate& P) { return P.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase); });
	});
}

const FCPOwnedRow* FCPFactorySnapshot::FindOwned(const FString& ItemName) const
{
	return Owned.FindByPredicate([&](const FCPOwnedRow& R) { return R.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase); });
}

const FCPCatalogRecipe* FCPFactorySnapshot::FindCatalogRecipe(const FString& ItemName) const
{
	return Catalog.FindByPredicate([&](const FCPCatalogRecipe& R) { return R.Product.Name.Equals(ItemName, ESearchCase::IgnoreCase); });
}

bool FCPFactorySnapshot::IsRawResource(const FString& ItemName) const
{
	return RawResourceNames.ContainsByPredicate([&](const FString& N) { return N.Equals(ItemName, ESearchCase::IgnoreCase); });
}

ECPNodeStatus FCPAnalysis::ClassifyItem(const FCPFactorySnapshot& Snapshot, const FString& ItemName)
{
	const FCPProductRow* Row = Snapshot.FindProduct(ItemName);
	if (!Row)
	{
		const bool bLocked = Snapshot.ResearchGaps.ContainsByPredicate([&](const FCPResearchGap& G)
		{
			return G.NeededItem.Name.Equals(ItemName, ESearchCase::IgnoreCase);
		});
		return bLocked ? ECPNodeStatus::RecipeLocked : ECPNodeStatus::Blocked;
	}

	if (Row->bIsExtraction)
	{
		return ECPNodeStatus::Extraction;
	}
	if (Row->NoPowerBuildings > 0 && Row->ProducingBuildings == 0)
	{
		return ECPNodeStatus::Blocked;
	}
	if (IsSaturated(*Row))
	{
		return ECPNodeStatus::Saturated;
	}
	if (IsStandbyReserve(*Row) && Row->ProducingBuildings == 0)
	{
		return ECPNodeStatus::StandbyReserve;
	}

	if (Row->MissingInputBuildings > 0)
	{
		return ECPNodeStatus::UnderSupplied;
	}
	if (Row->ProducingBuildings > 0 && Row->AverageProductivityPercent >= NearFullProductivityPercent)
	{
		return ECPNodeStatus::TimeLimited;
	}
	return ECPNodeStatus::Fulfilled;
}

bool FCPAnalysis::FindBlocker(const FCPFactorySnapshot& Snapshot, const FString& ItemName, FCPBlocker& OutBlocker)
{
	TSet<FString> Visited;
	TArray<FCPItemRef> Path;
	OutBlocker = FCPBlocker();
	FCPItemRef Ref;
	Ref.Name = ItemName;
	return DescendToBlocker(Snapshot, Ref, Visited, Path, OutBlocker);
}

namespace
{
/** Enumerate EVERY concurrent at-machine condition on the part's own line, worst first —
 *  a machine can be unpowered AND unfed AND clogged simultaneously; reporting only the first
 *  proved condition is half the truth (live-caught on a fresh Manufacturer). This complements
 *  the deep root-cause walk: Blocker explains the chain, Issues explain the machine. */
void CollectLocalIssues(const FCPFactorySnapshot& S, const FString& ItemName, TArray<FCPBlocker>& Out)
{
	const FCPProductRow* Row = S.FindProduct(ItemName);
	if (!Row)
	{
		return; // no line at all — NoProducer is already the (single) whole truth
	}

	const auto Add = [&](ECPBlockerReason Reason, int32 Affected)
	{
		FCPBlocker Issue;
		Issue.Item = Row->Item;
		Issue.Reason = Reason;
		Issue.AffectedBuildings = Affected;
		Issue.TotalBuildings = Row->ConfiguredBuildings;
		Out.Add(MoveTemp(Issue));
	};

	// Worst-first ordering: dead (power) → clogged (outputs) → starving (inputs) → routing.
	if (Row->NoPowerBuildings > 0)
	{
		Add(ECPBlockerReason::NoPower, Row->NoPowerBuildings);
	}
	if (Row->OutputBlockedBuildings > 0)
	{
		bool bByproduct = false;
		if (const FCPRecipeEdge* Edge = S.FindEdgeProducing(ItemName))
		{
			for (const FCPItemRate& CoProduct : Edge->Products)
			{
				if (!CoProduct.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase) && !S.FindDemand(CoProduct.Item.Name))
				{
					FCPBlocker Issue;
					Issue.Item = Row->Item;
					Issue.Reason = ECPBlockerReason::ByproductBackedUp;
					Issue.Byproduct = CoProduct.Item;
					Issue.AffectedBuildings = Row->OutputBlockedBuildings;
					Issue.TotalBuildings = Row->ConfiguredBuildings;
					Out.Add(MoveTemp(Issue));
					bByproduct = true;
					break;
				}
			}
		}
		if (!bByproduct)
		{
			Add(ECPBlockerReason::OutputsFull, Row->OutputBlockedBuildings);
		}
	}
	if (Row->MissingInputBuildings > 0)
	{
		Add(ECPBlockerReason::InputsStarved, Row->MissingInputBuildings);
	}
}

/** The full per-part pipeline (ledger → status → ETA → blocker → prospective chain → limiter),
 *  shared by objective items and research unlock costs — a milestone payment is structurally
 *  an objective item. */
FCPPartReport BuildPartReport(const FCPFactorySnapshot& Snapshot, const FCPItemRef& ItemRef, int32 Required, int32 Remaining)
{
	FCPPartReport Part;
	Part.Item = ItemRef;
	Part.Required = Required;
	Part.Remaining = Remaining;

	const FCPOwnedRow* Owned = Snapshot.FindOwned(ItemRef.Name);
	const int64 OwnedTotal = Owned ? Owned->Total() : 0;
	if (Owned)
	{
		Part.OwnedInStorage = Owned->InStorage;
		Part.OwnedInDepot = Owned->InDepot;
		Part.OwnedInPockets = Owned->InPockets;
	}
	Part.Banked = static_cast<int32>(FMath::Min<int64>(OwnedTotal, Remaining));
	Part.StillToProduce = Remaining - Part.Banked;
	Part.bPayable = Part.StillToProduce <= 0;

	Part.Status = Part.bPayable ? ECPNodeStatus::ReadyToDeliver : FCPAnalysis::ClassifyItem(Snapshot, ItemRef.Name);

	if (const FCPProductRow* Row = Snapshot.FindProduct(ItemRef.Name))
	{
		Part.EffectiveRatePerMinute = EffectiveRate(*Row);
		// Banked-aware ETA: banked parts only need delivery, not production time.
		// Machine buffers/belts are invisible to the ledger, so the true ETA is slightly
		// shorter — the estimate errs toward the pleasant surprise.
		if (!Part.bPayable && Part.EffectiveRatePerMinute > 0.01f &&
			Row->AverageProductivityPercent >= NearFullProductivityPercent)
		{
			Part.EtaMinutes = static_cast<float>(Part.StillToProduce) / Part.EffectiveRatePerMinute;
		}
	}

	if (!Part.bPayable)
	{
		CollectLocalIssues(Snapshot, ItemRef.Name, Part.Issues);

		FCPBlocker Blocker;
		if (FCPAnalysis::FindBlocker(Snapshot, ItemRef.Name, Blocker))
		{
			Part.Blocker = Blocker;
			// A found blocker overrides an optimistic classification, but never the
			// benign ones (a saturated objective part is waiting on demand, not broken).
			if (Part.Status == ECPNodeStatus::Fulfilled || Part.Status == ECPNodeStatus::TimeLimited)
			{
				Part.Status = ECPNodeStatus::UnderSupplied;
			}
			// Prospective chain: when nothing produces the blocker item, expand the
			// planned subtree from it down to graft points in the real factory.
			if (Blocker.Reason == ECPBlockerReason::NoProducer)
			{
				TSet<FString> PlanVisited;
				ExpandPlannedChain(Snapshot, Blocker.Item, 0, 0.0f, PlanVisited, Part.PlannedChain);
			}
		}

		// A line that EXISTS but starves is a window, not a wall: expand prospective chains
		// for its unproduced ingredients so they stay in the build order. (Live-caught:
		// placing a dead Supercomputer Manufacturer silently dropped AI Limiter +
		// High-Speed Connector from the tiers — the machine became a "graft" and the
		// NoPower blocker never descended.)
		if (Part.PlannedChain.Num() == 0)
		{
			const FCPProductRow* Row = Snapshot.FindProduct(ItemRef.Name);
			if (Row && Row->MissingInputBuildings > 0)
			{
				if (const FCPRecipeEdge* Edge = Snapshot.FindEdgeProducing(ItemRef.Name))
				{
					for (const FCPItemRate& Ingredient : Edge->Ingredients)
					{
						if (!Snapshot.FindProduct(Ingredient.Item.Name))
						{
							TSet<FString> PlanVisited;
							ExpandPlannedChain(Snapshot, Ingredient.Item, 0, Ingredient.RatePerMinute, PlanVisited, Part.PlannedChain);
						}
					}
				}
			}
		}
	}

	return Part;
}
} // namespace

FCPAnalysisResult FCPAnalysis::Analyze(const FCPFactorySnapshot& Snapshot)
{
	FCPAnalysisResult Result;
	Result.Truncation = Snapshot.Truncation;
	Result.WorldGatheredItemNames = Snapshot.WorldGatheredItemNames;
	Result.bObservedStatsAvailable = Snapshot.bObservedStatsAvailable;
	Result.bHasPlayerContext = Snapshot.bHasPlayerContext;
	Result.bMilestoneSelected = Snapshot.bMilestoneSelected;
	Result.SelectableMilestoneCount = Snapshot.SelectableMilestoneCount;
	Result.ResearchGaps = Snapshot.ResearchGaps;
	for (const FCPObjective& Objective : Snapshot.Objectives)
	{
		FCPObjectiveReport Report;
		Report.ObjectiveName = Objective.Name;
		Report.Kind = Objective.Kind;

		bool bAllCovered = true;
		bool bAnyOutstanding = false;

		for (const FCPObjectiveItem& Item : Objective.Items)
		{
			// Fully delivered parts stay in the report (green "done" rows — the list is a
			// progress ledger, not just a worry list) but skip all supply analysis.
			if (Item.Remaining <= 0)
			{
				FCPPartReport Done;
				Done.Item = Item.Item;
				Done.Required = Item.Required;
				Done.Status = ECPNodeStatus::Delivered;
				Report.Parts.Add(MoveTemp(Done));
				continue;
			}
			bAnyOutstanding = true;

			FCPPartReport Part = BuildPartReport(Snapshot, Item.Item, Item.Required, Item.Remaining);
			bAllCovered &= Part.bPayable;
			Report.Parts.Add(MoveTemp(Part));
		}

		Report.bPayableNow = bAnyOutstanding && bAllCovered;
		if (bAnyOutstanding)
		{
			Result.Objectives.Add(MoveTemp(Report));
		}
	}

	// M2.7b: expand each research gap's unlock costs through the SAME part pipeline — a
	// milestone payment is structurally an objective item (ledger, blocker, planned chain).
	for (const FCPResearchGap& Gap : Snapshot.ResearchGaps)
	{
		if (Gap.UnlockCosts.Num() == 0)
		{
			continue; // selected milestone (live ledger is the milestone objective) or unknown
		}
		FCPResearchGapReport Plan;
		Plan.Gap = Gap;
		for (const FCPObjectiveItem& Cost : Gap.UnlockCosts)
		{
			Plan.CostParts.Add(BuildPartReport(Snapshot, Cost.Item, Cost.Required, Cost.Remaining));
		}
		Result.ResearchPlans.Add(MoveTemp(Plan));
	}

	// Aggregate the build order across every part's planned chain (dedup, deepest tier wins).
	// Three passes: (1) fold planned lines with broken grafts counting 0, (2) derive FIX tiers
	// for broken-but-existing lines from the planned lines that must feed them, (3) refold so
	// dependents of a broken line sort AFTER its fix (build AI Limiter -> fix Supercomputer ->
	// build ADS, not one flat tier).
	{
		TMap<FString, TArray<FCPItemRef>> NeededForByItem;
		const auto FoldAllChains = [&Result, &NeededForByItem](TMap<FString, TPair<int32, FCPItemRef>>& Tiers,
			const TMap<FString, TPair<int32, FCPItemRef>>* FixTiers)
		{
			for (const FCPObjectiveReport& Report : Result.Objectives)
			{
				for (const FCPPartReport& Part : Report.Parts)
				{
					int32 Index = 0;
					while (Index < Part.PlannedChain.Num())
					{
						FoldPlanTiers(Part.PlannedChain, Index, Tiers, FixTiers, &NeededForByItem, &Part.Item);
					}
				}
			}
			// Research payments join the same build order: their missing items need lines too.
			for (const FCPResearchGapReport& Plan : Result.ResearchPlans)
			{
				for (const FCPPartReport& Part : Plan.CostParts)
				{
					int32 Index = 0;
					while (Index < Part.PlannedChain.Num())
					{
						FoldPlanTiers(Part.PlannedChain, Index, Tiers, FixTiers, &NeededForByItem, &Part.Item);
					}
				}
			}
		};

		TMap<FString, TPair<int32, FCPItemRef>> FirstPass;
		FoldAllChains(FirstPass, nullptr);

		// Fix tiers: a broken line's repair waits on the planned lines expanded from its own
		// starving frontier (the top-level planned nodes of ITS part report).
		TMap<FString, TPair<int32, FCPItemRef>> FixTiers;
		const auto ConsiderFix = [&FirstPass, &FixTiers, &Snapshot](const FCPPartReport& Part)
		{
			const FCPProductRow* Row = Snapshot.FindProduct(Part.Item.Name);
			const bool bBrokenLine = Row && Row->ConfiguredBuildings > 0 && Row->ProducingBuildings == 0 &&
				(Row->NoPowerBuildings > 0 || Row->MissingInputBuildings > 0 || Row->PausedBuildings > 0);
			if (!bBrokenLine)
			{
				return;
			}
			int32 MaxDep = 0;
			for (const FCPPlannedNode& Node : Part.PlannedChain)
			{
				if (Node.Depth == 0 && Node.Kind == ECPPlanNodeKind::PlannedLine)
				{
					if (const TPair<int32, FCPItemRef>* Dep = FirstPass.Find(Node.Item.Name))
					{
						MaxDep = FMath::Max(MaxDep, Dep->Key);
					}
				}
			}
			TPair<int32, FCPItemRef>& Entry = FixTiers.FindOrAdd(Part.Item.Name, TPair<int32, FCPItemRef>(0, Part.Item));
			if (MaxDep + 1 > Entry.Key)
			{
				Entry = TPair<int32, FCPItemRef>(MaxDep + 1, Part.Item);
			}
		};
		for (const FCPObjectiveReport& Report : Result.Objectives)
		{
			for (const FCPPartReport& Part : Report.Parts)
			{
				ConsiderFix(Part);
			}
		}
		for (const FCPResearchGapReport& Plan : Result.ResearchPlans)
		{
			for (const FCPPartReport& Part : Plan.CostParts)
			{
				ConsiderFix(Part);
			}
		}

		TMap<FString, TPair<int32, FCPItemRef>> ItemTiers;
		FoldAllChains(ItemTiers, &FixTiers);

		// ---- Research that is a LINK in this plan, not a parallel track ----------------------
		// A locked objective part is normally unplannable: no unlocked recipe makes it, so it has
		// no chain to fold. But when the research that unlocks it is ITSELF one of the objectives
		// being tracked, and every outstanding requirement of that objective is a line this plan
		// already builds, the ordering is fully determined: build those lines, the research
		// completes, the part becomes buildable. Emitting it as a step turns two lists that look
		// unrelated into the single chain it actually is.
		//
		// The guard matters. If any requirement of the gating research is NOT in the plan, we
		// cannot say when it becomes available, and inventing a position would be worse than the
		// honest "not in this plan" note the panel falls back to.
		struct FPendingUnlock
		{
			int32 Tier = 0;
			ECPResearchSource Source = ECPResearchSource::Unknown;
			TArray<FCPItemRef> Unlocks;
		};
		TMap<FString, FPendingUnlock> UnlockTiers;
		TMap<FString, FString> GateForItem;
		TMap<FString, FCPItemRef> LockedItems;

		const auto TierOf = [&ItemTiers, &FixTiers](const FString& Name)
		{
			int32 Best = 0;
			if (const TPair<int32, FCPItemRef>* Found = ItemTiers.Find(Name))
			{
				Best = FMath::Max(Best, Found->Key);
			}
			if (const TPair<int32, FCPItemRef>* Found = FixTiers.Find(Name))
			{
				Best = FMath::Max(Best, Found->Key);
			}
			return Best;
		};

		for (const FCPResearchGap& Gap : Result.ResearchGaps)
		{
			if (Gap.UnlockSchematicName.IsEmpty())
			{
				continue;
			}
			const FCPObjectiveReport* Gate = Result.Objectives.FindByPredicate(
				[&Gap](const FCPObjectiveReport& Objective)
				{
					return Objective.ObjectiveName.Equals(Gap.UnlockSchematicName, ESearchCase::IgnoreCase);
				});
			if (!Gate)
			{
				continue;
			}
			int32 PrereqTier = 0;
			bool bEveryRequirementPlanned = true;
			for (const FCPPartReport& GatePart : Gate->Parts)
			{
				if (GatePart.Status == ECPNodeStatus::Delivered)
				{
					continue;
				}
				const int32 PartTier = TierOf(GatePart.Item.Name);
				if (PartTier <= 0)
				{
					bEveryRequirementPlanned = false;
					break;
				}
				PrereqTier = FMath::Max(PrereqTier, PartTier);
			}
			if (!bEveryRequirementPlanned || PrereqTier <= 0)
			{
				continue;
			}

			FPendingUnlock& Pending = UnlockTiers.FindOrAdd(Gap.UnlockSchematicName);
			Pending.Tier = FMath::Max(Pending.Tier, PrereqTier + 1);
			Pending.Source = Gap.UnlockSource;
			if (!Pending.Unlocks.ContainsByPredicate([&Gap](const FCPItemRef& Existing)
				{ return Existing.Name == Gap.NeededItem.Name; }))
			{
				Pending.Unlocks.Add(Gap.NeededItem);
			}
			GateForItem.Add(Gap.NeededItem.Name, Gap.UnlockSchematicName);
			LockedItems.Add(Gap.NeededItem.Name, Gap.NeededItem);
		}

		// Resolved only after every gap is seen: a schematic's tier can still rise while gaps are
		// being collected, and anything it unlocks must sit above its FINAL position.
		//
		// Then a fixpoint over the locked items themselves. Sitting one tier above the unlock is
		// only a FLOOR: a single milestone commonly unlocks a part and an ingredient of that part
		// (here, Ballistic Warp Drive and the Singularity Cell it consumes five of), so they must
		// be ordered against each other too. Using the captured locked recipes, each item is
		// pushed above every ingredient it needs — locked or already-planned alike. Without this
		// the plan asserts the unlock was the last obstacle, which is the exact over-claim that
		// made "6) Build: Ballistic Warp Drive" wrong.
		TMap<FString, int32> LockedFloor;
		TMap<FString, int32> LockedTier;
		for (const TPair<FString, FString>& Entry : GateForItem)
		{
			if (const FPendingUnlock* Pending = UnlockTiers.Find(Entry.Value))
			{
				LockedFloor.Add(Entry.Key, Pending->Tier + 1);
				LockedTier.Add(Entry.Key, Pending->Tier + 1);
			}
		}
		bool bTierChanged = true;
		for (int32 Pass = 0; bTierChanged && Pass < 64; ++Pass)
		{
			bTierChanged = false;
			for (TPair<FString, int32>& Entry : LockedTier)
			{
				int32 Want = LockedFloor[Entry.Key];
				if (const FCPCatalogRecipe* Recipe = Snapshot.FindCatalogRecipe(Entry.Key))
				{
					for (const FCPItemRate& Ingredient : Recipe->Ingredients)
					{
						const int32* LockedIngredient = LockedTier.Find(Ingredient.Item.Name);
						const int32 IngredientTier = LockedIngredient ? *LockedIngredient : TierOf(Ingredient.Item.Name);
						Want = FMath::Max(Want, IngredientTier + 1);
					}
				}
				if (Want > Entry.Value)
				{
					Entry.Value = Want;
					bTierChanged = true;
				}
			}
		}
		TMap<FString, TPair<int32, FCPItemRef>> UnlockedItemTiers;
		for (const TPair<FString, int32>& Entry : LockedTier)
		{
			UnlockedItemTiers.Add(Entry.Key, TPair<int32, FCPItemRef>(Entry.Value, LockedItems[Entry.Key]));
		}

		int32 MaxTier = 0;
		for (const auto& Entry : ItemTiers)
		{
			MaxTier = FMath::Max(MaxTier, Entry.Value.Key);
		}
		for (const auto& Entry : FixTiers)
		{
			MaxTier = FMath::Max(MaxTier, Entry.Value.Key);
		}
		for (const auto& Entry : UnlockTiers)
		{
			MaxTier = FMath::Max(MaxTier, Entry.Value.Tier);
		}
		for (const auto& Entry : UnlockedItemTiers)
		{
			MaxTier = FMath::Max(MaxTier, Entry.Value.Key);
		}
		Result.BuildTiers.SetNum(MaxTier);
		// An objective's own requirements are chain roots, so nothing lists them as an ingredient.
		// Credited here instead, which is what lets a step say it satisfies the milestone rather
		// than appearing in the plan for no stated reason.
		TMap<FString, TArray<FString>> ObjectivesByItem;
		for (const FCPObjectiveReport& Objective : Result.Objectives)
		{
			for (const FCPPartReport& Part : Objective.Parts)
			{
				if (Part.Status != ECPNodeStatus::Delivered)
				{
					ObjectivesByItem.FindOrAdd(Part.Item.Name).AddUnique(Objective.ObjectiveName);
				}
			}
		}
		const auto MakeStep = [&NeededForByItem, &ObjectivesByItem](const FCPItemRef& Item)
		{
			FCPBuildStep Step;
			Step.Item = Item;
			if (const TArray<FCPItemRef>* Consumers = NeededForByItem.Find(Item.Name))
			{
				Step.NeededFor = *Consumers;
				Step.NeededFor.Sort([](const FCPItemRef& A, const FCPItemRef& B) { return A.Name < B.Name; });
			}
			if (const TArray<FString>* Objectives = ObjectivesByItem.Find(Item.Name))
			{
				Step.NeededForObjectives = *Objectives;
				Step.NeededForObjectives.Sort();
			}
			return Step;
		};
		for (const auto& Entry : ItemTiers)
		{
			Result.BuildTiers[Entry.Value.Key - 1].Items.Add(MakeStep(Entry.Value.Value));
		}
		for (const auto& Entry : FixTiers)
		{
			Result.BuildTiers[Entry.Value.Key - 1].Fixes.Add(MakeStep(Entry.Value.Value));
		}
		for (const auto& Entry : UnlockTiers)
		{
			FCPUnlockStep Step;
			Step.SchematicName = Entry.Key;
			Step.Source = Entry.Value.Source;
			Step.Unlocks = Entry.Value.Unlocks;
			Step.Unlocks.Sort([](const FCPItemRef& A, const FCPItemRef& B) { return A.Name < B.Name; });
			Result.BuildTiers[Entry.Value.Tier - 1].Unlocks.Add(Step);
		}
		// A part that only exists because of an unlock is an ordinary build step once it is
		// reached — the Unlock step above it already carries the explanation.
		for (const auto& Entry : UnlockedItemTiers)
		{
			Result.BuildTiers[Entry.Value.Key - 1].Items.Add(MakeStep(Entry.Value.Value));
		}
		for (FCPBuildTier& Tier : Result.BuildTiers)
		{
			Tier.Items.Sort([](const FCPBuildStep& A, const FCPBuildStep& B) { return A.Item.Name < B.Item.Name; });
			Tier.Fixes.Sort([](const FCPBuildStep& A, const FCPBuildStep& B) { return A.Item.Name < B.Item.Name; });
			Tier.Unlocks.Sort([](const FCPUnlockStep& A, const FCPUnlockStep& B) { return A.SchematicName < B.SchematicName; });
		}
	}

	return Result;
}
