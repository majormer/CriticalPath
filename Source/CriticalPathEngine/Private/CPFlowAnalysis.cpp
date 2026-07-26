// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPFlowAnalysis.h"
#include "CPFlowSolver.h"

namespace
{
const TArray<FCPItemRate>& BasisRates(const FCPFlowNode& Node, bool bDesign)
{
	return bDesign && Node.DesignRates.Num() > 0 ? Node.DesignRates : Node.Rates;
}

const FCPItemRate* FindRate(const TArray<FCPItemRate>& Rates, const FString& ItemName)
{
	return Rates.FindByPredicate([&ItemName](const FCPItemRate& Rate)
	{
		return Rate.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase);
	});
}

float StockAmount(const FCPFlowNode& Node, const FString& ItemName)
{
	float Total = 0.0f;
	for (const FCPBufferedItem& Stock : Node.Stocks)
	{
		if (Stock.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase))
		{
			Total += Stock.Amount;
		}
	}
	return Total;
}

bool SplitterAdmits(const FCPFlowGraph& Graph, const FCPFlowEdge& Edge, const FString& ItemName)
{
	if (!Graph.Nodes.IsValidIndex(Edge.FromNode))
	{
		return false;
	}
	const FCPFlowNode& Source = Graph.Nodes[Edge.FromNode];
	if (Source.Kind != ECPFlowNodeKind::Splitter || !Source.OutRules.IsValidIndex(Edge.FromPortIndex))
	{
		return true;
	}
	const FCPSplitterRule& Rule = Source.OutRules[Edge.FromPortIndex];
	switch (Rule.Rule)
	{
	case ECPSplitterOutRule::None:
		return false;
	case ECPSplitterOutRule::Filtered:
		return Rule.Items.ContainsByPredicate([&ItemName](const FString& Candidate)
		{
			return Candidate.Equals(ItemName, ESearchCase::IgnoreCase);
		});
	case ECPSplitterOutRule::AnyUndefined:
		for (const FCPSplitterRule& Sibling : Source.OutRules)
		{
			if (Sibling.Rule == ECPSplitterOutRule::Filtered &&
				Sibling.Items.ContainsByPredicate([&ItemName](const FString& Candidate)
				{
					return Candidate.Equals(ItemName, ESearchCase::IgnoreCase);
				}))
			{
				return false;
			}
		}
		return true;
	case ECPSplitterOutRule::Any:
	case ECPSplitterOutRule::Overflow:
	default:
		return true;
	}
}

float ResultRate(const TArray<FCPEdgeItemFlow>& Rows, int32 NodeIndex, const FString& ItemName)
{
	float Total = 0.0f;
	for (const FCPEdgeItemFlow& Row : Rows)
	{
		if (Row.EdgeIndex == NodeIndex && Row.ItemName.Equals(ItemName, ESearchCase::IgnoreCase))
		{
			Total += Row.RatePerMinute;
		}
	}
	return Total;
}

float TotalResultRate(const TArray<FCPEdgeItemFlow>& Rows, int32 Index)
{
	float Total = 0.0f;
	for (const FCPEdgeItemFlow& Row : Rows)
	{
		if (Row.EdgeIndex == Index)
		{
			Total += Row.RatePerMinute;
		}
	}
	return Total;
}

float DeliveredRate(const FCPFlowSolveResult& Solve, int32 NodeIndex, const FString& ItemName)
{
	for (const FCPConsumerDelivery& Row : Solve.Deliveries)
	{
		if (Row.NodeIndex == NodeIndex && Row.ItemName.Equals(ItemName, ESearchCase::IgnoreCase))
		{
			return Row.DeliveredPerMinute;
		}
	}
	return 0.0f;
}
} // namespace

void FCPFlowAnalysis::BuildItemBalances(
	const FCPFlowGraph& Graph,
	const FCPFlowSolveResult& CurrentSolve,
	const FCPFlowSolveResult& DesignSolve,
	TArray<FCPItemBalance>& OutBalances)
{
	OutBalances.Reset();
	if (Graph.Nodes.Num() == 0)
	{
		return;
	}

	TMap<FString, FCPItemRef> Items;
	const auto RememberItem = [&Items](const FCPItemRef& Item)
	{
		FCPItemRef& Stored = Items.FindOrAdd(Item.Name);
		if (!Stored.IsValid() || Item.Form != ECPItemForm::Unknown)
		{
			Stored = Item;
		}
	};
	for (const FCPFlowNode& Node : Graph.Nodes)
	{
		for (const FCPItemRate& Rate : Node.Rates)
		{
			RememberItem(Rate.Item);
		}
		for (const FCPItemRate& Rate : Node.DesignRates)
		{
			RememberItem(Rate.Item);
		}
		for (const FCPBufferedItem& Stock : Node.Stocks)
		{
			RememberItem(Stock.Item);
		}
	}

	for (const TPair<FString, FCPItemRef>& ItemPair : Items)
	{
		const FString& ItemName = ItemPair.Key;
		bool bFluid = ItemPair.Value.Form == ECPItemForm::Fluid;
		bool bFormKnown = ItemPair.Value.Form != ECPItemForm::Unknown;
		// Backward-compatible fallback for synthetic or third-party graphs created before item
		// form became explicit. Live capture always uses descriptor-backed form metadata.
		for (int32 EdgeIndex = 0; EdgeIndex < Graph.Edges.Num() && !bFormKnown; ++EdgeIndex)
		{
			const FCPFlowEdge& Edge = Graph.Edges[EdgeIndex];
			if (!Graph.Nodes.IsValidIndex(Edge.FromNode) || !Graph.Nodes.IsValidIndex(Edge.ToNode))
			{
				continue;
			}
			const bool bEndpointCarriesItem =
				FindRate(Graph.Nodes[Edge.FromNode].Rates, ItemName) ||
				FindRate(Graph.Nodes[Edge.FromNode].DesignRates, ItemName) ||
				StockAmount(Graph.Nodes[Edge.FromNode], ItemName) > 0.0f ||
				FindRate(Graph.Nodes[Edge.ToNode].Rates, ItemName) ||
				FindRate(Graph.Nodes[Edge.ToNode].DesignRates, ItemName) ||
				StockAmount(Graph.Nodes[Edge.ToNode], ItemName) > 0.0f;
			if (bEndpointCarriesItem)
			{
				bFluid = Edge.bFluid;
				bFormKnown = true;
			}
		}

		TArray<int32> Parent;
		Parent.SetNum(Graph.Nodes.Num());
		for (int32 NodeIndex = 0; NodeIndex < Parent.Num(); ++NodeIndex)
		{
			Parent[NodeIndex] = NodeIndex;
		}
		const auto FindRoot = [&Parent](int32 Node)
		{
			while (Parent[Node] != Node)
			{
				Parent[Node] = Parent[Parent[Node]];
				Node = Parent[Node];
			}
			return Node;
		};
		for (const FCPFlowEdge& Edge : Graph.Edges)
		{
			if (Edge.bBlocked || !Graph.Nodes.IsValidIndex(Edge.FromNode) || !Graph.Nodes.IsValidIndex(Edge.ToNode) ||
				(!Edge.bTransport && bFormKnown && Edge.bFluid != bFluid) || !SplitterAdmits(Graph, Edge, ItemName))
			{
				continue;
			}
			const int32 FromRoot = FindRoot(Edge.FromNode);
			const int32 ToRoot = FindRoot(Edge.ToNode);
			if (FromRoot != ToRoot)
			{
				Parent[FromRoot] = ToRoot;
			}
		}

		TMap<int32, FCPItemBalance> ByDomain;
		for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
		{
			const FCPFlowNode& Node = Graph.Nodes[NodeIndex];
			const FCPItemRate* CurrentRate = FindRate(BasisRates(Node, false), ItemName);
			const FCPItemRate* DesignRate = FindRate(BasisRates(Node, true), ItemName);
			const float Buffered = StockAmount(Node, ItemName);
			if (!CurrentRate && !DesignRate && Buffered <= 0.0f)
			{
				continue;
			}
			const int32 Domain = FindRoot(NodeIndex);
			FCPItemBalance& Balance = ByDomain.FindOrAdd(Domain);
			Balance.Item = ItemPair.Value;
			Balance.DomainId = Domain;
			Balance.bFluid = bFormKnown && bFluid;
			Balance.Current.bKnown = CurrentSolve.bConverged && !Graph.bTruncated;
			Balance.Design.bKnown = DesignSolve.bConverged && !Graph.bTruncated;
			Balance.BufferedAmount += Buffered;

			if (Node.Kind == ECPFlowNodeKind::Producer)
			{
				Balance.ProducerBuildings++;
				if (Node.bMachineStateKnown)
				{
					Balance.MachineStateProducerBuildings++;
					Balance.ProducingProducerBuildings += Node.bProducing ? 1 : 0;
					Balance.NoPowerProducerBuildings += Node.bNoPower ? 1 : 0;
					Balance.PausedProducerBuildings += Node.bPaused ? 1 : 0;
					Balance.MissingInputProducerBuildings += Node.bMissingInput ? 1 : 0;
					if (!Node.JammedByItemName.IsEmpty())
					{
						Balance.JammedProducerBuildings++;
						if (Balance.JammedByItemName.IsEmpty()) { Balance.JammedByItemName = Node.JammedByItemName; }
					}
					Balance.OutputBlockedProducerBuildings += Node.bOutputBlocked ? 1 : 0;
					const float Capacity = DesignRate ? DesignRate->RatePerMinute :
						(CurrentRate ? CurrentRate->RatePerMinute : 0.0f);
					Balance.EstimatedCurrentOutputPerMinute += Capacity *
						FMath::Clamp(Node.ProductivityPercent / 100.0f, 0.0f, 1.0f);
				}
				FCPBalanceLocation& Location = Balance.Locations.AddDefaulted_GetRef();
				Location.Label = Node.Label;
				Location.ActorName = Node.ActorName;
				Location.Location = Node.Location;
				Location.BuildingItem = Node.BuildingItem;
				Location.bProducer = true;
				Location.bMachineStateKnown = Node.bMachineStateKnown;
				Location.bProducing = Node.bProducing;
				Location.bNoPower = Node.bNoPower;
				Location.bPaused = Node.bPaused;
				Location.bMissingInput = Node.bMissingInput;
				Location.JammedByItemName = Node.JammedByItemName;
				Location.StarvedOfItemName = Node.StarvedOfItemName;
				Location.bOutputBlocked = Node.bOutputBlocked;
				Location.ProductivityPercent = Node.ProductivityPercent;
				Location.EstimatedRatePerMinute = Node.bMachineStateKnown
					? (DesignRate ? DesignRate->RatePerMinute : (CurrentRate ? CurrentRate->RatePerMinute : 0.0f)) *
						FMath::Clamp(Node.ProductivityPercent / 100.0f, 0.0f, 1.0f)
					: 0.0f;
				Location.BufferedAmount = Buffered;
				Location.PowerShardCount = Node.PowerShardCount;
				Location.SomersloopCount = Node.SomersloopCount;
				Location.PowerShardItem = Node.PowerShardItem;
				Location.SomersloopItem = Node.SomersloopItem;
				// Installed capacity is what the placed/configured machines could make; unlike
				// sustainable output it does not disappear when power or inputs disappear.
				Balance.Current.InstalledPerMinute += DesignRate ? DesignRate->RatePerMinute :
					(CurrentRate ? CurrentRate->RatePerMinute : 0.0f);
				Balance.Design.InstalledPerMinute += DesignRate ? DesignRate->RatePerMinute : 0.0f;
				if (Balance.Current.bKnown)
				{
					Balance.Current.SustainablePerMinute += ResultRate(CurrentSolve.ProducerSustainable, NodeIndex, ItemName);
				}
				if (Balance.Design.bKnown)
				{
					Balance.Design.SustainablePerMinute += ResultRate(DesignSolve.ProducerSustainable, NodeIndex, ItemName);
				}
			}
			else if (Node.Kind == ECPFlowNodeKind::Consumer)
			{
				Balance.ConsumerBuildings++;
				if (Node.bMachineStateKnown)
				{
					Balance.MachineStateConsumerBuildings++;
					Balance.ProducingConsumerBuildings += Node.bProducing ? 1 : 0;
					Balance.MissingInputConsumerBuildings += Node.bMissingInput ? 1 : 0;
					const float StateRate = Node.bProducing
						? (CurrentRate ? CurrentRate->RatePerMinute : 0.0f) *
							FMath::Clamp(Node.ProductivityPercent / 100.0f, 0.0f, 1.0f)
						: 0.0f;
					Balance.EstimatedCurrentUsePerMinute += StateRate;
				}
				FCPBalanceLocation& Location = Balance.Locations.AddDefaulted_GetRef();
				Location.Label = Node.Label;
				Location.ActorName = Node.ActorName;
				Location.Location = Node.Location;
				Location.BuildingItem = Node.BuildingItem;
				Location.bProducer = false;
				Location.bMachineStateKnown = Node.bMachineStateKnown;
				Location.bProducing = Node.bProducing;
				Location.bNoPower = Node.bNoPower;
				Location.bPaused = Node.bPaused;
				Location.bMissingInput = Node.bMissingInput;
				Location.JammedByItemName = Node.JammedByItemName;
				Location.StarvedOfItemName = Node.StarvedOfItemName;
				Location.bOutputBlocked = Node.bOutputBlocked;
				Location.ProductivityPercent = Node.ProductivityPercent;
				Location.EstimatedRatePerMinute = Node.bProducing
					? (CurrentRate ? CurrentRate->RatePerMinute : 0.0f) *
						FMath::Clamp(Node.ProductivityPercent / 100.0f, 0.0f, 1.0f)
					: 0.0f;
				Location.BufferedAmount = Buffered;
				Location.PowerShardCount = Node.PowerShardCount;
				Location.SomersloopCount = Node.SomersloopCount;
				Location.PowerShardItem = Node.PowerShardItem;
				Location.SomersloopItem = Node.SomersloopItem;
				Balance.Current.DemandPerMinute += CurrentRate ? CurrentRate->RatePerMinute : 0.0f;
				Balance.Design.DemandPerMinute += DesignRate ? DesignRate->RatePerMinute : 0.0f;
				if (Balance.Current.bKnown)
				{
					Balance.Current.DeliveredPerMinute += DeliveredRate(CurrentSolve, NodeIndex, ItemName);
				}
				if (Balance.Design.bKnown)
				{
					Balance.Design.DeliveredPerMinute += DeliveredRate(DesignSolve, NodeIndex, ItemName);
				}
			}
		}

		for (const FCPFlowEdge& Edge : Graph.Edges)
		{
			if (!Edge.bTransport || Edge.bBlocked ||
				!Graph.Nodes.IsValidIndex(Edge.FromNode) || !SplitterAdmits(Graph, Edge, ItemName))
			{
				continue;
			}
			if (FCPItemBalance* Balance = ByDomain.Find(FindRoot(Edge.FromNode)))
			{
				Balance->bUsesTransport = true;
				Balance->bTransportRateKnown = false; // Phase T edges are topology-only today.
			}
		}

		for (TPair<int32, FCPItemBalance>& DomainPair : ByDomain)
		{
			FCPItemBalance& Balance = DomainPair.Value;
			if (Balance.Design.bKnown && Balance.Design.DemandPerMinute > KINDA_SMALL_NUMBER)
			{
				const float Demand = Balance.Design.DemandPerMinute;
				const float CapacityRatio = Balance.Design.InstalledPerMinute / Demand;
				const float SupportRatio = Balance.Design.InstalledPerMinute > KINDA_SMALL_NUMBER
					? Balance.Design.SustainablePerMinute / Balance.Design.InstalledPerMinute : 1.0f;
				const float DeliveryRatio = Balance.Design.SustainablePerMinute > KINDA_SMALL_NUMBER
					? Balance.Design.DeliveredPerMinute / Balance.Design.SustainablePerMinute : 1.0f;
				float WorstStageRatio = 1.0f;
				if (CapacityRatio < 0.995f)
				{
					Balance.DesignConstraintKind = ECPFlowConstraintKind::InstalledCapacity;
					WorstStageRatio = CapacityRatio;
				}
				if (SupportRatio < 0.995f && SupportRatio < WorstStageRatio)
				{
					Balance.DesignConstraintKind = ECPFlowConstraintKind::InputSupport;
					WorstStageRatio = SupportRatio;
				}
				if (DeliveryRatio < 0.995f && DeliveryRatio < WorstStageRatio)
				{
					Balance.DesignConstraintKind = ECPFlowConstraintKind::DeliveryRouting;
					float BestUtilization = -1.0f;
					for (int32 EdgeIndex = 0; EdgeIndex < Graph.Edges.Num(); ++EdgeIndex)
					{
						const FCPFlowEdge& Edge = Graph.Edges[EdgeIndex];
						if (Edge.bBlocked || Edge.bTransport || Edge.CapacityPerMinute <= KINDA_SMALL_NUMBER ||
							Edge.CapacityPerMinute >= 1.0e8f || !Graph.Nodes.IsValidIndex(Edge.FromNode) ||
							!Graph.Nodes.IsValidIndex(Edge.ToNode) || FindRoot(Edge.FromNode) != DomainPair.Key ||
							(bFormKnown && Edge.bFluid != bFluid) || !SplitterAdmits(Graph, Edge, ItemName))
						{
							continue;
						}
						const float ItemFlow = ResultRate(DesignSolve.EdgeFlows, EdgeIndex, ItemName);
						if (ItemFlow <= KINDA_SMALL_NUMBER)
						{
							continue;
						}
						const float TotalFlow = TotalResultRate(DesignSolve.EdgeFlows, EdgeIndex);
						const float Utilization = TotalFlow / Edge.CapacityPerMinute;
						if (Utilization + 0.005f < 1.0f || Utilization <= BestUtilization)
						{
							continue;
						}
						BestUtilization = Utilization;
						Balance.DesignConstraintKind = ECPFlowConstraintKind::DeliveryEdge;
						Balance.DesignEdgeEvidence.EdgeIndex = EdgeIndex;
						Balance.DesignEdgeEvidence.FromLabel = Graph.Nodes[Edge.FromNode].Label;
						Balance.DesignEdgeEvidence.ToLabel = Graph.Nodes[Edge.ToNode].Label;
						Balance.DesignEdgeEvidence.Location = !Graph.Nodes[Edge.ToNode].Location.IsNearlyZero()
							? Graph.Nodes[Edge.ToNode].Location : Graph.Nodes[Edge.FromNode].Location;
						Balance.DesignEdgeEvidence.ItemFlowPerMinute = ItemFlow;
						Balance.DesignEdgeEvidence.TotalFlowPerMinute = TotalFlow;
						Balance.DesignEdgeEvidence.CapacityPerMinute = Edge.CapacityPerMinute;
						Balance.DesignEdgeEvidence.bFluid = Edge.bFluid;
					}
				}
			}
			Balance.Locations.Sort([](const FCPBalanceLocation& A, const FCPBalanceLocation& B)
			{
				if (A.bProducer != B.bProducer)
				{
					return !A.bProducer; // consumer-side anchors first: that is where a shortfall is felt
				}
				return A.ActorName < B.ActorName;
			});
			const float Deficit = Balance.Current.DemandPerMinute - Balance.Current.DeliveredPerMinute;
			if (Balance.Current.bKnown && Deficit > KINDA_SMALL_NUMBER)
			{
				Balance.BufferRunwayMinutes = Balance.BufferedAmount / Deficit;
			}
			OutBalances.Add(MoveTemp(DomainPair.Value));
		}
	}

	OutBalances.Sort([](const FCPItemBalance& A, const FCPItemBalance& B)
	{
		const int32 NameOrder = A.Item.Name.Compare(B.Item.Name, ESearchCase::IgnoreCase);
		return NameOrder == 0 ? A.DomainId < B.DomainId : NameOrder < 0;
	});
}

void FCPFlowAnalysis::BuildSolverSufficiency(
	const TArray<FCPItemBalance>& Balances,
	TArray<FCPItemSufficiency>& OutSufficiency)
{
	OutSufficiency.Reset();
	TMap<FString, TArray<const FCPItemBalance*>> ByItem;
	for (const FCPItemBalance& Balance : Balances)
	{
		if (Balance.Current.DemandPerMinute > KINDA_SMALL_NUMBER ||
			Balance.Design.DemandPerMinute > KINDA_SMALL_NUMBER)
		{
			ByItem.FindOrAdd(Balance.Item.Name).Add(&Balance);
		}
	}

	const auto AccumulateBasis = [](const FCPItemBalanceBasis& Source, bool bDomainKnown,
		FCPSufficiencyBasis& Target)
	{
		Target.DemandPerMinute += Source.DemandPerMinute;
		Target.InstalledPerMinute += Source.InstalledPerMinute;
		Target.SustainablePerMinute += Source.SustainablePerMinute;
		Target.DeliveredPerMinute += Source.DeliveredPerMinute;
		if (!bDomainKnown || Source.DemandPerMinute <= KINDA_SMALL_NUMBER)
		{
			Target.UnknownDomainCount += Source.DemandPerMinute > KINDA_SMALL_NUMBER ? 1 : 0;
			return;
		}
		Target.KnownDomainCount++;
		const float Ratio = Source.DeliveredPerMinute / Source.DemandPerMinute;
		if (Target.LimitingDomainRatio < 0.0f || Ratio < Target.LimitingDomainRatio)
		{
			Target.LimitingDomainRatio = Ratio;
			Target.LimitingDomainDeliveredPerMinute = Source.DeliveredPerMinute;
			Target.LimitingDomainDemandPerMinute = Source.DemandPerMinute;
		}
	};

	for (const TPair<FString, TArray<const FCPItemBalance*>>& ItemPair : ByItem)
	{
		FCPItemSufficiency Summary;
		Summary.bSolverDerived = true;
		Summary.BufferMinutes = -1.0f;
		for (const FCPItemBalance* Balance : ItemPair.Value)
		{
			if (!Balance)
			{
				continue;
			}
			Summary.Item = Balance->Item;
			Summary.NetworkCount++;
			Summary.ConsumerBuildings += Balance->ConsumerBuildings;
			Summary.InactiveConsumerBuildings += FMath::Max(0,
				Balance->MachineStateConsumerBuildings - Balance->ProducingConsumerBuildings);
			const bool bTransportEvidenceKnown = !Balance->bUsesTransport || Balance->bTransportRateKnown;
			AccumulateBasis(Balance->Current, Balance->Current.bKnown && bTransportEvidenceKnown, Summary.Current);
			AccumulateBasis(Balance->Design, Balance->Design.bKnown && bTransportEvidenceKnown, Summary.Design);
			if (Balance->BufferRunwayMinutes >= 0.0f &&
				(Summary.BufferMinutes < 0.0f || Balance->BufferRunwayMinutes < Summary.BufferMinutes))
			{
				Summary.BufferMinutes = Balance->BufferRunwayMinutes;
			}
		}

		const auto FinalizeBasis = [](FCPSufficiencyBasis& Basis)
		{
			Basis.bKnown = Basis.KnownDomainCount > 0 && Basis.UnknownDomainCount == 0;
			if (Basis.bKnown && Basis.DemandPerMinute > KINDA_SMALL_NUMBER)
			{
				Basis.AggregateRatio = Basis.DeliveredPerMinute / Basis.DemandPerMinute;
			}
		};
		FinalizeBasis(Summary.Current);
		FinalizeBasis(Summary.Design);

		// Compatibility contract during Phase C: the old configured-demand fields now alias the
		// design solve. Unlike M2 flow networks, every consumer belongs to exactly one domain.
		Summary.ConnectedSupplyPerMinute = Summary.Design.DeliveredPerMinute;
		Summary.ConfiguredDemandPerMinute = Summary.Design.DemandPerMinute;
		Summary.AggregateRatio = Summary.Design.AggregateRatio;
		Summary.LimitingNetworkSupplyPerMinute = Summary.Design.LimitingDomainDeliveredPerMinute;
		Summary.LimitingNetworkDemandPerMinute = Summary.Design.LimitingDomainDemandPerMinute;
		Summary.LimitingNetworkRatio = Summary.Design.LimitingDomainRatio;
		Summary.bKnown = Summary.Design.bKnown;
		OutSufficiency.Add(MoveTemp(Summary));
	}

	OutSufficiency.Sort([](const FCPItemSufficiency& A, const FCPItemSufficiency& B)
	{
		return A.Item.Name.Compare(B.Item.Name, ESearchCase::IgnoreCase) < 0;
	});
}

namespace
{
constexpr float SolverDeficitTolerance = 0.995f;
constexpr int32 SolverPathDepthCap = 32;

struct FCPSolverDomainEvidence
{
	bool bHasDemand = false;
	bool bHasProducerAnywhere = false;
	const FCPItemBalance* LimitingDomain = nullptr;
	float LimitingRatio = -1.0f;
	ECPFlowUnknownReason UnknownReason = ECPFlowUnknownReason::None;
};

void AddUnknownReason(FCPSolverDomainEvidence& Evidence, ECPFlowUnknownReason Reason)
{
	// An incomplete/non-converged solve is the strongest qualification. Missing machine-path
	// attribution outranks transport because it means we cannot prove which route is relevant.
	const auto Priority = [](ECPFlowUnknownReason Value)
	{
		switch (Value)
		{
		case ECPFlowUnknownReason::IncompleteOrNonConverged: return 3;
		case ECPFlowUnknownReason::PathAttributionMissing: return 2;
		case ECPFlowUnknownReason::TransportRateUnknown: return 1;
		default: return 0;
		}
	};
	if (Priority(Reason) > Priority(Evidence.UnknownReason))
	{
		Evidence.UnknownReason = Reason;
	}
}

bool HasObservedHealthyProducerCapacity(const FCPItemBalance& Balance)
{
	if (Balance.ProducerBuildings <= 0 ||
		Balance.Design.InstalledPerMinute + KINDA_SMALL_NUMBER < Balance.Design.DemandPerMinute)
	{
		return false;
	}

	int32 ObservedProducers = 0;
	for (const FCPBalanceLocation& Location : Balance.Locations)
	{
		if (!Location.bProducer)
		{
			continue;
		}
		ObservedProducers++;
		const bool bHealthyOrBackpressured = Location.bMachineStateKnown &&
			!Location.bNoPower && !Location.bPaused && !Location.bMissingInput &&
			(Location.bProducing || Location.bOutputBlocked);
		if (!bHealthyOrBackpressured)
		{
			return false;
		}
	}
	return ObservedProducers == Balance.ProducerBuildings;
}

FCPSolverDomainEvidence SolverEvidenceFor(
	const TArray<FCPItemBalance>& Balances,
	const FString& ItemName,
	const TSet<FString>* ExpectedConsumerActors)
{
	FCPSolverDomainEvidence Evidence;
	bool bAnyDemandForItem = false;
	bool bMatchedDomain = false;
	for (const FCPItemBalance& Balance : Balances)
	{
		if (!Balance.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		Evidence.bHasProducerAnywhere |= Balance.ProducerBuildings > 0;
		if (Balance.Design.DemandPerMinute <= KINDA_SMALL_NUMBER)
		{
			continue;
		}
		bAnyDemandForItem = true;
		if (ExpectedConsumerActors)
		{
			const bool bFeedsExpectedMachine = ExpectedConsumerActors->Num() > 0 &&
				Balance.Locations.ContainsByPredicate([ExpectedConsumerActors](const FCPBalanceLocation& Location)
				{
					return !Location.bProducer && ExpectedConsumerActors->Contains(Location.ActorName);
				});
			if (!bFeedsExpectedMachine)
			{
				continue;
			}
		}
		bMatchedDomain = true;
		Evidence.bHasDemand = true;
		if (!Balance.Design.bKnown)
		{
			AddUnknownReason(Evidence, ECPFlowUnknownReason::IncompleteOrNonConverged);
			continue;
		}
		if (Balance.bUsesTransport && !Balance.bTransportRateKnown)
		{
			AddUnknownReason(Evidence, ECPFlowUnknownReason::TransportRateUnknown);
			continue;
		}
		// Shared manifolds can make a fair-allocation model pessimistic even while the machines
		// serving this branch are all producing or backed up. That live state is stronger evidence
		// than a theoretical shortage elsewhere on the same input network.
		const float Ratio = HasObservedHealthyProducerCapacity(Balance)
			? 1.0f
			: Balance.Design.DeliveredPerMinute / Balance.Design.DemandPerMinute;
		if (!Evidence.LimitingDomain || Ratio < Evidence.LimitingRatio)
		{
			Evidence.LimitingDomain = &Balance;
			Evidence.LimitingRatio = Ratio;
		}
	}
	// Once a dependency hop names its parent machines, silently falling back to another factory
	// island would recreate the legacy false-limiter bug. Missing actor/domain attribution is
	// unknown, even when an unrelated domain for the same item is perfectly measurable.
	if (ExpectedConsumerActors && bAnyDemandForItem && !bMatchedDomain)
	{
		Evidence.bHasDemand = true;
		AddUnknownReason(Evidence, ECPFlowUnknownReason::PathAttributionMissing);
	}
	return Evidence;
}

TSet<FString> ProducerActorsForEvidence(
	const TArray<FCPItemBalance>& Balances,
	const FString& ItemName,
	const TSet<FString>* ExpectedConsumerActors,
	const FCPSolverDomainEvidence& Evidence)
{
	TSet<FString> Actors;
	for (const FCPItemBalance& Balance : Balances)
	{
		if (!Balance.Item.Name.Equals(ItemName, ESearchCase::IgnoreCase))
		{
			continue;
		}
		if (Evidence.LimitingDomain && &Balance != Evidence.LimitingDomain)
		{
			continue;
		}
		if (!Evidence.LimitingDomain && ExpectedConsumerActors)
		{
			const bool bFeedsExpectedMachine = ExpectedConsumerActors->Num() > 0 &&
				Balance.Locations.ContainsByPredicate([ExpectedConsumerActors](const FCPBalanceLocation& Location)
				{
					return !Location.bProducer && ExpectedConsumerActors->Contains(Location.ActorName);
				});
			if (!bFeedsExpectedMachine)
			{
				continue;
			}
		}
		for (const FCPBalanceLocation& Location : Balance.Locations)
		{
			if (Location.bProducer && !Location.ActorName.IsEmpty())
			{
				Actors.Add(Location.ActorName);
			}
		}
	}
	return Actors;
}

bool IsRateShapedBlocker(ECPBlockerReason Reason)
{
	return Reason == ECPBlockerReason::SupplyBelowDemand ||
		Reason == ECPBlockerReason::ExtractionBelowDemand ||
		Reason == ECPBlockerReason::ProducerNotConnected ||
		Reason == ECPBlockerReason::ConnectivityUnknown;
}

bool DescendToSolverBlocker(
	const FCPFactorySnapshot& Snapshot,
	const TArray<FCPItemBalance>& Balances,
	const FCPItemRef& ItemRef,
	const TSet<FString>* ExpectedConsumerActors,
	TSet<FString>& Visited,
	TArray<FCPItemRef>& Path,
	FCPBlocker& Out)
{
	if (Visited.Contains(ItemRef.Name) || Path.Num() > SolverPathDepthCap)
	{
		return false;
	}
	Visited.Add(ItemRef.Name);

	const FCPProductRow* Product = Snapshot.FindProduct(ItemRef.Name);
	Path.Add(Product ? Product->Item : ItemRef);
	if (!Product)
	{
		Out.Item = ItemRef;
		Out.Reason = ECPBlockerReason::NoProducer;
		Out.Path = Path;
		return true;
	}

	// Machine facts remain snapshot-derived. The solver decides rates and routing, not whether a
	// placed building is powered or presently reporting an empty input buffer.
	if (Product->NoPowerBuildings > 0 && Product->ProducingBuildings == 0)
	{
		Out.Item = Product->Item;
		Out.Reason = ECPBlockerReason::NoPower;
		Out.AffectedBuildings = Product->NoPowerBuildings;
		Out.TotalBuildings = Product->ConfiguredBuildings;
		Out.Path = Path;
		return true;
	}
	const FCPSolverDomainEvidence Evidence = SolverEvidenceFor(Balances, ItemRef.Name, ExpectedConsumerActors);
	const TSet<FString> ProducerActors = ProducerActorsForEvidence(
		Balances, ItemRef.Name, ExpectedConsumerActors, Evidence);

	if (Product->MissingInputBuildings > 0)
	{
		if (const FCPRecipeEdge* Recipe = Snapshot.FindEdgeProducing(ItemRef.Name))
		{
			for (const FCPItemRate& Ingredient : Recipe->Ingredients)
			{
				if (DescendToSolverBlocker(Snapshot, Balances, Ingredient.Item, &ProducerActors, Visited, Path, Out))
				{
					return true;
				}
			}
		}
	}

	if (Evidence.bHasDemand && Evidence.UnknownReason != ECPFlowUnknownReason::None)
	{
		Out.Item = Product->Item;
		Out.Reason = ECPBlockerReason::ConnectivityUnknown;
		Out.Path = Path;
		Out.bSolverDerived = true;
		Out.UnknownReason = Evidence.UnknownReason;
		return true;
	}
	if (Evidence.LimitingDomain && Evidence.LimitingRatio < SolverDeficitTolerance)
	{
		Out.Item = Product->Item;
		Out.SupplyPerMinute = Evidence.LimitingDomain->Design.DeliveredPerMinute;
		Out.DemandPerMinute = Evidence.LimitingDomain->Design.DemandPerMinute;
		Out.Path = Path;
		Out.bSolverDerived = true;
		if (Evidence.LimitingDomain->ProducerBuildings == 0 && Evidence.bHasProducerAnywhere)
		{
			Out.Reason = ECPBlockerReason::ProducerNotConnected;
		}
		else
		{
			Out.Reason = Product->bIsExtraction
				? ECPBlockerReason::ExtractionBelowDemand
				: ECPBlockerReason::SupplyBelowDemand;
		}
		return true;
	}

	if (Product->MissingInputBuildings > 0)
	{
		Out.Item = Product->Item;
		Out.Reason = ECPBlockerReason::ConnectivityUnknown;
		Out.Path = Path;
		Out.bSolverDerived = true;
		Out.UnknownReason = ECPFlowUnknownReason::PathAttributionMissing;
		return true;
	}

	Path.Pop();
	return false;
}

void WalkSolverLimiter(
	const FCPFactorySnapshot& Snapshot,
	const TArray<FCPItemBalance>& Balances,
	const FCPItemRef& ItemRef,
	int32 Depth,
	const TSet<FString>* ExpectedConsumerActors,
	TSet<FString>& Visited,
	TArray<FCPItemRef>& Path,
	FCPLimiter& Best)
{
	if (Visited.Contains(ItemRef.Name) || Depth > SolverPathDepthCap)
	{
		return;
	}
	Visited.Add(ItemRef.Name);
	const FCPProductRow* Product = Snapshot.FindProduct(ItemRef.Name);
	Path.Add(Product ? Product->Item : ItemRef);

	if (Product)
	{
		const FCPSolverDomainEvidence Evidence = SolverEvidenceFor(Balances, ItemRef.Name, ExpectedConsumerActors);
		const TSet<FString> ProducerActors = ProducerActorsForEvidence(
			Balances, ItemRef.Name, ExpectedConsumerActors, Evidence);
		// A healthy intermediate has already demonstrated that its upstream ingredients can
		// sustain the rate needed by this branch. Continuing through it can select a low-ratio
		// ingredient from other consumers on a shared network (for example, a backed-up Wire
		// line) even though that ingredient is not constraining the objective chain.
		const bool bNeedsUpstreamExplanation = Depth == 0 ||
			(Evidence.UnknownReason == ECPFlowUnknownReason::None &&
			 Evidence.LimitingDomain && Evidence.LimitingRatio < SolverDeficitTolerance);
		if (Evidence.bHasDemand && Evidence.UnknownReason != ECPFlowUnknownReason::None)
		{
			Best.UnknownLinks++;
		}
		else if (Evidence.LimitingDomain && Evidence.LimitingDomain->ProducerBuildings > 0 &&
			Evidence.LimitingDomain->Design.InstalledPerMinute > KINDA_SMALL_NUMBER &&
			Evidence.LimitingRatio < SolverDeficitTolerance &&
			(!Best.IsSet() || Evidence.LimitingRatio <= Best.Ratio))
		{
			const FCPItemBalance& Domain = *Evidence.LimitingDomain;
			Best.Item = Product->Item;
			Best.Ratio = Evidence.LimitingRatio;
			Best.SupplyPerMinute = Domain.Design.DeliveredPerMinute;
			Best.DemandPerMinute = Domain.Design.DemandPerMinute;
			Best.InstalledPerMinute = Domain.Design.InstalledPerMinute;
			Best.SustainablePerMinute = Domain.Design.SustainablePerMinute;
			Best.bNetworkScoped = true;
			Best.bSolverDerived = true;
			Best.DomainId = Domain.DomainId;
			Best.ConstraintKind = Domain.DesignConstraintKind;
			Best.EdgeEvidence = Domain.DesignEdgeEvidence;
			Best.Path = Path;
			Best.BufferMinutes = Domain.BufferRunwayMinutes;
			Best.MachineName.Reset();
			Best.PerMachinePerMinute = 0.0f;
			Best.MarginalAction = ECPMarginalActionKind::None;
			Best.MarginalDeliveredPerMinute = 0.0f;
			Best.MarginalEdgeCapacityPerMinute = 0.0f;
			Best.MarginalRatio = -1.0f; // requires the planned full re-solve, not arithmetic guessing
			Best.bMarginalSolverDerived = false;
			if (Domain.ProducerBuildings > 0)
			{
				Best.PerMachinePerMinute = Domain.Design.InstalledPerMinute /
					static_cast<float>(Domain.ProducerBuildings);
				if (const FCPBalanceLocation* Location = Domain.Locations.FindByPredicate(
					[](const FCPBalanceLocation& Candidate) { return Candidate.bProducer; }))
				{
					Best.MachineName = Location->Label;
				}
			}
		}

		if (bNeedsUpstreamExplanation)
		{
			if (const FCPRecipeEdge* Recipe = Snapshot.FindEdgeProducing(ItemRef.Name))
			{
				for (const FCPItemRate& Ingredient : Recipe->Ingredients)
				{
					WalkSolverLimiter(Snapshot, Balances, Ingredient.Item, Depth + 1, &ProducerActors, Visited, Path, Best);
				}
			}
		}
	}
	Path.Pop();
}

void ApplySolverPathToPart(
	const FCPFactorySnapshot& Snapshot,
	const TArray<FCPItemBalance>& Balances,
	FCPPartReport& Part)
{
	if (Part.bPayable ||
		Part.Status == ECPNodeStatus::ReadyToDeliver || Part.Status == ECPNodeStatus::Delivered ||
		Part.Status == ECPNodeStatus::Saturated || Part.Status == ECPNodeStatus::StandbyReserve)
	{
		Part.Limiter = FCPLimiter();
		return;
	}

	// Quantitative blockers belong exclusively to the disjoint flow domains. Base analysis may
	// still provide categorical machine facts (no producer, no power, backed-up byproduct), but
	// an otherwise healthy-looking placed line must also be checked here: installed capacity can
	// be insufficient even when every existing machine is running normally.
	if (Part.Blocker.Reason == ECPBlockerReason::None || IsRateShapedBlocker(Part.Blocker.Reason))
	{
		FCPBlocker SolverBlocker;
		TSet<FString> Visited;
		TArray<FCPItemRef> Path;
		if (DescendToSolverBlocker(Snapshot, Balances, Part.Item, nullptr, Visited, Path, SolverBlocker))
		{
			Part.Blocker = MoveTemp(SolverBlocker);
		}
		else if (IsRateShapedBlocker(Part.Blocker.Reason))
		{
			Part.Blocker = FCPBlocker();
			if (Part.Status == ECPNodeStatus::UnderSupplied)
			{
				Part.Status = Part.EtaMinutes >= 0.0f ? ECPNodeStatus::TimeLimited : ECPNodeStatus::Fulfilled;
			}
		}
	}
	// A rate-shaped blocker can disappear during the solver-backed rewrite above. Do not then
	// repopulate a limiter on the part we just proved healthy.
	if (Part.Status == ECPNodeStatus::Fulfilled)
	{
		Part.Limiter = FCPLimiter();
		return;
	}

	Part.Limiter = FCPLimiter();
	if (Snapshot.FindProduct(Part.Item.Name))
	{
		TSet<FString> Visited;
		TArray<FCPItemRef> Path;
		WalkSolverLimiter(Snapshot, Balances, Part.Item, 0, nullptr, Visited, Path, Part.Limiter);
	}

	// The legacy ETA assumes the currently observed output rate continues indefinitely. When an
	// upstream limiter is temporarily funded by stored stock, use that rate only for the disclosed
	// runway, then finish at the objective producers' input-supported steady-state rate. Producer
	// domains are disjoint, so their sustainable output may be summed without double-counting.
	if (Part.EtaMinutes >= 0.0f && Part.EffectiveRatePerMinute > KINDA_SMALL_NUMBER &&
		Part.StillToProduce > 0 && Part.Limiter.IsSet() && Part.Limiter.BufferMinutes >= 0.0f)
	{
		bool bHasProducerDomain = false;
		bool bSustainableRateKnown = true;
		float SustainableRatePerMinute = 0.0f;
		for (const FCPItemBalance& Balance : Balances)
		{
			if (Balance.ProducerBuildings <= 0 ||
				!Balance.Item.Name.Equals(Part.Item.Name, ESearchCase::IgnoreCase))
			{
				continue;
			}
			bHasProducerDomain = true;
			bSustainableRateKnown &= Balance.Current.bKnown &&
				(!Balance.bUsesTransport || Balance.bTransportRateKnown);
			if (Balance.Current.bKnown)
			{
				SustainableRatePerMinute += Balance.Current.SustainablePerMinute;
			}
		}

		if (bHasProducerDomain && bSustainableRateKnown &&
			SustainableRatePerMinute + KINDA_SMALL_NUMBER < Part.EffectiveRatePerMinute)
		{
			const float RunwayMinutes = FMath::Max(0.0f, Part.Limiter.BufferMinutes);
			const float ProducedDuringRunway = FMath::Min(
				static_cast<float>(Part.StillToProduce), Part.EffectiveRatePerMinute * RunwayMinutes);
			const float RemainingAfterRunway = static_cast<float>(Part.StillToProduce) - ProducedDuringRunway;
			if (RemainingAfterRunway <= KINDA_SMALL_NUMBER)
			{
				Part.EtaMinutes = static_cast<float>(Part.StillToProduce) / Part.EffectiveRatePerMinute;
			}
			else if (SustainableRatePerMinute > KINDA_SMALL_NUMBER)
			{
				Part.EtaMinutes = ProducedDuringRunway / Part.EffectiveRatePerMinute +
					RemainingAfterRunway / SustainableRatePerMinute;
			}
			else
			{
				Part.EtaMinutes = -1.0f;
			}
		}
		else if (!bHasProducerDomain || !bSustainableRateKnown)
		{
			Part.EtaMinutes = -1.0f;
		}
	}

	if (Part.Blocker.Reason != ECPBlockerReason::None &&
		(Part.Status == ECPNodeStatus::Fulfilled || Part.Status == ECPNodeStatus::TimeLimited))
	{
		Part.Status = ECPNodeStatus::UnderSupplied;
	}
}
} // namespace

void FCPFlowAnalysis::ApplySolverPathInterpretation(
	const FCPFactorySnapshot& Snapshot,
	const TArray<FCPItemBalance>& Balances,
	FCPAnalysisResult& InOutResult)
{
	for (FCPObjectiveReport& Objective : InOutResult.Objectives)
	{
		for (FCPPartReport& Part : Objective.Parts)
		{
			ApplySolverPathToPart(Snapshot, Balances, Part);
		}
	}
	for (FCPResearchGapReport& Plan : InOutResult.ResearchPlans)
	{
		for (FCPPartReport& Part : Plan.CostParts)
		{
			ApplySolverPathToPart(Snapshot, Balances, Part);
		}
	}
}

void FCPFlowAnalysis::ApplySolverPlanInterpretation(
	const TArray<FCPItemBalance>& Balances,
	FCPAnalysisResult& InOutResult)
{
	const auto ApplyToChain = [&Balances](TArray<FCPPlannedNode>& Chain)
	{
		for (FCPPlannedNode& Node : Chain)
		{
			if (Node.Kind != ECPPlanNodeKind::ExistingProduction)
			{
				continue;
			}

			bool bHasProducerDomain = false;
			bool bAllProducerDomainsKnown = true;
			float BestSustainableHeadroom = 0.0f;
			for (const FCPItemBalance& Balance : Balances)
			{
				if (Balance.ProducerBuildings <= 0 ||
					!Balance.Item.Name.Equals(Node.Item.Name, ESearchCase::IgnoreCase))
				{
					continue;
				}
				bHasProducerDomain = true;
				const bool bDomainKnown = Balance.Design.bKnown &&
					(!Balance.bUsesTransport || Balance.bTransportRateKnown);
				bAllProducerDomainsKnown &= bDomainKnown;
				if (bDomainKnown)
				{
					BestSustainableHeadroom = FMath::Max(BestSustainableHeadroom,
						Balance.Design.SustainablePerMinute - Balance.Design.DemandPerMinute);
				}
			}

			Node.bSufficiencyKnown = bHasProducerDomain && bAllProducerDomainsKnown &&
				Node.RequiredPerMinute > 0.01f;
			Node.AvailableHeadroomPerMinute = 0.0f;
			Node.SufficiencyRatio = -1.0f;
			if (Node.bSufficiencyKnown)
			{
				Node.AvailableHeadroomPerMinute = FMath::Max(0.0f, BestSustainableHeadroom);
				Node.SufficiencyRatio = Node.AvailableHeadroomPerMinute / Node.RequiredPerMinute;
			}
		}
	};

	for (FCPObjectiveReport& Objective : InOutResult.Objectives)
	{
		for (FCPPartReport& Part : Objective.Parts)
		{
			ApplyToChain(Part.PlannedChain);
		}
	}
	for (FCPResearchGapReport& Plan : InOutResult.ResearchPlans)
	{
		for (FCPPartReport& Part : Plan.CostParts)
		{
			ApplyToChain(Part.PlannedChain);
		}
	}
}

namespace
{
float NextStandardEdgeCapacity(float CurrentCapacity, bool bFluid)
{
	static const float BeltTiers[] = { 60.0f, 120.0f, 270.0f, 480.0f, 780.0f, 1200.0f };
	static const float PipeTiers[] = { 300.0f, 600.0f };
	if (bFluid)
	{
		for (const float Tier : PipeTiers)
		{
			if (Tier > CurrentCapacity + 0.5f)
			{
				return Tier;
			}
		}
	}
	else
	{
		for (const float Tier : BeltTiers)
		{
			if (Tier > CurrentCapacity + 0.5f)
			{
				return Tier;
			}
		}
	}
	return 0.0f;
}

const FCPItemBalance* FindLimiterBalance(const FCPAnalysisResult& Result, const FCPLimiter& Limiter)
{
	return Result.ItemBalances.FindByPredicate([&Limiter](const FCPItemBalance& Balance)
	{
		return Balance.DomainId == Limiter.DomainId &&
			Balance.Item.Name.Equals(Limiter.Item.Name, ESearchCase::IgnoreCase);
	});
}

FCPLimiter* FindPrimaryActionableLimiter(FCPAnalysisResult& Result)
{
	FCPLimiter* Best = nullptr;
	const auto Consider = [&Result, &Best](FCPLimiter& Candidate)
	{
		const FCPItemBalance* Balance = FindLimiterBalance(Result, Candidate);
		bool bPerturbable = false;
		if (Balance && Candidate.ConstraintKind == ECPFlowConstraintKind::InstalledCapacity &&
			Balance->Design.InstalledPerMinute > KINDA_SMALL_NUMBER)
		{
			bPerturbable = Balance->Locations.ContainsByPredicate([](const FCPBalanceLocation& Location)
			{
				return Location.bProducer && !Location.ActorName.IsEmpty();
			});
		}
		else if (Balance && Candidate.ConstraintKind == ECPFlowConstraintKind::DeliveryEdge)
		{
			bPerturbable = Candidate.EdgeEvidence.IsSet();
		}
		const bool bActionable = Candidate.bSolverDerived && Candidate.IsSet() && bPerturbable;
		if (!bActionable)
		{
			return;
		}
		if (!Best || Candidate.Ratio < Best->Ratio - KINDA_SMALL_NUMBER ||
			(FMath::IsNearlyEqual(Candidate.Ratio, Best->Ratio) && Candidate.Path.Num() > Best->Path.Num()))
		{
			Best = &Candidate;
		}
	};
	for (FCPObjectiveReport& Objective : Result.Objectives)
	{
		for (FCPPartReport& Part : Objective.Parts)
		{
			Consider(Part.Limiter);
		}
	}
	for (FCPResearchGapReport& Plan : Result.ResearchPlans)
	{
		for (FCPPartReport& Part : Plan.CostParts)
		{
			Consider(Part.Limiter);
		}
	}
	return Best;
}
} // namespace

void FCPFlowAnalysis::ApplyPrimaryMarginalValue(
	const FCPFlowGraph& Graph,
	const FCPFlowSolveResult& BaselineDesignSolve,
	FCPAnalysisResult& InOutResult)
{
	FCPLimiter* Limiter = FindPrimaryActionableLimiter(InOutResult);
	if (!Limiter || Graph.bTruncated || !BaselineDesignSolve.bConverged)
	{
		return;
	}
	const FCPItemBalance* BaselineBalance = FindLimiterBalance(InOutResult, *Limiter);
	if (!BaselineBalance || !BaselineBalance->Design.bKnown ||
		(BaselineBalance->bUsesTransport && !BaselineBalance->bTransportRateKnown))
	{
		return;
	}

	FCPFlowGraph ScenarioGraph = Graph;
	if (Limiter->ConstraintKind == ECPFlowConstraintKind::InstalledCapacity)
	{
		TSet<FString> ProducerActors;
		for (const FCPBalanceLocation& Location : BaselineBalance->Locations)
		{
			if (Location.bProducer && !Location.ActorName.IsEmpty())
			{
				ProducerActors.Add(Location.ActorName);
			}
		}
		if (ProducerActors.Num() == 0)
		{
			return;
		}
		const float Scale = 1.0f + 1.0f / static_cast<float>(ProducerActors.Num());
		TSet<int32> ScaledInputNodes;
		int32 ScaledProducerActors = 0;
		for (int32 NodeIndex = 0; NodeIndex < ScenarioGraph.Nodes.Num(); ++NodeIndex)
		{
			FCPFlowNode& Node = ScenarioGraph.Nodes[NodeIndex];
			if (Node.Kind != ECPFlowNodeKind::Producer || !ProducerActors.Contains(Node.ActorName) ||
				!FindRate(BasisRates(Node, true), Limiter->Item.Name))
			{
				continue;
			}
			if (Node.DesignRates.Num() == 0)
			{
				Node.DesignRates = Node.Rates;
			}
			for (FCPItemRate& Rate : Node.DesignRates)
			{
				Rate.RatePerMinute *= Scale;
			}
			ScaledProducerActors++;
			if (ScenarioGraph.Nodes.IsValidIndex(Node.PairedNodeIndex) &&
				!ScaledInputNodes.Contains(Node.PairedNodeIndex))
			{
				FCPFlowNode& InputNode = ScenarioGraph.Nodes[Node.PairedNodeIndex];
				if (InputNode.DesignRates.Num() == 0)
				{
					InputNode.DesignRates = InputNode.Rates;
				}
				for (FCPItemRate& Rate : InputNode.DesignRates)
				{
					Rate.RatePerMinute *= Scale;
				}
				ScaledInputNodes.Add(Node.PairedNodeIndex);
			}
		}
		if (ScaledProducerActors == 0)
		{
			return;
		}
		Limiter->MarginalAction = ECPMarginalActionKind::AddAverageMachine;
		Limiter->PerMachinePerMinute = BaselineBalance->Design.InstalledPerMinute /
			static_cast<float>(ProducerActors.Num());
	}
	else if (Limiter->ConstraintKind == ECPFlowConstraintKind::DeliveryEdge &&
		Limiter->EdgeEvidence.IsSet() && ScenarioGraph.Edges.IsValidIndex(Limiter->EdgeEvidence.EdgeIndex))
	{
		FCPFlowEdge& Edge = ScenarioGraph.Edges[Limiter->EdgeEvidence.EdgeIndex];
		const float NextCapacity = NextStandardEdgeCapacity(Edge.CapacityPerMinute, Edge.bFluid);
		if (NextCapacity <= Edge.CapacityPerMinute + KINDA_SMALL_NUMBER)
		{
			return;
		}
		Edge.CapacityPerMinute = NextCapacity;
		Limiter->MarginalAction = ECPMarginalActionKind::UpgradeEdge;
		Limiter->MarginalEdgeCapacityPerMinute = NextCapacity;
	}
	else
	{
		return;
	}

	FCPFlowSolveParams DesignParams;
	DesignParams.bUseDesignRates = true;
	FCPFlowSolveResult ScenarioSolve;
	FCPFlowSolver::Solve(ScenarioGraph, DesignParams, ScenarioSolve);
	if (!ScenarioSolve.bConverged)
	{
		Limiter->MarginalAction = ECPMarginalActionKind::None;
		Limiter->MarginalEdgeCapacityPerMinute = 0.0f;
		return;
	}
	TArray<FCPItemBalance> ScenarioBalances;
	BuildItemBalances(ScenarioGraph, ScenarioSolve, ScenarioSolve, ScenarioBalances);
	const FCPItemBalance* ScenarioBalance = ScenarioBalances.FindByPredicate([Limiter](const FCPItemBalance& Balance)
	{
		return Balance.DomainId == Limiter->DomainId &&
			Balance.Item.Name.Equals(Limiter->Item.Name, ESearchCase::IgnoreCase);
	});
	if (!ScenarioBalance || !ScenarioBalance->Design.bKnown ||
		(ScenarioBalance->bUsesTransport && !ScenarioBalance->bTransportRateKnown) ||
		ScenarioBalance->Design.DemandPerMinute <= KINDA_SMALL_NUMBER)
	{
		Limiter->MarginalAction = ECPMarginalActionKind::None;
		Limiter->MarginalEdgeCapacityPerMinute = 0.0f;
		return;
	}
	Limiter->MarginalDeliveredPerMinute = ScenarioBalance->Design.DeliveredPerMinute;
	Limiter->MarginalRatio = FMath::Clamp(
		ScenarioBalance->Design.DeliveredPerMinute / ScenarioBalance->Design.DemandPerMinute, 0.0f, 1.0f);
	Limiter->bMarginalSolverDerived = true;
}
