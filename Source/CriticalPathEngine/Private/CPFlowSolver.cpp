// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPFlowSolver.h"

namespace
{
constexpr float BIG = 1.0e12f;

/** Water-filling: distribute Amount across N lanes with per-lane limits, equal shares with
 *  redistribution of residuals (FlowModel §3 splitter semantics). Returns total assigned. */
float WaterFill(float Amount, const TArray<int32>& Lanes, const TArray<float>& Limits, TArray<float>& OutAssigned)
{
	float Assigned = 0.0f;
	TArray<int32> Open = Lanes;
	float Remaining = Amount;
	while (Remaining > KINDA_SMALL_NUMBER && Open.Num() > 0)
	{
		const float Share = Remaining / static_cast<float>(Open.Num());
		float PassAssigned = 0.0f;
		for (int32 Index = Open.Num() - 1; Index >= 0; --Index)
		{
			const int32 Lane = Open[Index];
			const float Room = Limits[Lane] - OutAssigned[Lane];
			const float Take = FMath::Min(Share, Room);
			OutAssigned[Lane] += Take;
			PassAssigned += Take;
			if (Take >= Room - KINDA_SMALL_NUMBER)
			{
				Open.RemoveAt(Index); // saturated: its residual redistributes next loop
			}
		}
		Remaining -= PassAssigned;
		if (PassAssigned <= KINDA_SMALL_NUMBER)
		{
			break; // nothing could be placed — all open lanes effectively closed
		}
	}
	Assigned = Amount - Remaining;
	return Assigned;
}

struct FSolverContext
{
	const FCPFlowGraph& Graph;
	bool bDesignBasis = false;

	const TArray<FCPItemRate>& NodeRates(const FCPFlowNode& Node) const
	{
		// Design basis falls back to current rates on nodes that carry no design set.
		return bDesignBasis && Node.DesignRates.Num() > 0 ? Node.DesignRates : Node.Rates;
	}
	TArray<FString> Items;                 // item index table
	TMap<FString, int32> ItemIndex;
	TArray<TArray<int32>> OutEdges;        // per node
	TArray<TArray<int32>> InEdges;         // per node
	TArray<int32> ForwardOrder;            // topological order of the condensed dependency graph
	TBitArray<> NeedsDamping;              // only nodes inside a true dependency cycle
	TArray<int32> WeakComponentByNode;     // disconnected transport domains converge independently
	int32 WeakComponentCount = 0;
	/** Items that can ever reach each node. A factory carries dozens of items but any one node
	 *  touches one or two, so iterating the whole item table per node is ~100x wasted work on a
	 *  real graph. Over-inclusive by construction (splitter filters are ignored), which is safe:
	 *  a candidate that never carries flow simply costs one no-op visit. */
	TArray<TArray<int32>> NodeItems;

	int32 NumItems() const { return Items.Num(); }
	int32 Flat(int32 Edge, int32 Item) const { return Edge * Items.Num() + Item; }

	explicit FSolverContext(const FCPFlowGraph& InGraph, bool bInDesignBasis, bool bDenseItems = false)
		: Graph(InGraph)
		, bDesignBasis(bInDesignBasis)
	{
		// Item table: everything produced or demanded (either basis, for index stability).
		for (const FCPFlowNode& Node : Graph.Nodes)
		{
			TArray<FCPItemRate> All = Node.Rates;
			All.Append(Node.DesignRates);
			for (const FCPItemRate& Rate : All)
			{
				if (!ItemIndex.Contains(Rate.Item.Name))
				{
					ItemIndex.Add(Rate.Item.Name, Items.Num());
					Items.Add(Rate.Item.Name);
				}
			}
		}

		OutEdges.SetNum(Graph.Nodes.Num());
		InEdges.SetNum(Graph.Nodes.Num());
		TArray<TArray<int32>> DependencyOut;
		TArray<TArray<int32>> DependencyIn;
		DependencyOut.SetNum(Graph.Nodes.Num());
		DependencyIn.SetNum(Graph.Nodes.Num());
		for (int32 EdgeIndex = 0; EdgeIndex < Graph.Edges.Num(); ++EdgeIndex)
		{
			const FCPFlowEdge& Edge = Graph.Edges[EdgeIndex];
			if (Graph.Nodes.IsValidIndex(Edge.FromNode) && Graph.Nodes.IsValidIndex(Edge.ToNode))
			{
				OutEdges[Edge.FromNode].Add(EdgeIndex);
				InEdges[Edge.ToNode].Add(EdgeIndex);
				DependencyOut[Edge.FromNode].Add(Edge.ToNode);
				DependencyIn[Edge.ToNode].Add(Edge.FromNode);
			}
		}
		// Candidate items per node: seed from every rate the node declares (both bases, so the
		// set is basis-independent), then flood forward along unblocked edges. One pass over
		// edges per round replaces ~100x redundant per-item visits in every solver iteration.
		{
			TArray<TSet<int32>> Candidates;
			Candidates.SetNum(Graph.Nodes.Num());
			for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
			{
				const FCPFlowNode& Node = Graph.Nodes[NodeIndex];
				for (const FCPItemRate& Rate : Node.Rates)
				{
					if (const int32* Found = ItemIndex.Find(Rate.Item.Name)) { Candidates[NodeIndex].Add(*Found); }
				}
				for (const FCPItemRate& Rate : Node.DesignRates)
				{
					if (const int32* Found = ItemIndex.Find(Rate.Item.Name)) { Candidates[NodeIndex].Add(*Found); }
				}
			}
			// Flood to fixpoint. Bounded by node count; a chain longer than that cannot exist.
			bool bChanged = true;
			for (int32 Round = 0; Round < Graph.Nodes.Num() && bChanged; ++Round)
			{
				bChanged = false;
				for (const FCPFlowEdge& Edge : Graph.Edges)
				{
					if (Edge.bBlocked || !Graph.Nodes.IsValidIndex(Edge.FromNode) || !Graph.Nodes.IsValidIndex(Edge.ToNode))
					{
						continue;
					}
					for (const int32 Item : Candidates[Edge.FromNode])
					{
						bool bAlready = false;
						Candidates[Edge.ToNode].Add(Item, &bAlready);
						bChanged |= !bAlready;
					}
				}
			}
			NodeItems.SetNum(Graph.Nodes.Num());
			for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
			{
				if (bDenseItems)
				{
					// Diagnostic parity mode: every node sees every item.
					NodeItems[NodeIndex].Reserve(Items.Num());
					for (int32 Item = 0; Item < Items.Num(); ++Item) { NodeItems[NodeIndex].Add(Item); }
					continue;
				}
				NodeItems[NodeIndex] = Candidates[NodeIndex].Array();
				NodeItems[NodeIndex].Sort(); // stable visit order keeps results reproducible
			}
		}

		// A manufacturer's output depends on the flow delivered to its paired recipe-input node.
		// This is not a transport edge, but omitting it from ordering forces one whole fixed-point
		// pass per recipe tier and lets an upstream belt loop make every downstream tier appear
		// cyclic. Keep the seam in the dependency graph only; no material crosses it directly.
		for (int32 ProducerIndex = 0; ProducerIndex < Graph.Nodes.Num(); ++ProducerIndex)
		{
			const FCPFlowNode& Producer = Graph.Nodes[ProducerIndex];
			if (Producer.Kind == ECPFlowNodeKind::Producer &&
				Graph.Nodes.IsValidIndex(Producer.PairedNodeIndex) &&
				Producer.PairedNodeIndex != ProducerIndex)
			{
				DependencyOut[Producer.PairedNodeIndex].Add(ProducerIndex);
				DependencyIn[ProducerIndex].Add(Producer.PairedNodeIndex);
			}
		}

		WeakComponentByNode.Init(INDEX_NONE, Graph.Nodes.Num());
		for (int32 Seed = 0; Seed < Graph.Nodes.Num(); ++Seed)
		{
			if (WeakComponentByNode[Seed] != INDEX_NONE)
			{
				continue;
			}
			TArray<int32> Pending;
			Pending.Add(Seed);
			WeakComponentByNode[Seed] = WeakComponentCount;
			for (int32 Head = 0; Head < Pending.Num(); ++Head)
			{
				const int32 NodeIndex = Pending[Head];
				const auto VisitPeer = [this, &Pending](int32 Peer)
				{
					if (WeakComponentByNode.IsValidIndex(Peer) && WeakComponentByNode[Peer] == INDEX_NONE)
					{
						WeakComponentByNode[Peer] = WeakComponentCount;
						Pending.Add(Peer);
					}
				};
				// Convergence tolerances remain transport-domain local. Recipe seams control
				// execution order, but combining every upstream/downstream recipe tier here would
				// let a huge raw-resource line hide meaningful drift on a small final-product belt.
				for (int32 EdgeIndex : OutEdges[NodeIndex]) { VisitPeer(Graph.Edges[EdgeIndex].ToNode); }
				for (int32 EdgeIndex : InEdges[NodeIndex]) { VisitPeer(Graph.Edges[EdgeIndex].FromNode); }
			}
			WeakComponentCount++;
		}

		// Find strongly-connected dependency components without recursion (the stress save has
		// tens of thousands of nodes). Only true SCC members need damping; ordinary nodes after a
		// loop remain a deterministic DAG and must update at full strength.
		TBitArray<> Visited(false, Graph.Nodes.Num());
		TArray<int32> FinishOrder;
		FinishOrder.Reserve(Graph.Nodes.Num());
		for (int32 Seed = 0; Seed < Graph.Nodes.Num(); ++Seed)
		{
			if (Visited[Seed])
			{
				continue;
			}
			TArray<TPair<int32, int32>> Stack;
			Stack.Emplace(Seed, 0);
			Visited[Seed] = true;
			while (Stack.Num() > 0)
			{
				TPair<int32, int32>& Frame = Stack.Last();
				const int32 NodeIndex = Frame.Key;
				if (Frame.Value < DependencyOut[NodeIndex].Num())
				{
					const int32 Peer = DependencyOut[NodeIndex][Frame.Value++];
					if (!Visited[Peer])
					{
						Visited[Peer] = true;
						Stack.Emplace(Peer, 0);
					}
				}
				else
				{
					FinishOrder.Add(NodeIndex);
					Stack.Pop(EAllowShrinking::No);
				}
			}
		}

		TArray<int32> SccByNode;
		SccByNode.Init(INDEX_NONE, Graph.Nodes.Num());
		TArray<TArray<int32>> SccNodes;
		for (int32 OrderIndex = FinishOrder.Num() - 1; OrderIndex >= 0; --OrderIndex)
		{
			const int32 Seed = FinishOrder[OrderIndex];
			if (SccByNode[Seed] != INDEX_NONE)
			{
				continue;
			}
			const int32 SccIndex = SccNodes.AddDefaulted();
			TArray<int32> Pending;
			Pending.Add(Seed);
			SccByNode[Seed] = SccIndex;
			for (int32 Head = 0; Head < Pending.Num(); ++Head)
			{
				const int32 NodeIndex = Pending[Head];
				SccNodes[SccIndex].Add(NodeIndex);
				for (int32 Peer : DependencyIn[NodeIndex])
				{
					if (SccByNode[Peer] == INDEX_NONE)
					{
						SccByNode[Peer] = SccIndex;
						Pending.Add(Peer);
					}
				}
			}
		}

		NeedsDamping.Init(false, Graph.Nodes.Num());
		for (int32 SccIndex = 0; SccIndex < SccNodes.Num(); ++SccIndex)
		{
			bool bCyclic = SccNodes[SccIndex].Num() > 1;
			if (!bCyclic && SccNodes[SccIndex].Num() == 1)
			{
				const int32 NodeIndex = SccNodes[SccIndex][0];
				bCyclic = DependencyOut[NodeIndex].Contains(NodeIndex);
			}
			if (bCyclic)
			{
				for (int32 NodeIndex : SccNodes[SccIndex])
				{
					NeedsDamping[NodeIndex] = true;
				}
			}
		}

		// Topologically order the condensed SCC graph. Nodes inside a true cycle retain stable
		// index order; every downstream SCC is now placed after it without inheriting damping.
		TArray<TArray<int32>> SccOut;
		SccOut.SetNum(SccNodes.Num());
		TArray<int32> SccInDegree;
		SccInDegree.SetNumZeroed(SccNodes.Num());
		TSet<uint64> SeenSccEdges;
		for (int32 FromNode = 0; FromNode < DependencyOut.Num(); ++FromNode)
		{
			for (int32 ToNode : DependencyOut[FromNode])
			{
				const int32 FromScc = SccByNode[FromNode];
				const int32 ToScc = SccByNode[ToNode];
				if (FromScc == ToScc)
				{
					continue;
				}
				const uint64 Key = (static_cast<uint64>(static_cast<uint32>(FromScc)) << 32) |
					static_cast<uint32>(ToScc);
				if (!SeenSccEdges.Contains(Key))
				{
					SeenSccEdges.Add(Key);
					SccOut[FromScc].Add(ToScc);
					SccInDegree[ToScc]++;
				}
			}
		}
		TArray<int32> SccQueue;
		for (int32 SccIndex = 0; SccIndex < SccNodes.Num(); ++SccIndex)
		{
			if (SccInDegree[SccIndex] == 0)
			{
				SccQueue.Add(SccIndex);
			}
		}
		for (int32 Head = 0; Head < SccQueue.Num(); ++Head)
		{
			const int32 SccIndex = SccQueue[Head];
			SccNodes[SccIndex].Sort();
			ForwardOrder.Append(SccNodes[SccIndex]);
			for (int32 ToScc : SccOut[SccIndex])
			{
				if (--SccInDegree[ToScc] == 0)
				{
					SccQueue.Add(ToScc);
				}
			}
		}
	}

	float EdgeCapacity(int32 EdgeIndex) const
	{
		const FCPFlowEdge& Edge = Graph.Edges[EdgeIndex];
		return Edge.bBlocked ? 0.0f : Edge.CapacityPerMinute;
	}

	/** Splitter rule eligibility for an item on a given out edge. */
	bool RuleAdmits(const FCPFlowNode& Node, const FCPFlowEdge& Edge, const FString& Item, bool bOverflowPhase) const
	{
		if (Node.Kind != ECPFlowNodeKind::Splitter || !Node.OutRules.IsValidIndex(Edge.FromPortIndex))
		{
			return !bOverflowPhase; // non-splitter (or unruled) ports behave as Any
		}
		const FCPSplitterRule& Rule = Node.OutRules[Edge.FromPortIndex];
		switch (Rule.Rule)
		{
		case ECPSplitterOutRule::None:
			return false;
		case ECPSplitterOutRule::Overflow:
			return bOverflowPhase;
		case ECPSplitterOutRule::Filtered:
			return !bOverflowPhase && Rule.Items.ContainsByPredicate(
				[&Item](const FString& F) { return F.Equals(Item, ESearchCase::IgnoreCase); });
		case ECPSplitterOutRule::AnyUndefined:
		{
			if (bOverflowPhase)
			{
				return false;
			}
			// Item must not be specifically filtered by a sibling port.
			for (const FCPSplitterRule& Sibling : Node.OutRules)
			{
				if (Sibling.Rule == ECPSplitterOutRule::Filtered && Sibling.Items.ContainsByPredicate(
					[&Item](const FString& F) { return F.Equals(Item, ESearchCase::IgnoreCase); }))
				{
					return false;
				}
			}
			return true;
		}
		case ECPSplitterOutRule::Any:
		default:
			return !bOverflowPhase;
		}
	}
};
} // namespace

void FCPFlowSolver::Solve(const FCPFlowGraph& Graph, const FCPFlowSolveParams& Params, FCPFlowSolveResult& OutResult)
{
	OutResult = FCPFlowSolveResult();
	if (Graph.Nodes.Num() == 0 || Graph.bTruncated)
	{
		OutResult.bConverged = !Graph.bTruncated && Graph.Edges.Num() == 0;
		return;
	}

	FSolverContext Ctx(Graph, Params.bUseDesignRates, Params.bDenseItemIteration);
	const int32 NumItems = Ctx.NumItems();
	const int32 NumEdges = Graph.Edges.Num();
	if (NumItems == 0)
	{
		OutResult.bConverged = true;
		return;
	}

	TArray<float> Flow;        // solved flow per edge per item
	TArray<float> Acceptance;  // downstream willingness per edge per item
	Flow.SetNumZeroed(NumEdges * NumItems);
	Acceptance.Init(BIG, NumEdges * NumItems);

	TArray<float> NodeSupply;  // per node per item, rebuilt per pass
	NodeSupply.SetNumZeroed(Graph.Nodes.Num() * NumItems);
	TArray<float> NodeWant;    // per node per item, rebuilt per pass
	NodeWant.SetNumZeroed(Graph.Nodes.Num() * NumItems);

	// Supply-pass scratch, allocated ONCE. These are indexed by global edge index but only ever
	// touched for the current node's out edges, so they are cleared per node rather than wholesale.
	// Allocating and zeroing NumEdges of each per (node, item, iteration) made the solve scale
	// with nodes x items x edges instead of with the work actually done -- the dominant cost on
	// large factories, and the reason a megabase took seconds rather than milliseconds.
	TArray<float> Limits;
	Limits.SetNumZeroed(NumEdges);
	TArray<float> AssignedPerEdge;
	AssignedPerEdge.SetNumZeroed(NumEdges);
	TArray<int32> Lanes;

	// Sustainable manufacturer output is limited by its least-supplied ingredient. Input and
	// output halves are deliberately separate graph nodes, so this is the recipe-transform seam
	// that propagates upstream shortages into downstream production. It reads the previous
	// iteration's input flow; damping handles feedback loops.
	const auto ProducerSupportRatio = [&Ctx, &Flow](int32 ProducerNodeIndex)
	{
		const FCPFlowNode& Producer = Ctx.Graph.Nodes[ProducerNodeIndex];
		if (!Ctx.Graph.Nodes.IsValidIndex(Producer.PairedNodeIndex))
		{
			return 1.0f; // extractor or other raw producer
		}
		const FCPFlowNode& Consumer = Ctx.Graph.Nodes[Producer.PairedNodeIndex];
		if (Consumer.Kind != ECPFlowNodeKind::Consumer)
		{
			return 1.0f;
		}
		float Support = 1.0f;
		bool bHasRequiredInput = false;
		for (const FCPItemRate& Demand : Ctx.NodeRates(Consumer))
		{
			if (Demand.RatePerMinute <= KINDA_SMALL_NUMBER)
			{
				continue;
			}
			bHasRequiredInput = true;
			const int32* Item = Ctx.ItemIndex.Find(Demand.Item.Name);
			float Delivered = 0.0f;
			if (Item)
			{
				for (int32 EdgeIndex : Ctx.InEdges[Producer.PairedNodeIndex])
				{
					Delivered += Flow[Ctx.Flat(EdgeIndex, *Item)];
				}
			}
			Support = FMath::Min(Support, FMath::Clamp(Delivered / Demand.RatePerMinute, 0.0f, 1.0f));
		}
		return bHasRequiredInput ? Support : 1.0f;
	};

	bool bConverged = false;
	int32 Iteration = 0;
	// Survives the loop: on exit this is the LAST iteration's verdict per component, which is
	// what decides whose numbers get published.
	TArray<bool> ComponentConverged;
	ComponentConverged.Init(false, Ctx.WeakComponentCount);
	TArray<float> ComponentSupply;
	ComponentSupply.SetNumZeroed(Ctx.WeakComponentCount);
	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		if (Graph.Nodes[NodeIndex].Kind == ECPFlowNodeKind::Producer)
		{
			for (const FCPItemRate& Rate : Ctx.NodeRates(Graph.Nodes[NodeIndex]))
			{
				ComponentSupply[Ctx.WeakComponentByNode[NodeIndex]] += FMath::Max(0.0f, Rate.RatePerMinute);
			}
		}
	}

	for (; Iteration < Params.MaxIterations && !bConverged; ++Iteration)
	{
		// ---- supply pass (forward order; Gauss-Seidel within the pass) --------------------
		float MaxDelta = 0.0f;
		int32 MaxDeltaEdge = INDEX_NONE;
		int32 MaxDeltaItem = INDEX_NONE;
		TArray<float> ComponentMaxDelta;
		ComponentMaxDelta.SetNumZeroed(Ctx.WeakComponentCount);
		TArray<float> EdgeRemaining;
		EdgeRemaining.SetNum(NumEdges);
		for (int32 EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
		{
			EdgeRemaining[EdgeIndex] = Ctx.EdgeCapacity(EdgeIndex);
		}
		for (float& Value : NodeSupply)
		{
			Value = 0.0f;
		}

		for (int32 NodeIndex : Ctx.ForwardOrder)
		{
			const FCPFlowNode& Node = Graph.Nodes[NodeIndex];
			const float NodeDamping = Ctx.NeedsDamping[NodeIndex] ? Params.Damping : 1.0f;

			// Inflow: freshly computed for topologically-earlier sources, previous-iteration
			// flow for back edges (cycles) — the damped Jacobi part.
			for (const int32 Item : Ctx.NodeItems[NodeIndex])
			{
				float In = 0.0f;
				for (int32 EdgeIndex : Ctx.InEdges[NodeIndex])
				{
					In += Flow[Ctx.Flat(EdgeIndex, Item)];
				}
				if (Node.Kind == ECPFlowNodeKind::Producer)
				{
					// A producer node's supply is its own output rate (its in edges, if any,
					// are ignored — capture never wires them).
					In = 0.0f;
					const float SupportRatio = ProducerSupportRatio(NodeIndex);
					for (const FCPItemRate& Rate : Ctx.NodeRates(Node))
					{
						if (Ctx.ItemIndex[Rate.Item.Name] == Item)
						{
							In = Rate.RatePerMinute * SupportRatio;
						}
					}
				}
				NodeSupply[NodeIndex * NumItems + Item] = In;
			}

			// Consumers and sinks absorb; nothing forwards.
			if (Node.Kind == ECPFlowNodeKind::Consumer || Node.Kind == ECPFlowNodeKind::Sink)
			{
				continue;
			}
			if (Ctx.OutEdges[NodeIndex].Num() == 0)
			{
				continue;
			}

			// Distribute supply to out edges: two phases (normal, then overflow ports).
			for (const int32 Item : Ctx.NodeItems[NodeIndex])
			{
				float Supply = NodeSupply[NodeIndex * NumItems + Item];
				if (Supply <= KINDA_SMALL_NUMBER)
				{
					// Still write zero targets so stale flow decays.
					for (int32 EdgeIndex : Ctx.OutEdges[NodeIndex])
					{
						const int32 FlatIndex = Ctx.Flat(EdgeIndex, Item);
						const float Target = 0.0f;
						const float Delta = (Target - Flow[FlatIndex]) * NodeDamping;
						Flow[FlatIndex] += Delta;
						ComponentMaxDelta[Ctx.WeakComponentByNode[NodeIndex]] = FMath::Max(
							ComponentMaxDelta[Ctx.WeakComponentByNode[NodeIndex]], FMath::Abs(Delta));
						if (FMath::Abs(Delta) > MaxDelta)
						{
							MaxDelta = FMath::Abs(Delta);
							MaxDeltaEdge = EdgeIndex;
							MaxDeltaItem = Item;
						}
					}
					continue;
				}

				// Clear only what this node touches; the buffers are shared across the whole solve.
				for (int32 EdgeIndex : Ctx.OutEdges[NodeIndex])
				{
					Limits[EdgeIndex] = 0.0f;
					AssignedPerEdge[EdgeIndex] = 0.0f;
				}

				for (int32 Phase = 0; Phase < 2 && Supply > KINDA_SMALL_NUMBER; ++Phase)
				{
					const bool bOverflowPhase = Phase == 1;
					Lanes.Reset(); // keeps capacity across iterations
					for (int32 EdgeIndex : Ctx.OutEdges[NodeIndex])
					{
						if (Ctx.RuleAdmits(Node, Graph.Edges[EdgeIndex], Ctx.Items[Item], bOverflowPhase))
						{
							Lanes.Add(EdgeIndex);
							Limits[EdgeIndex] = FMath::Min(EdgeRemaining[EdgeIndex],
								Acceptance[Ctx.Flat(EdgeIndex, Item)]) + AssignedPerEdge[EdgeIndex];
						}
					}
					Supply -= WaterFill(Supply, Lanes, Limits, AssignedPerEdge);
				}

				for (int32 EdgeIndex : Ctx.OutEdges[NodeIndex])
				{
					const int32 FlatIndex = Ctx.Flat(EdgeIndex, Item);
					const float Target = AssignedPerEdge[EdgeIndex];
					const float Delta = (Target - Flow[FlatIndex]) * NodeDamping;
					Flow[FlatIndex] += Delta;
					ComponentMaxDelta[Ctx.WeakComponentByNode[NodeIndex]] = FMath::Max(
						ComponentMaxDelta[Ctx.WeakComponentByNode[NodeIndex]], FMath::Abs(Delta));
					EdgeRemaining[EdgeIndex] = FMath::Max(0.0f, EdgeRemaining[EdgeIndex] - Flow[FlatIndex]);
					if (FMath::Abs(Delta) > MaxDelta)
					{
						MaxDelta = FMath::Abs(Delta);
						MaxDeltaEdge = EdgeIndex;
						MaxDeltaItem = Item;
					}
				}
			}
		}

		// ---- acceptance pass (reverse order; Gauss-Seidel) --------------------------------
		for (float& Value : NodeWant)
		{
			Value = 0.0f;
		}

		for (int32 OrderIndex = Ctx.ForwardOrder.Num() - 1; OrderIndex >= 0; --OrderIndex)
		{
			const int32 NodeIndex = Ctx.ForwardOrder[OrderIndex];
			const FCPFlowNode& Node = Graph.Nodes[NodeIndex];

			for (const int32 Item : Ctx.NodeItems[NodeIndex])
			{
				float Want = 0.0f;
				switch (Node.Kind)
				{
				case ECPFlowNodeKind::Consumer:
					for (const FCPItemRate& Rate : Ctx.NodeRates(Node))
					{
						if (Ctx.ItemIndex[Rate.Item.Name] == Item)
						{
							Want = Rate.RatePerMinute;
						}
					}
					break;
				case ECPFlowNodeKind::Sink:
					Want = BIG;
					break;
				// Storage is TRANSPARENT at steady state (FlowModel S5.3): it wants only what
				// its downstream wants. Absorb-until-full sounded right but gave every non-full
				// tank infinite priority -- live validation showed tank farms drinking the
				// entire oil supply while refineries and train loaders starved.
				default:
					for (int32 EdgeIndex : Ctx.OutEdges[NodeIndex])
					{
						Want += FMath::Min(Acceptance[Ctx.Flat(EdgeIndex, Item)], Ctx.EdgeCapacity(EdgeIndex));
					}
					break;
				}
				NodeWant[NodeIndex * NumItems + Item] = FMath::Min(Want, BIG);
			}

			// Allocate this node's want across its in edges. Ordinary routing remains
			// offer-proportional/fair; priority mergers exhaust higher configured groups first.
			for (const int32 Item : Ctx.NodeItems[NodeIndex])
			{
				const float Want = NodeWant[NodeIndex * NumItems + Item];
				const TArray<int32>& Ins = Ctx.InEdges[NodeIndex];
				if (Ins.Num() == 0)
				{
					continue;
				}
				const auto AllocateGroup = [&Ctx, &Flow, &Acceptance, Item](
					const TArray<int32>& Group, float GroupWant)
				{
					float TotalOffer = 0.0f;
					for (int32 EdgeIndex : Group)
					{
						TotalOffer += Flow[Ctx.Flat(EdgeIndex, Item)];
					}
					const float Unmet = FMath::Max(0.0f, GroupWant - TotalOffer);
					for (int32 EdgeIndex : Group)
					{
						float Share;
						if (GroupWant >= BIG * 0.5f)
						{
							Share = BIG;
						}
						else if (TotalOffer > GroupWant)
						{
							Share = GroupWant * (Flow[Ctx.Flat(EdgeIndex, Item)] / TotalOffer);
						}
						else
						{
							Share = Flow[Ctx.Flat(EdgeIndex, Item)] +
								Unmet / static_cast<float>(Group.Num());
						}
						Acceptance[Ctx.Flat(EdgeIndex, Item)] =
							FMath::Min(Share, Ctx.EdgeCapacity(EdgeIndex));
					}
				};

				if (!Node.bPriorityMerger || Want >= BIG * 0.5f)
				{
					AllocateGroup(Ins, Want);
					continue;
				}

				TArray<int32> Priorities;
				for (int32 EdgeIndex : Ins)
				{
					Priorities.AddUnique(Graph.Edges[EdgeIndex].MergerInputPriority);
				}
				Priorities.Sort(TGreater<int32>());
				float RemainingWant = Want;
				for (int32 Priority : Priorities)
				{
					TArray<int32> Group;
					float CurrentGroupOffer = 0.0f;
					for (int32 EdgeIndex : Ins)
					{
						if (Graph.Edges[EdgeIndex].MergerInputPriority == Priority)
						{
							Group.Add(EdgeIndex);
							CurrentGroupOffer += Flow[Ctx.Flat(EdgeIndex, Item)];
						}
					}
					AllocateGroup(Group, RemainingWant);
					RemainingWant = FMath::Max(0.0f, RemainingWant - CurrentGroupOffer);
				}
			}
		}

		OutResult.DeltaHistory.Add(MaxDelta);
		OutResult.LastMaxDeltaEdge = MaxDeltaEdge;
		OutResult.LastMaxDeltaItem = MaxDeltaItem != INDEX_NONE ? Ctx.Items[MaxDeltaItem] : FString();
		// Every component is evaluated every iteration — no early-out on the first failure, since
		// the per-component verdicts are the output, not just the loop condition.
		bConverged = Iteration > 0;
		for (int32 Component = 0; Component < Ctx.WeakComponentCount; ++Component)
		{
			const float ComponentEpsilon = FMath::Max(
				Params.Epsilon, Params.RelativeEpsilon * ComponentSupply[Component]);
			ComponentConverged[Component] = Iteration > 0 && ComponentMaxDelta[Component] < ComponentEpsilon;
			bConverged &= ComponentConverged[Component];
		}
	}

	OutResult.bConverged = bConverged;
	OutResult.Iterations = Iteration;
	OutResult.ComponentConverged = ComponentConverged;
	OutResult.ComponentByNode = Ctx.WeakComponentByNode;

	// UNKNOWN is per component, not global (FlowModel.md §3.3: "the island's flows report
	// UNKNOWN"). Discarding the whole solve because one domain oscillates threw away every
	// correctly solved line in the factory and reported them all as unmeasurable.
	auto ComponentSettled = [&ComponentConverged, &Ctx](int32 NodeIndex)
	{
		if (!Ctx.WeakComponentByNode.IsValidIndex(NodeIndex))
		{
			return false;
		}
		const int32 Component = Ctx.WeakComponentByNode[NodeIndex];
		return ComponentConverged.IsValidIndex(Component) && ComponentConverged[Component];
	};

	for (int32 EdgeIndex = 0; EdgeIndex < NumEdges; ++EdgeIndex)
	{
		if (!ComponentSettled(Graph.Edges[EdgeIndex].FromNode))
		{
			continue;
		}
		for (int32 Item = 0; Item < NumItems; ++Item)
		{
			const float Rate = Flow[Ctx.Flat(EdgeIndex, Item)];
			if (Rate > Params.Epsilon)
			{
				FCPEdgeItemFlow EdgeFlow;
				EdgeFlow.EdgeIndex = EdgeIndex;
				EdgeFlow.ItemName = Ctx.Items[Item];
				EdgeFlow.RatePerMinute = Rate;
				OutResult.EdgeFlows.Add(MoveTemp(EdgeFlow));
			}
		}
	}

	for (int32 NodeIndex = 0; NodeIndex < Graph.Nodes.Num(); ++NodeIndex)
	{
		if (!ComponentSettled(NodeIndex))
		{
			continue; // absent = UNKNOWN for this node's domain only
		}
		const FCPFlowNode& Node = Graph.Nodes[NodeIndex];
		if (Node.Kind == ECPFlowNodeKind::Consumer)
		{
			for (const FCPItemRate& Demand : Ctx.NodeRates(Node))
			{
				const int32 Item = Ctx.ItemIndex[Demand.Item.Name];
				float Delivered = 0.0f;
				for (int32 EdgeIndex : Ctx.InEdges[NodeIndex])
				{
					Delivered += Flow[Ctx.Flat(EdgeIndex, Item)];
				}
				FCPConsumerDelivery Delivery;
				Delivery.NodeIndex = NodeIndex;
				Delivery.ItemName = Demand.Item.Name;
				Delivery.DeliveredPerMinute = FMath::Min(Delivered, Demand.RatePerMinute);
				Delivery.DemandPerMinute = Demand.RatePerMinute;
				OutResult.Deliveries.Add(MoveTemp(Delivery));
			}
		}
		else if (Node.Kind == ECPFlowNodeKind::Producer)
		{
			const float SupportRatio = ProducerSupportRatio(NodeIndex);
			for (const FCPItemRate& Output : Ctx.NodeRates(Node))
			{
				const int32 Item = Ctx.ItemIndex[Output.Item.Name];
				float Actual = 0.0f;
				for (int32 EdgeIndex : Ctx.OutEdges[NodeIndex])
				{
					Actual += Flow[Ctx.Flat(EdgeIndex, Item)];
				}
				FCPEdgeItemFlow ProducerActual;
				ProducerActual.EdgeIndex = NodeIndex; // node index by convention (see header)
				ProducerActual.ItemName = Output.Item.Name;
				ProducerActual.RatePerMinute = Actual;
				OutResult.ProducerActuals.Add(MoveTemp(ProducerActual));

				FCPEdgeItemFlow Sustainable;
				Sustainable.EdgeIndex = NodeIndex;
				Sustainable.ItemName = Output.Item.Name;
				Sustainable.RatePerMinute = Output.RatePerMinute * SupportRatio;
				OutResult.ProducerSustainable.Add(MoveTemp(Sustainable));
			}
		}
	}
}

namespace
{
/** Collapse only strongly-connected passive solid-buffer loops. A storage/merger loop is one
 *  buffered routing domain: internal circulation has no productive meaning, while its external
 *  belt edges are the measurable ingress/delivery gates. Splitters are deliberately excluded
 *  because their port rules remain semantically significant. */
void CollapsePassiveBufferLoops(FCPFlowGraph& Graph)
{
	const int32 NumNodes = Graph.Nodes.Num();
	if (NumNodes == 0)
	{
		return;
	}
	const auto IsPassiveBufferNode = [&Graph](int32 NodeIndex)
	{
		if (!Graph.Nodes.IsValidIndex(NodeIndex))
		{
			return false;
		}
		const FCPFlowNode& Node = Graph.Nodes[NodeIndex];
		return (Node.Kind == ECPFlowNodeKind::Storage || Node.Kind == ECPFlowNodeKind::Merger) &&
			!Node.bPriorityMerger &&
			Node.Rates.Num() == 0 && Node.DesignRates.Num() == 0 && Node.OutRules.Num() == 0 &&
			Node.PairedNodeIndex == INDEX_NONE;
	};

	TArray<TArray<int32>> OutNodes;
	TArray<TArray<int32>> InNodes;
	OutNodes.SetNum(NumNodes);
	InNodes.SetNum(NumNodes);
	for (const FCPFlowEdge& Edge : Graph.Edges)
	{
		if (!Edge.bBlocked && !Edge.bFluid && !Edge.bTransport &&
			IsPassiveBufferNode(Edge.FromNode) && IsPassiveBufferNode(Edge.ToNode))
		{
			OutNodes[Edge.FromNode].Add(Edge.ToNode);
			InNodes[Edge.ToNode].Add(Edge.FromNode);
		}
	}

	TBitArray<> Visited(false, NumNodes);
	TArray<int32> FinishOrder;
	FinishOrder.Reserve(NumNodes);
	for (int32 Seed = 0; Seed < NumNodes; ++Seed)
	{
		if (!IsPassiveBufferNode(Seed) || Visited[Seed])
		{
			continue;
		}
		TArray<TPair<int32, int32>> Stack;
		Stack.Emplace(Seed, 0);
		Visited[Seed] = true;
		while (Stack.Num() > 0)
		{
			TPair<int32, int32>& Frame = Stack.Last();
			const int32 NodeIndex = Frame.Key;
			if (Frame.Value < OutNodes[NodeIndex].Num())
			{
				const int32 Peer = OutNodes[NodeIndex][Frame.Value++];
				if (!Visited[Peer])
				{
					Visited[Peer] = true;
					Stack.Emplace(Peer, 0);
				}
			}
			else
			{
				FinishOrder.Add(NodeIndex);
				Stack.Pop(EAllowShrinking::No);
			}
		}
	}

	TArray<int32> SccByNode;
	SccByNode.Init(INDEX_NONE, NumNodes);
	TArray<TArray<int32>> SccNodes;
	for (int32 OrderIndex = FinishOrder.Num() - 1; OrderIndex >= 0; --OrderIndex)
	{
		const int32 Seed = FinishOrder[OrderIndex];
		if (SccByNode[Seed] != INDEX_NONE)
		{
			continue;
		}
		const int32 SccIndex = SccNodes.AddDefaulted();
		TArray<int32> Pending;
		Pending.Add(Seed);
		SccByNode[Seed] = SccIndex;
		for (int32 Head = 0; Head < Pending.Num(); ++Head)
		{
			const int32 NodeIndex = Pending[Head];
			SccNodes[SccIndex].Add(NodeIndex);
			for (int32 Peer : InNodes[NodeIndex])
			{
				if (SccByNode[Peer] == INDEX_NONE)
				{
					SccByNode[Peer] = SccIndex;
					Pending.Add(Peer);
				}
			}
		}
	}

	TArray<int32> Representative;
	TBitArray<> CollapsedRepresentative(false, NumNodes);
	for (int32 NodeIndex = 0; NodeIndex < NumNodes; ++NodeIndex)
	{
		Representative.Add(NodeIndex);
	}
	bool bAnyCollapsed = false;
	for (const TArray<int32>& Members : SccNodes)
	{
		bool bSelfLoop = false;
		if (Members.Num() == 1)
		{
			bSelfLoop = OutNodes[Members[0]].Contains(Members[0]);
		}
		if (Members.Num() <= 1 && !bSelfLoop)
		{
			continue;
		}
		int32 GroupRepresentative = Members[0];
		for (int32 NodeIndex : Members)
		{
			if (Graph.Nodes[NodeIndex].Kind == ECPFlowNodeKind::Storage)
			{
				GroupRepresentative = NodeIndex;
				break;
			}
		}
		for (int32 NodeIndex : Members)
		{
			Representative[NodeIndex] = GroupRepresentative;
		}
		CollapsedRepresentative[GroupRepresentative] = true;
		bAnyCollapsed = true;
	}
	if (!bAnyCollapsed)
	{
		return;
	}

	TArray<int32> OldToNew;
	OldToNew.Init(INDEX_NONE, NumNodes);
	TMap<int32, int32> RepresentativeToNew;
	TArray<FCPFlowNode> NewNodes;
	NewNodes.Reserve(NumNodes);
	for (int32 OldIndex = 0; OldIndex < NumNodes; ++OldIndex)
	{
		const int32 GroupRepresentative = Representative[OldIndex];
		int32* Existing = RepresentativeToNew.Find(GroupRepresentative);
		if (!Existing)
		{
			FCPFlowNode Node = Graph.Nodes[GroupRepresentative];
			if (CollapsedRepresentative[GroupRepresentative])
			{
				Node.Kind = ECPFlowNodeKind::Storage;
				Node.Label = TEXT("Buffered Belt Loop");
				Node.bContractible = false;
			}
			const int32 NewIndex = NewNodes.Add(MoveTemp(Node));
			RepresentativeToNew.Add(GroupRepresentative, NewIndex);
			Existing = RepresentativeToNew.Find(GroupRepresentative);
		}
		OldToNew[OldIndex] = *Existing;
	}

	// The representative already contributed its stock when copied above; append every other
	// member so runway and funded-buffer evidence retain the complete loop inventory.
	for (int32 OldIndex = 0; OldIndex < NumNodes; ++OldIndex)
	{
		if (Representative[OldIndex] != OldIndex)
		{
			NewNodes[OldToNew[OldIndex]].Stocks.Append(Graph.Nodes[OldIndex].Stocks);
		}
	}
	for (int32 OldIndex = 0; OldIndex < NumNodes; ++OldIndex)
	{
		const int32 OldPair = Graph.Nodes[OldIndex].PairedNodeIndex;
		if (Graph.Nodes.IsValidIndex(OldPair))
		{
			NewNodes[OldToNew[OldIndex]].PairedNodeIndex = OldToNew[OldPair];
		}
	}

	TArray<FCPFlowEdge> NewEdges;
	NewEdges.Reserve(Graph.Edges.Num());
	for (const FCPFlowEdge& Edge : Graph.Edges)
	{
		FCPFlowEdge Remapped = Edge;
		Remapped.FromNode = OldToNew[Edge.FromNode];
		Remapped.ToNode = OldToNew[Edge.ToNode];
		if (Remapped.FromNode != Remapped.ToNode)
		{
			NewEdges.Add(MoveTemp(Remapped));
		}
	}
	Graph.Nodes = MoveTemp(NewNodes);
	Graph.Edges = MoveTemp(NewEdges);
}
} // namespace

void FCPFlowSolver::CollapseContractibleClusters(FCPFlowGraph& Graph)
{
	const int32 NumNodes = Graph.Nodes.Num();
	if (NumNodes == 0)
	{
		return;
	}
	TArray<int32> Parent;
	Parent.SetNum(NumNodes);
	for (int32 Index = 0; Index < NumNodes; ++Index)
	{
		Parent[Index] = Index;
	}
	const auto Find = [&Parent](int32 Node)
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
		if (!Edge.bBlocked && Graph.Nodes[Edge.FromNode].bContractible && Graph.Nodes[Edge.ToNode].bContractible)
		{
			Parent[Find(Edge.FromNode)] = Find(Edge.ToNode);
		}
	}
	TArray<int32> OldToNew;
	OldToNew.Init(INDEX_NONE, NumNodes);
	TArray<FCPFlowNode> NewNodes;
	NewNodes.Reserve(NumNodes);
	for (int32 Index = 0; Index < NumNodes; ++Index)
	{
		if (!Graph.Nodes[Index].bContractible || Find(Index) == Index)
		{
			OldToNew[Index] = NewNodes.Add(Graph.Nodes[Index]);
		}
	}
	for (int32 Index = 0; Index < NumNodes; ++Index)
	{
		if (OldToNew[Index] == INDEX_NONE)
		{
			OldToNew[Index] = OldToNew[Find(Index)];
		}
	}
	for (int32 OldIndex = 0; OldIndex < NumNodes; ++OldIndex)
	{
		const int32 NewIndex = OldToNew[OldIndex];
		const int32 OldPair = Graph.Nodes[OldIndex].PairedNodeIndex;
		if (NewNodes.IsValidIndex(NewIndex) && Graph.Nodes.IsValidIndex(OldPair))
		{
			NewNodes[NewIndex].PairedNodeIndex = OldToNew[OldPair];
		}
		if (NewNodes.IsValidIndex(NewIndex) && OldIndex != Find(OldIndex))
		{
			NewNodes[NewIndex].Stocks.Append(Graph.Nodes[OldIndex].Stocks);
		}
	}

	TArray<FCPFlowEdge> NewEdges;
	NewEdges.Reserve(Graph.Edges.Num());
	TMap<TPair<int32, int32>, int32> MergedFluidEdge; // parallel unblocked pipes ADD capacity
	for (const FCPFlowEdge& Edge : Graph.Edges)
	{
		FCPFlowEdge Remapped = Edge;
		Remapped.FromNode = OldToNew[Edge.FromNode];
		Remapped.ToNode = OldToNew[Edge.ToNode];
		if (Remapped.FromNode == Remapped.ToNode)
		{
			continue; // intra-cluster
		}
		if (Remapped.bFluid && !Remapped.bBlocked)
		{
			const TPair<int32, int32> Key(Remapped.FromNode, Remapped.ToNode);
			if (const int32* Existing = MergedFluidEdge.Find(Key))
			{
				NewEdges[*Existing].CapacityPerMinute += Remapped.CapacityPerMinute;
				continue;
			}
			MergedFluidEdge.Add(Key, NewEdges.Num());
		}
		NewEdges.Add(MoveTemp(Remapped));
	}

	Graph.Nodes = MoveTemp(NewNodes);
	Graph.Edges = MoveTemp(NewEdges);
	CollapsePassiveBufferLoops(Graph);
}
