// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPReportMarkdown.h"

namespace
{
const TCHAR* StatusLabel(ECPNodeStatus Status)
{
	switch (Status)
	{
	case ECPNodeStatus::Fulfilled:      return TEXT("fulfilled");
	case ECPNodeStatus::ReadyToDeliver: return TEXT("ready to deliver");
	case ECPNodeStatus::TimeLimited:    return TEXT("time-limited");
	case ECPNodeStatus::UnderSupplied:  return TEXT("under-supplied");
	case ECPNodeStatus::Blocked:        return TEXT("blocked");
	case ECPNodeStatus::RecipeLocked:   return TEXT("recipe locked");
	case ECPNodeStatus::Saturated:      return TEXT("saturated");
	case ECPNodeStatus::StandbyReserve: return TEXT("standby reserve");
	case ECPNodeStatus::Extraction:     return TEXT("extraction");
	default:                            return TEXT("unknown");
	}
}

FString ReasonLabel(const FCPBlocker& Blocker)
{
	switch (Blocker.Reason)
	{
	case ECPBlockerReason::NoProducer: return TEXT("nothing produces it");
	case ECPBlockerReason::WorldGatheredOnly: return TEXT("no recipe makes this - gathered in the world");
	case ECPBlockerReason::NoPower: return TEXT("producers have no power");
	case ECPBlockerReason::SupplyBelowDemand:
		return Blocker.bSolverDerived
			? FString::Printf(TEXT("solved delivery %.0f/min vs configured demand %.0f/min"),
				Blocker.SupplyPerMinute, Blocker.DemandPerMinute)
			: FString::Printf(TEXT("supply %.0f/min vs demand %.0f/min"),
				Blocker.SupplyPerMinute, Blocker.DemandPerMinute);
	case ECPBlockerReason::ExtractionBelowDemand:
		return FString::Printf(TEXT("extraction %.0f/min vs demand %.0f/min"), Blocker.SupplyPerMinute, Blocker.DemandPerMinute);
	case ECPBlockerReason::LogisticsSuspected: return TEXT("legacy logistics suspicion (refresh analysis)");
	case ECPBlockerReason::ProducerNotConnected: return Blocker.bSolverDerived
		? TEXT("producer exists but this consumer domain has no valid delivery route")
		: TEXT("producer exists but is not belt/pipe-connected to a consumer");
	case ECPBlockerReason::ConnectivityUnknown:
		switch (Blocker.UnknownReason)
		{
		case ECPFlowUnknownReason::TransportRateUnknown:
			return TEXT("vehicle route connected; delivery rate unknown because throughput is not measured");
		case ECPFlowUnknownReason::PathAttributionMissing:
			return TEXT("no solved delivery domain could be attributed to this parent machine");
		case ECPFlowUnknownReason::IncompleteOrNonConverged:
			return TEXT("delivery unknown because capture was incomplete or the solver did not converge");
		default:
			return TEXT("starved; connectivity could not be determined completely");
		}
	case ECPBlockerReason::ByproductBackedUp:
		return FString::Printf(TEXT("outputs full; byproduct %s may be backing up"), *Blocker.Byproduct.Name);
	case ECPBlockerReason::InputsStarved:
		return FString::Printf(TEXT("no inputs arriving (%d of %d machines starved)"), Blocker.AffectedBuildings, Blocker.TotalBuildings);
	case ECPBlockerReason::OutputsFull:
		return FString::Printf(TEXT("outputs full (%d of %d machines stalled)"), Blocker.AffectedBuildings, Blocker.TotalBuildings);
	default: return FString();
	}
}

FString LimiterRate(float RatePerMinute, bool bFluid)
{
	return bFluid
		? FString::Printf(TEXT("%.2f m³/min"), RatePerMinute)
		: FString::Printf(TEXT("%.0f items/min"), RatePerMinute);
}

FString LimiterConstraintLabel(const FCPLimiter& Limiter)
{
	const bool bFluid = Limiter.Item.Form == ECPItemForm::Fluid;
	switch (Limiter.ConstraintKind)
	{
	case ECPFlowConstraintKind::InstalledCapacity:
		return FString::Printf(TEXT("machine capacity: %s installed for %s demand"),
			*LimiterRate(Limiter.InstalledPerMinute, bFluid),
			*LimiterRate(Limiter.DemandPerMinute, bFluid));
	case ECPFlowConstraintKind::InputSupport:
		return FString::Printf(TEXT("input support: %s sustainable of %s installed"),
			*LimiterRate(Limiter.SustainablePerMinute, bFluid),
			*LimiterRate(Limiter.InstalledPerMinute, bFluid));
	case ECPFlowConstraintKind::DeliveryEdge:
		if (Limiter.EdgeEvidence.IsSet())
		{
			return FString::Printf(TEXT("%s bottleneck: %s -> %s carries %s of %s"),
				Limiter.EdgeEvidence.bFluid ? TEXT("pipe") : TEXT("belt"),
				Limiter.EdgeEvidence.FromLabel.IsEmpty() ? TEXT("upstream node") : *Limiter.EdgeEvidence.FromLabel,
				Limiter.EdgeEvidence.ToLabel.IsEmpty() ? TEXT("downstream node") : *Limiter.EdgeEvidence.ToLabel,
				*LimiterRate(Limiter.EdgeEvidence.TotalFlowPerMinute, Limiter.EdgeEvidence.bFluid),
				*LimiterRate(Limiter.EdgeEvidence.CapacityPerMinute, Limiter.EdgeEvidence.bFluid));
		}
		return FString();
	case ECPFlowConstraintKind::DeliveryRouting:
		return FString::Printf(TEXT("delivery/routing: %s sustainable, only %s delivered; no saturated edge proven"),
			*LimiterRate(Limiter.SustainablePerMinute, bFluid),
			*LimiterRate(Limiter.SupplyPerMinute, bFluid));
	case ECPFlowConstraintKind::None:
	default:
		return FString();
	}
}

FString LimiterMarginalLabel(const FCPLimiter& Limiter)
{
	if (!Limiter.bMarginalSolverDerived || Limiter.MarginalRatio < 0.0f)
	{
		return FString();
	}
	const bool bFluid = Limiter.Item.Form == ECPItemForm::Fluid;
	if (Limiter.MarginalAction == ECPMarginalActionKind::AddAverageMachine)
	{
		return FString::Printf(TEXT("+1 average %s exact re-solve: %s delivered (%.0f%%)"),
			Limiter.MachineName.IsEmpty() ? TEXT("machine") : *Limiter.MachineName,
			*LimiterRate(Limiter.MarginalDeliveredPerMinute, bFluid), Limiter.MarginalRatio * 100.0f);
	}
	if (Limiter.MarginalAction == ECPMarginalActionKind::UpgradeEdge)
	{
		return FString::Printf(TEXT("upgrade this %s to %s exact re-solve: %s delivered (%.0f%%)"),
			Limiter.EdgeEvidence.bFluid ? TEXT("pipe") : TEXT("belt"),
			*LimiterRate(Limiter.MarginalEdgeCapacityPerMinute, Limiter.EdgeEvidence.bFluid),
			*LimiterRate(Limiter.MarginalDeliveredPerMinute, bFluid), Limiter.MarginalRatio * 100.0f);
	}
	return FString();
}

/** Mermaid node ids must be bare words. */
FString MermaidId(const FString& Name)
{
	FString Id;
	for (TCHAR C : Name)
	{
		Id.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
	}
	return Id;
}
}

FString FCPReportMarkdown::Build(const FCPAnalysisResult& Result, const FString& CapturedAtIso)
{
	FString Md;
	Md += TEXT("# Critical Path Report\n\n");
	Md += FString::Printf(TEXT("_Captured: %s_\n\n"), *CapturedAtIso);
	// bAnyCapHit is set by SEVEN different caps (buildings, extractors, connectivity, states,
	// recipe edges, storage containers, catalog recipes). Reporting building counts whenever ANY
	// of them trips produced the nonsense line "8808 of 8808 buildings scanned - too many to
	// scan": the building scan finished, a different cap tripped, and the player was told their
	// factory was too big. Only claim a partial building scan when the BUILDING scan is short.
	if (Result.Truncation.BuildingsScanned < Result.Truncation.BuildingsAvailable)
	{
		Md += FString::Printf(TEXT("> **Partial scan**: %d of %d buildings scanned - treat totals as lower bounds.\n\n"),
			Result.Truncation.BuildingsScanned, Result.Truncation.BuildingsAvailable);
	}
	else if (Result.Truncation.bAnyCapHit)
	{
		Md += TEXT("> **Partial detail**: every building was scanned, but some supporting detail ")
			TEXT("(storage, recipe or connectivity breadth) hit a collection limit - treat totals as lower bounds.\n\n");
	}

	bool bAnyChain = false;
	for (const FCPObjectiveReport& Objective : Result.Objectives)
	{
		Md += FString::Printf(TEXT("## %s: %s\n\n"),
			Objective.Kind == ECPObjectiveKind::SpaceElevatorPhase ? TEXT("Elevator") : TEXT("Milestone"),
			*Objective.ObjectiveName);
		if (Objective.bPayableNow)
		{
			Md += TEXT("**Payable now** - every remaining part is banked.\n\n");
		}
		Md += TEXT("| Part | Needed | Banked | To produce | Status |\n|---|---:|---:|---:|---|\n");
		for (const FCPPartReport& Part : Objective.Parts)
		{
			Md += FString::Printf(TEXT("| %s | %d | %d | %d | %s |\n"),
				*Part.Item.Name, Part.Remaining, Part.Banked, Part.StillToProduce, StatusLabel(Part.Status));
		}
		Md += TEXT("\n");

		for (const FCPPartReport& Part : Objective.Parts)
		{
			if (Part.Blocker.Reason == ECPBlockerReason::None)
			{
				continue;
			}
			bAnyChain = true;
			Md += FString::Printf(TEXT("- **%s** blocked at **%s**: %s\n"),
				*Part.Item.Name, *Part.Blocker.Item.Name, *ReasonLabel(Part.Blocker));
			if (Part.Blocker.Path.Num() > 1)
			{
				FString Chain;
				for (const FCPItemRef& Node : Part.Blocker.Path)
				{
					if (!Chain.IsEmpty())
					{
						Chain += TEXT(" <- ");
					}
					Chain += Node.Name;
				}
				Md += FString::Printf(TEXT("  - chain: %s\n"), *Chain);
			}
			if (Part.Limiter.IsSet() && !Part.Limiter.Item.Name.Equals(Part.Blocker.Item.Name, ESearchCase::IgnoreCase))
			{
				const FString Constraint = LimiterConstraintLabel(Part.Limiter);
				const FString Marginal = LimiterMarginalLabel(Part.Limiter);
				Md += FString::Printf(TEXT("  - limiter: **%s** - %.0f/min vs %.0f/min (%.0f%%, %s)%s%s%s%s\n"),
					*Part.Limiter.Item.Name, Part.Limiter.SupplyPerMinute, Part.Limiter.DemandPerMinute,
					Part.Limiter.Ratio * 100.0f,
					Part.Limiter.bSolverDerived ? TEXT("solved domain") :
						(Part.Limiter.bNetworkScoped ? TEXT("limiting network") : TEXT("aggregate")),
					Part.Limiter.BufferMinutes >= 0.0f
						? (Part.Limiter.BufferMinutes < 1.0f ? TEXT(" - stored buffer dry")
							: *FString::Printf(TEXT(" - stored buffer ~%.0f min"), Part.Limiter.BufferMinutes))
						: TEXT(""),
					Part.Limiter.UnknownLinks > 0 ? *FString::Printf(TEXT(" - %d link(s) unknown"), Part.Limiter.UnknownLinks) : TEXT(""),
					Constraint.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" - %s"), *Constraint),
					Marginal.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" - %s"), *Marginal));
			}
			for (const FCPPlannedNode& Node : Part.PlannedChain)
			{
				FString Indent;
				for (int32 D = 0; D < Node.Depth; ++D)
				{
					Indent += TEXT("  ");
				}
				const FString Kind =
					Node.Kind == ECPPlanNodeKind::PlannedLine ? FString(TEXT("no line - build")) :
					Node.Kind == ECPPlanNodeKind::ExistingProduction ? FString(Node.bLineStopped ? TEXT("line exists but STOPPED") : TEXT("already produced")) :
					Node.Kind == ECPPlanNodeKind::RawResource ? FString(TEXT("raw - extract")) :
					Node.Kind == ECPPlanNodeKind::ResearchLocked
						? (Node.ByproductOfFuel.IsEmpty()
							? FString(TEXT("research locked"))
							: FString::Printf(TEXT("spent fuel: burn %s"), *Node.ByproductOfFuel))
						: FString(TEXT("unknown"));
				FString Sufficiency;
				if (Node.Kind == ECPPlanNodeKind::ExistingProduction)
				{
					Sufficiency = Node.bSufficiencyKnown
						? FString::Printf(TEXT(", spare %.2f/min vs %.2f/min required (%.0f%%)"),
							Node.AvailableHeadroomPerMinute, Node.RequiredPerMinute, Node.SufficiencyRatio * 100.0f)
						: TEXT(", spare capacity unavailable: one or more routes could not be fully measured");
				}
				Md += FString::Printf(TEXT("  - plan: %s%s (%s%s%s%s)%s\n"), *Indent, *Node.Item.Name, *Kind,
					Node.MachineName.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s"), *Node.MachineName),
					Node.bAlternatesExist ? TEXT(", alts exist") : TEXT(""),
					*Sufficiency,
					Node.bTruncated ? TEXT(" …") : TEXT(""));
			}
		}
		Md += TEXT("\n");
	}

	if (Result.BuildTiers.Num() > 0)
	{
		Md += TEXT("## Build order (aggregated across all parts)\n\n");
		for (int32 TierIndex = 0; TierIndex < Result.BuildTiers.Num(); ++TierIndex)
		{
			FString Names;
			// "needed for" turns a list of names into a chain the reader can follow.
			const auto AppendNeededFor = [](FString& Out, const FCPBuildStep& Step)
			{
				if (Step.NeededFor.Num() == 0)
				{
					return;
				}
				FString Consumers;
				for (const FCPItemRef& Consumer : Step.NeededFor)
				{
					if (!Consumers.IsEmpty()) { Consumers += TEXT(", "); }
					Consumers += Consumer.Name;
				}
				Out += FString::Printf(TEXT(" (needed for %s)"), *Consumers);
			};
			for (const FCPBuildStep& Step : Result.BuildTiers[TierIndex].Fixes)
			{
				if (!Names.IsEmpty())
				{
					Names += TEXT(", ");
				}
				Names += FString::Printf(TEXT("fix %s"), *Step.Item.Name);
				AppendNeededFor(Names, Step);
			}
			for (const FCPBuildStep& Step : Result.BuildTiers[TierIndex].Items)
			{
				if (!Names.IsEmpty())
				{
					Names += TEXT(", ");
				}
				Names += Step.Item.Name;
				AppendNeededFor(Names, Step);
			}
			Md += FString::Printf(TEXT("%d. %s\n"), TierIndex + 1, *Names);
		}
		Md += TEXT("\nTier 1 lines can be fed from existing production today; each later tier waits on the previous one.\n\n");
	}

	if (Result.ResearchPlans.Num() > 0)
	{
		Md += TEXT("## Research payment plans\n\n");
		for (const FCPResearchGapReport& Plan : Result.ResearchPlans)
		{
			Md += FString::Printf(TEXT("### %s (unlocks %s)\n\n| Cost | Needed | Banked | Status |\n|---|---:|---:|---|\n"),
				*Plan.Gap.UnlockSchematicName, *Plan.Gap.NeededItem.Name);
			for (const FCPPartReport& Cost : Plan.CostParts)
			{
				Md += FString::Printf(TEXT("| %s | %d | %d | %s |\n"),
					*Cost.Item.Name, Cost.Remaining, Cost.Banked, StatusLabel(Cost.Status));
			}
			Md += TEXT("\n");
		}
	}

	if (Result.ResearchGaps.Num() > 0)
	{
		Md += TEXT("## Research gaps\n\n");
		for (const FCPResearchGap& Gap : Result.ResearchGaps)
		{
			Md += !Gap.ByproductOfFuel.IsEmpty()
				? FString::Printf(TEXT("- %s: no recipe by design - spent fuel, burn **%s** in a Nuclear Power Plant\n"), *Gap.NeededItem.Name, *Gap.ByproductOfFuel)
				: Gap.UnlockSchematicName.IsEmpty()
					? FString::Printf(TEXT("- %s: recipe locked, no purchasable research yet\n"), *Gap.NeededItem.Name)
					: FString::Printf(TEXT("- %s: research **%s**\n"), *Gap.NeededItem.Name, *Gap.UnlockSchematicName);
		}
		Md += TEXT("\n");
	}

	if (bAnyChain)
	{
		Md += TEXT("## Blocker graph\n\n```mermaid\ngraph RL\n");
		TSet<FString> Emitted;
		for (const FCPObjectiveReport& Objective : Result.Objectives)
		{
			for (const FCPPartReport& Part : Objective.Parts)
			{
				for (int32 Index = 0; Index + 1 < Part.Blocker.Path.Num(); ++Index)
				{
					const FString& From = Part.Blocker.Path[Index].Name;
					const FString& To = Part.Blocker.Path[Index + 1].Name;
					const FString EdgeKey = From + TEXT("->") + To;
					if (!Emitted.Contains(EdgeKey))
					{
						Emitted.Add(EdgeKey);
						Md += FString::Printf(TEXT("  %s[\"%s\"] --> %s[\"%s\"]\n"),
							*MermaidId(To), *To, *MermaidId(From), *From);
					}
				}
			}
		}
		Md += TEXT("```\n");
	}

	return Md;
}
