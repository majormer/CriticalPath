// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

// Synthetic closed-form suite for the flow solver (FlowModel.md §6.1): every case has a
// hand-computable answer. Tolerances are loose-ish (0.5/min) because the damped fixed point
// approaches, not lands on, exact values.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CPFlowSolver.h"
#include "CPFlowAnalysis.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace CPFlowTest
{
FCPItemRate Rate(const TCHAR* Item, float PerMinute, ECPItemForm Form = ECPItemForm::Unknown)
{
	FCPItemRate Result;
	Result.Item.Name = Item;
	Result.Item.Form = Form;
	Result.RatePerMinute = PerMinute;
	return Result;
}

int32 AddNode(FCPFlowGraph& Graph, ECPFlowNodeKind Kind, const TCHAR* Label)
{
	FCPFlowNode Node;
	Node.Kind = Kind;
	Node.Label = Label;
	Graph.Nodes.Add(MoveTemp(Node));
	return Graph.Nodes.Num() - 1;
}

int32 AddEdge(FCPFlowGraph& Graph, int32 From, int32 To, float Capacity,
	int32 Port = INDEX_NONE, int32 MergerPriority = INDEX_NONE)
{
	FCPFlowEdge Edge;
	Edge.FromNode = From;
	Edge.ToNode = To;
	Edge.CapacityPerMinute = Capacity;
	Edge.FromPortIndex = Port;
	Edge.MergerInputPriority = MergerPriority;
	Graph.Edges.Add(MoveTemp(Edge));
	return Graph.Edges.Num() - 1;
}

FCPFlowSolveResult Solve(const FCPFlowGraph& Graph)
{
	FCPFlowSolveResult Result;
	FCPFlowSolver::Solve(Graph, FCPFlowSolveParams(), Result);
	return Result;
}

bool Near(float A, float B) { return FMath::Abs(A - B) <= 0.5f; }
} // namespace CPFlowTest

// --- A producer over a slow belt: the belt is the law. ---------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowChainTest, "CriticalPath.Flow.ChainCappedByBelt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowChainTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Iron"), 100.0f));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Iron"), 100.0f));
	AddEdge(G, P, C, 60.0f);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("delivered 60 (belt-capped)"), Near(R.DeliveredTo(C, TEXT("Iron")), 60.0f));
	return true;
}

// --- Even split with overflow redistribution: the small consumer takes 20, the residual of
// its lane flows to the hungry one. ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowSplitTest, "CriticalPath.Flow.EvenSplitRedistributes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowSplitTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Iron"), 100.0f));
	const int32 S = AddNode(G, ECPFlowNodeKind::Splitter, TEXT("S"));
	const int32 C1 = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C1"));
	G.Nodes[C1].Rates.Add(Rate(TEXT("Iron"), 100.0f));
	const int32 C2 = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C2"));
	G.Nodes[C2].Rates.Add(Rate(TEXT("Iron"), 20.0f));
	AddEdge(G, P, S, 780.0f);
	G.Nodes[S].OutRules.SetNum(2); // both Any
	AddEdge(G, S, C1, 780.0f, 0);
	AddEdge(G, S, C2, 780.0f, 1);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("C2 fully fed (20)"), Near(R.DeliveredTo(C2, TEXT("Iron")), 20.0f));
	TestTrue(TEXT("C1 gets the rest (80)"), Near(R.DeliveredTo(C1, TEXT("Iron")), 80.0f));
	return true;
}

// --- Smart splitter: filtered port feeds first, overflow takes only the refusal. -------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowSmartTest, "CriticalPath.Flow.SmartFilterThenOverflow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowSmartTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	const int32 S = AddNode(G, ECPFlowNodeKind::Splitter, TEXT("S"));
	const int32 C1 = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C1"));
	G.Nodes[C1].Rates.Add(Rate(TEXT("Iron"), 30.0f));
	const int32 C2 = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C2"));
	G.Nodes[C2].Rates.Add(Rate(TEXT("Iron"), 100.0f));
	AddEdge(G, P, S, 780.0f);
	G.Nodes[S].OutRules.SetNum(2);
	G.Nodes[S].OutRules[0].Rule = ECPSplitterOutRule::Filtered;
	G.Nodes[S].OutRules[0].Items.Add(TEXT("Iron"));
	G.Nodes[S].OutRules[1].Rule = ECPSplitterOutRule::Overflow;
	AddEdge(G, S, C1, 780.0f, 0);
	AddEdge(G, S, C2, 780.0f, 1);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("filtered port satisfied first (30)"), Near(R.DeliveredTo(C1, TEXT("Iron")), 30.0f));
	TestTrue(TEXT("overflow takes only the refusal (30)"), Near(R.DeliveredTo(C2, TEXT("Iron")), 30.0f));
	return true;
}

// --- Merger contention: equal producers share a constrained consumer proportional-fair. ------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowMergerTest, "CriticalPath.Flow.MergerProportionalFair",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowMergerTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P1 = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P1"));
	G.Nodes[P1].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	const int32 P2 = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P2"));
	G.Nodes[P2].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	const int32 M = AddNode(G, ECPFlowNodeKind::Merger, TEXT("M"));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Iron"), 90.0f));
	const int32 E1 = AddEdge(G, P1, M, 780.0f);
	const int32 E2 = AddEdge(G, P2, M, 780.0f);
	AddEdge(G, M, C, 780.0f);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("demand met (90)"), Near(R.DeliveredTo(C, TEXT("Iron")), 90.0f));
	float FlowE1 = 0.0f, FlowE2 = 0.0f;
	for (const FCPEdgeItemFlow& F : R.EdgeFlows)
	{
		if (F.EdgeIndex == E1) { FlowE1 = F.RatePerMinute; }
		if (F.EdgeIndex == E2) { FlowE2 = F.RatePerMinute; }
	}
	TestTrue(TEXT("admission fair (45/45)"), Near(FlowE1, 45.0f) && Near(FlowE2, 45.0f));
	return true;
}

// --- Storage is transparent at steady state: it feeds its downstream and absorbs nothing
// itself -- a dead-end tank must never out-prioritize consumers (the oil-tank-farm lesson). ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowStorageTest, "CriticalPath.Flow.StorageIsTransparent",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowStorageTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Computer"), 60.0f));
	const int32 S = AddNode(G, ECPFlowNodeKind::Splitter, TEXT("S"));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Computer"), 30.0f));
	const int32 Store = AddNode(G, ECPFlowNodeKind::Storage, TEXT("Store"));
	AddEdge(G, P, S, 780.0f);
	G.Nodes[S].OutRules.SetNum(2);
	AddEdge(G, S, C, 780.0f, 0);
	const int32 EStore = AddEdge(G, S, Store, 780.0f, 1);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("consumer fully fed (30)"), Near(R.DeliveredTo(C, TEXT("Computer")), 30.0f));
	float StoreFlow = 0.0f;
	for (const FCPEdgeItemFlow& F : R.EdgeFlows)
	{
		if (F.EdgeIndex == EStore) { StoreFlow = F.RatePerMinute; }
	}
	TestTrue(TEXT("dead-end storage absorbs nothing"), StoreFlow <= 1.0f);

	// Consumer BEHIND storage: the tank is a wire, not a competitor.
	const int32 C2 = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C2"));
	G.Nodes[C2].Rates.Add(Rate(TEXT("Computer"), 20.0f));
	AddEdge(G, Store, C2, 780.0f);
	const FCPFlowSolveResult R2 = Solve(G);
	TestTrue(TEXT("converged (through-storage)"), R2.bConverged);
	TestTrue(TEXT("downstream consumer fed through storage (20)"), Near(R2.DeliveredTo(C2, TEXT("Computer")), 20.0f));
	TestTrue(TEXT("first consumer still fed (30)"), Near(R2.DeliveredTo(C, TEXT("Computer")), 30.0f));
	return true;
}

// --- A blocked (head-capped / closed-valve) edge carries nothing, and says so honestly. ------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowBlockedTest, "CriticalPath.Flow.BlockedEdgeCarriesNothing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowBlockedTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Water"), 120.0f));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Water"), 120.0f));
	const int32 E = AddEdge(G, P, C, 300.0f);
	G.Edges[E].bFluid = true;
	G.Edges[E].bBlocked = true; // head-capped

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("nothing delivered"), Near(R.DeliveredTo(C, TEXT("Water")), 0.0f));
	return true;
}

// --- Belt loops terminate and still satisfy demand (damped fixed point). ---------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowCycleTest, "CriticalPath.Flow.CycleConverges",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowCycleTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	const int32 M = AddNode(G, ECPFlowNodeKind::Merger, TEXT("M"));
	const int32 S = AddNode(G, ECPFlowNodeKind::Splitter, TEXT("S"));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	AddEdge(G, P, M, 780.0f);
	AddEdge(G, M, S, 780.0f);
	G.Nodes[S].OutRules.SetNum(2);
	AddEdge(G, S, C, 780.0f, 0);
	AddEdge(G, S, M, 780.0f, 1); // the loop

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged within cap"), R.bConverged);
	TestTrue(TEXT("demand met through the loop (60)"), Near(R.DeliveredTo(C, TEXT("Iron")), 60.0f));
	return true;
}

// --- Priority merger: higher input groups take the constrained output first; equal inputs in a
// group retain the ordinary fair-share approximation. -----------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowPriorityMergerTest,
	"CriticalPath.Flow.PriorityMergerExhaustsHigherInputsFirst",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowPriorityMergerTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 High = AddNode(G, ECPFlowNodeKind::Producer, TEXT("High"));
	const int32 Medium = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Medium"));
	const int32 Low = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Low"));
	G.Nodes[High].Rates.Add(Rate(TEXT("Wire"), 60.0f));
	G.Nodes[Medium].Rates.Add(Rate(TEXT("Wire"), 60.0f));
	G.Nodes[Low].Rates.Add(Rate(TEXT("Wire"), 60.0f));
	const int32 Merger = AddNode(G, ECPFlowNodeKind::Merger, TEXT("Priority Merger"));
	G.Nodes[Merger].bPriorityMerger = true;
	const int32 Consumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Consumer"));
	G.Nodes[Consumer].Rates.Add(Rate(TEXT("Wire"), 90.0f));
	const int32 HighEdge = AddEdge(G, High, Merger, 780.0f, INDEX_NONE, 2);
	const int32 MediumEdge = AddEdge(G, Medium, Merger, 780.0f, INDEX_NONE, 1);
	const int32 LowEdge = AddEdge(G, Low, Merger, 780.0f, INDEX_NONE, 0);
	AddEdge(G, Merger, Consumer, 780.0f);

	const FCPFlowSolveResult Result = Solve(G);
	TestTrue(TEXT("priority merger converges"), Result.bConverged);
	TestTrue(TEXT("priority merger meets downstream demand"),
		Near(Result.DeliveredTo(Consumer, TEXT("Wire")), 90.0f));
	float HighFlow = 0.0f;
	float MediumFlow = 0.0f;
	float LowFlow = 0.0f;
	for (const FCPEdgeItemFlow& Flow : Result.EdgeFlows)
	{
		if (Flow.EdgeIndex == HighEdge) { HighFlow = Flow.RatePerMinute; }
		if (Flow.EdgeIndex == MediumEdge) { MediumFlow = Flow.RatePerMinute; }
		if (Flow.EdgeIndex == LowEdge) { LowFlow = Flow.RatePerMinute; }
	}
	TestTrue(TEXT("high input supplies its full 60 per minute"), Near(HighFlow, 60.0f));
	TestTrue(TEXT("medium input supplies the remaining 30 per minute"), Near(MediumFlow, 30.0f));
	TestTrue(TEXT("low input is held at zero"), Near(LowFlow, 0.0f));

	FCPFlowGraph EqualGraph;
	const int32 HighA = AddNode(EqualGraph, ECPFlowNodeKind::Producer, TEXT("High A"));
	const int32 HighB = AddNode(EqualGraph, ECPFlowNodeKind::Producer, TEXT("High B"));
	const int32 EqualLow = AddNode(EqualGraph, ECPFlowNodeKind::Producer, TEXT("Low"));
	EqualGraph.Nodes[HighA].Rates.Add(Rate(TEXT("Wire"), 60.0f));
	EqualGraph.Nodes[HighB].Rates.Add(Rate(TEXT("Wire"), 60.0f));
	EqualGraph.Nodes[EqualLow].Rates.Add(Rate(TEXT("Wire"), 60.0f));
	const int32 EqualMerger = AddNode(EqualGraph, ECPFlowNodeKind::Merger, TEXT("Priority Merger"));
	EqualGraph.Nodes[EqualMerger].bPriorityMerger = true;
	const int32 EqualConsumer = AddNode(EqualGraph, ECPFlowNodeKind::Consumer, TEXT("Consumer"));
	EqualGraph.Nodes[EqualConsumer].Rates.Add(Rate(TEXT("Wire"), 90.0f));
	const int32 HighAEdge = AddEdge(EqualGraph, HighA, EqualMerger, 780.0f, INDEX_NONE, 2);
	const int32 HighBEdge = AddEdge(EqualGraph, HighB, EqualMerger, 780.0f, INDEX_NONE, 2);
	const int32 EqualLowEdge = AddEdge(EqualGraph, EqualLow, EqualMerger, 780.0f, INDEX_NONE, 0);
	AddEdge(EqualGraph, EqualMerger, EqualConsumer, 780.0f);
	const FCPFlowSolveResult EqualResult = Solve(EqualGraph);
	float HighAFlow = 0.0f;
	float HighBFlow = 0.0f;
	float EqualLowFlow = 0.0f;
	for (const FCPEdgeItemFlow& Flow : EqualResult.EdgeFlows)
	{
		if (Flow.EdgeIndex == HighAEdge) { HighAFlow = Flow.RatePerMinute; }
		if (Flow.EdgeIndex == HighBEdge) { HighBFlow = Flow.RatePerMinute; }
		if (Flow.EdgeIndex == EqualLowEdge) { EqualLowFlow = Flow.RatePerMinute; }
	}
	TestTrue(TEXT("equal-priority case converges"), EqualResult.bConverged);
	TestTrue(TEXT("equal high inputs share the constrained 90 per minute"),
		Near(HighAFlow, 45.0f) && Near(HighBFlow, 45.0f));
	TestTrue(TEXT("lower group remains unused while equal high inputs suffice"), Near(EqualLowFlow, 0.0f));
	return true;
}

// --- A cycle in one disconnected domain must not damp every line or let a huge unrelated
// producer inflate the convergence tolerance for a small healthy component. ------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowComponentConvergenceTest,
	"CriticalPath.Flow.ComponentsConvergeIndependently",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowComponentConvergenceTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 Huge = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Unrelated huge producer"));
	G.Nodes[Huge].Rates.Add(Rate(TEXT("Other"), 1000000.0f));
	const int32 LoopA = AddNode(G, ECPFlowNodeKind::Passthrough, TEXT("Loop A"));
	const int32 LoopB = AddNode(G, ECPFlowNodeKind::Passthrough, TEXT("Loop B"));
	AddEdge(G, LoopA, LoopB, 60.0f);
	AddEdge(G, LoopB, LoopA, 60.0f);
	const int32 Producer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Small producer"));
	G.Nodes[Producer].Rates.Add(Rate(TEXT("Part"), 100.0f));
	const int32 Consumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Small consumer"));
	G.Nodes[Consumer].Rates.Add(Rate(TEXT("Part"), 100.0f));
	AddEdge(G, Producer, Consumer, 120.0f);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("disconnected components converge"), R.bConverged);
	TestTrue(TEXT("unrelated cycle and throughput do not truncate the small line"),
		Near(R.DeliveredTo(Consumer, TEXT("Part")), 100.0f));
	return true;
}

// --- Recipe input/output pairs are causal dependencies, not transport edges. Ordering them
// explicitly prevents a deep factory from advancing only one recipe tier per solver pass. ------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowRecipeDependencyOrderTest,
	"CriticalPath.Flow.RecipeDependenciesUseCausalOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowRecipeDependencyOrderTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	int32 PreviousProducer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Raw source"));
	G.Nodes[PreviousProducer].Rates.Add(Rate(TEXT("Tier 0"), 10.0f));
	constexpr int32 RecipeTiers = 110; // deliberately deeper than the default 96-pass guard
	for (int32 Tier = 0; Tier < RecipeTiers; ++Tier)
	{
		const FString InputItem = FString::Printf(TEXT("Tier %d"), Tier);
		const FString OutputItem = FString::Printf(TEXT("Tier %d"), Tier + 1);
		// Insert the output first so node-index order cannot accidentally satisfy the dependency.
		const int32 Output = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Recipe output"));
		G.Nodes[Output].Rates.Add(Rate(*OutputItem, 10.0f));
		const int32 Input = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Recipe input"));
		G.Nodes[Input].Rates.Add(Rate(*InputItem, 10.0f));
		G.Nodes[Output].PairedNodeIndex = Input;
		AddEdge(G, PreviousProducer, Input, 60.0f);
		PreviousProducer = Output;
	}
	const int32 FinalConsumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Objective consumer"));
	G.Nodes[FinalConsumer].Rates.Add(Rate(TEXT("Tier 110"), 10.0f));
	AddEdge(G, PreviousProducer, FinalConsumer, 60.0f);

	const FCPFlowSolveResult Result = Solve(G);
	TestTrue(TEXT("deep paired-recipe chain converges within the normal guard"), Result.bConverged);
	TestTrue(TEXT("supply reaches the objective through every configured recipe tier"),
		Near(Result.DeliveredTo(FinalConsumer, TEXT("Tier 110")), 10.0f));
	return true;
}

// --- Truncated graphs refuse to answer. -------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowTruncatedTest, "CriticalPath.Flow.TruncatedIsUnknown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowTruncatedTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	AddEdge(G, P, C, 780.0f);
	G.bTruncated = true;

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("truncated graph does not converge to numbers"), !R.bConverged);
	TestTrue(TEXT("no deliveries reported"), R.Deliveries.Num() == 0);
	return true;
}

// --- Regression (first megabase probe): multiple items sharing one edge into an unbounded
// acceptor must not fight over an acceptance budget — capacity sharing belongs to the supply
// pass alone. Previously churned forever (64-iteration cap, no fixpoint). --------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowSharedEdgeTest, "CriticalPath.Flow.MultiItemSharedEdgeConverges",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowSharedEdgeTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("A"), 30.0f));
	G.Nodes[P].Rates.Add(Rate(TEXT("B"), 30.0f));
	const int32 M = AddNode(G, ECPFlowNodeKind::Merger, TEXT("M"));
	const int32 Store = AddNode(G, ECPFlowNodeKind::Storage, TEXT("Store"));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("A"), 10.0f));
	AddEdge(G, P, M, 780.0f);
	const int32 S = AddNode(G, ECPFlowNodeKind::Splitter, TEXT("S"));
	AddEdge(G, M, S, 780.0f);
	G.Nodes[S].OutRules.SetNum(2);
	AddEdge(G, S, Store, 780.0f, 0);
	AddEdge(G, S, C, 780.0f, 1);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converges with two items on shared edges"), R.bConverged);
	TestTrue(TEXT("consumer's A demand met (10)"), Near(R.DeliveredTo(C, TEXT("A")), 10.0f));
	// Transparent storage: only real demand ships, the rest backs up at the producer.
	float TotalActual = 0.0f;
	for (const FCPEdgeItemFlow& F : R.ProducerActuals)
	{
		if (F.EdgeIndex == P) { TotalActual += F.RatePerMinute; }
	}
	TestTrue(TEXT("only demanded amount ships (10)"), Near(TotalActual, 10.0f));
	return true;
}

// --- Dual basis: a dead producer contributes zero to the CURRENT solve but its configured
// rate to the DESIGN solve — "is it running?" and "does the design close?" are different
// questions with different numbers. ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowDualBasisTest, "CriticalPath.Flow.DualBasisSolves",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowDualBasisTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P (unpowered)"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Iron"), 0.0f));         // current: dead
	G.Nodes[P].DesignRates.Add(Rate(TEXT("Iron"), 60.0f));  // design: 60/min
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	G.Nodes[C].DesignRates.Add(Rate(TEXT("Iron"), 60.0f));
	AddEdge(G, P, C, 780.0f);

	FCPFlowSolveResult Current;
	FCPFlowSolver::Solve(G, FCPFlowSolveParams(), Current);
	TestTrue(TEXT("current: converged"), Current.bConverged);
	TestTrue(TEXT("current: dead producer delivers 0"), Near(Current.DeliveredTo(C, TEXT("Iron")), 0.0f));

	FCPFlowSolveParams DesignParams;
	DesignParams.bUseDesignRates = true;
	FCPFlowSolveResult Design;
	FCPFlowSolver::Solve(G, DesignParams, Design);
	TestTrue(TEXT("design: converged"), Design.bConverged);
	TestTrue(TEXT("design: the design closes (60)"), Near(Design.DeliveredTo(C, TEXT("Iron")), 60.0f));
	return true;
}

// --- A manufacturer's output is not an independent source: half of its required input can
// sustain only half of its configured output. -------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowMachineTransformTest, "CriticalPath.Flow.MachineOutputFollowsInputs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowMachineTransformTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 IngotProducer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Ingot source"));
	G.Nodes[IngotProducer].Rates.Add(Rate(TEXT("Caterium Ingot"), 50.0f));
	const int32 MachineInput = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Quickwire Constructor input"));
	G.Nodes[MachineInput].Rates.Add(Rate(TEXT("Caterium Ingot"), 100.0f));
	const int32 MachineOutput = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Quickwire Constructor output"));
	G.Nodes[MachineOutput].Rates.Add(Rate(TEXT("Quickwire"), 500.0f));
	G.Nodes[MachineInput].PairedNodeIndex = MachineOutput;
	G.Nodes[MachineOutput].PairedNodeIndex = MachineInput;
	const int32 QuickwireConsumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Supercomputer Manufacturer"));
	G.Nodes[QuickwireConsumer].Rates.Add(Rate(TEXT("Quickwire"), 500.0f));
	AddEdge(G, IngotProducer, MachineInput, 780.0f);
	AddEdge(G, MachineOutput, QuickwireConsumer, 780.0f);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("half-fed constructor delivers half output"), Near(R.DeliveredTo(QuickwireConsumer, TEXT("Quickwire")), 250.0f));
	const FCPEdgeItemFlow* Sustainable = R.ProducerSustainable.FindByPredicate([MachineOutput](const FCPEdgeItemFlow& F)
	{
		return F.EdgeIndex == MachineOutput && F.ItemName == TEXT("Quickwire");
	});
	TestTrue(TEXT("sustainable output records input-supported 250/min"), Sustainable && Near(Sustainable->RatePerMinute, 250.0f));
	return true;
}

// --- Multiple ingredients use the least-supplied ratio; surplus on one input cannot conceal
// a shortage on another. ----------------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowMachineMultiInputTest, "CriticalPath.Flow.MachineUsesLimitingInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowMachineMultiInputTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 AProducer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("A source"));
	G.Nodes[AProducer].Rates.Add(Rate(TEXT("A"), 100.0f));
	const int32 BProducer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("B source"));
	G.Nodes[BProducer].Rates.Add(Rate(TEXT("B"), 25.0f));
	const int32 MachineInput = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Machine input"));
	G.Nodes[MachineInput].Rates.Add(Rate(TEXT("A"), 100.0f));
	G.Nodes[MachineInput].Rates.Add(Rate(TEXT("B"), 100.0f));
	const int32 MachineOutput = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Machine output"));
	G.Nodes[MachineOutput].Rates.Add(Rate(TEXT("Product"), 40.0f));
	G.Nodes[MachineInput].PairedNodeIndex = MachineOutput;
	G.Nodes[MachineOutput].PairedNodeIndex = MachineInput;
	const int32 ProductConsumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Product sink"));
	G.Nodes[ProductConsumer].Rates.Add(Rate(TEXT("Product"), 40.0f));
	AddEdge(G, AProducer, MachineInput, 780.0f);
	AddEdge(G, BProducer, MachineInput, 780.0f);
	AddEdge(G, MachineOutput, ProductConsumer, 780.0f);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("25 percent B supply limits output to 10/min"), Near(R.DeliveredTo(ProductConsumer, TEXT("Product")), 10.0f));
	return true;
}

// --- Phase C balance preserves the four distinct rates inside the relevant item domain. ------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowItemBalanceTest, "CriticalPath.Flow.ItemBalanceSeparatesCapacitySupportDelivery",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowItemBalanceTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 IngotProducer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Ingot source"));
	G.Nodes[IngotProducer].Rates.Add(Rate(TEXT("Caterium Ingot"), 50.0f));
	const int32 MachineInput = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Quickwire input"));
	G.Nodes[MachineInput].Rates.Add(Rate(TEXT("Caterium Ingot"), 100.0f));
	const int32 MachineOutput = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Quickwire output"));
	G.Nodes[MachineOutput].Rates.Add(Rate(TEXT("Quickwire"), 500.0f));
	G.Nodes[MachineOutput].ActorName = TEXT("Build_Constructor_1");
	G.Nodes[MachineOutput].Location = FVector(100.0f, 200.0f, 300.0f);
	G.Nodes[MachineOutput].BuildingItem.Name = TEXT("Constructor");
	G.Nodes[MachineOutput].BuildingItem.DescriptorClassPath = TEXT("/Game/Test/Desc_Constructor.Desc_Constructor_C");
	G.Nodes[MachineOutput].bMachineStateKnown = true;
	G.Nodes[MachineOutput].bProducing = false;
	G.Nodes[MachineOutput].bMissingInput = true;
	G.Nodes[MachineOutput].ProductivityPercent = 50.0f;
	G.Nodes[MachineOutput].PowerShardCount = 3;
	G.Nodes[MachineOutput].SomersloopCount = 1;
	G.Nodes[MachineInput].PairedNodeIndex = MachineOutput;
	G.Nodes[MachineOutput].PairedNodeIndex = MachineInput;
	const int32 QuickwireStorage = AddNode(G, ECPFlowNodeKind::Storage, TEXT("Quickwire buffer"));
	FCPBufferedItem Stock;
	Stock.Item.Name = TEXT("Quickwire");
	Stock.Amount = 1000.0f;
	G.Nodes[QuickwireStorage].Stocks.Add(Stock);
	const int32 QuickwireConsumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Quickwire demand"));
	G.Nodes[QuickwireConsumer].Rates.Add(Rate(TEXT("Quickwire"), 500.0f));
	G.Nodes[QuickwireConsumer].ActorName = TEXT("Build_Assembler_1");
	G.Nodes[QuickwireConsumer].Location = FVector(400.0f, 500.0f, 600.0f);
	G.Nodes[QuickwireConsumer].bMachineStateKnown = true;
	G.Nodes[QuickwireConsumer].bProducing = true;
	G.Nodes[QuickwireConsumer].ProductivityPercent = 40.0f;
	FCPBufferedItem ConsumerStock;
	ConsumerStock.Item.Name = TEXT("Quickwire");
	ConsumerStock.Amount = 25.0f;
	G.Nodes[QuickwireConsumer].Stocks.Add(ConsumerStock);
	AddEdge(G, IngotProducer, MachineInput, 780.0f);
	AddEdge(G, MachineOutput, QuickwireStorage, 200.0f); // delivery loses another 50/min
	AddEdge(G, QuickwireStorage, QuickwireConsumer, 200.0f);

	const FCPFlowSolveResult Current = Solve(G);
	const FCPFlowSolveResult Design = Solve(G);
	TArray<FCPItemBalance> Balances;
	FCPFlowAnalysis::BuildItemBalances(G, Current, Design, Balances);
	const FCPItemBalance* Quickwire = Balances.FindByPredicate([](const FCPItemBalance& Balance)
	{
		return Balance.Item.Name == TEXT("Quickwire") && Balance.ConsumerBuildings == 1;
	});
	TestNotNull(TEXT("Quickwire domain balance exists"), Quickwire);
	if (Quickwire)
	{
		TestTrue(TEXT("demand 500"), Near(Quickwire->Current.DemandPerMinute, 500.0f));
		TestTrue(TEXT("installed 500"), Near(Quickwire->Current.InstalledPerMinute, 500.0f));
		TestTrue(TEXT("input-supported 250"), Near(Quickwire->Current.SustainablePerMinute, 250.0f));
		TestTrue(TEXT("belt-delivered 200"), Near(Quickwire->Current.DeliveredPerMinute, 200.0f));
		TestTrue(TEXT("largest staged loss is upstream input support"),
			Quickwire->DesignConstraintKind == ECPFlowConstraintKind::InputSupport);
		TestFalse(TEXT("input constraint does not invent edge evidence"), Quickwire->DesignEdgeEvidence.IsSet());
		TestTrue(TEXT("one producer has machine-state evidence"), Quickwire->MachineStateProducerBuildings == 1);
		TestTrue(TEXT("producer is not currently producing"), Quickwire->ProducingProducerBuildings == 0);
		TestTrue(TEXT("producer is missing input"), Quickwire->MissingInputProducerBuildings == 1);
		TestTrue(TEXT("productivity estimates 250 output"), Near(Quickwire->EstimatedCurrentOutputPerMinute, 250.0f));
		const FCPBalanceLocation* ProducerLocation = Quickwire->Locations.FindByPredicate([](const FCPBalanceLocation& Location)
		{
			return Location.bProducer;
		});
		TestNotNull(TEXT("producer endpoint retained"), ProducerLocation);
		if (ProducerLocation)
		{
			TestTrue(TEXT("producer building descriptor retained"),
				ProducerLocation->BuildingItem.Name == TEXT("Constructor") &&
				ProducerLocation->BuildingItem.DescriptorClassPath == TEXT("/Game/Test/Desc_Constructor.Desc_Constructor_C"));
			TestTrue(TEXT("producer Power Shards retained"), ProducerLocation->PowerShardCount == 3);
			TestTrue(TEXT("producer Somersloop retained"), ProducerLocation->SomersloopCount == 1);
		}
		TestTrue(TEXT("one consumer has machine-state evidence"), Quickwire->MachineStateConsumerBuildings == 1);
		TestTrue(TEXT("consumer is currently producing"), Quickwire->ProducingConsumerBuildings == 1);
		TestTrue(TEXT("consumer state estimates 200 current use"), Near(Quickwire->EstimatedCurrentUsePerMinute, 200.0f));
		TestTrue(TEXT("consumer endpoint buffer retained"), Near(Quickwire->Locations[0].BufferedAmount, 25.0f));
		TestTrue(TEXT("reachable storage and endpoint buffers total 1025"), Near(Quickwire->BufferedAmount, 1025.0f));
		TestTrue(TEXT("buffer covers 3.42 deficit-minutes"), Near(Quickwire->BufferRunwayMinutes, 1025.0f / 300.0f));
		TestEqual(TEXT("producer and consumer locations retained"), Quickwire->Locations.Num(), 2);
		if (Quickwire->Locations.Num() == 2)
		{
			TestFalse(TEXT("consumer sorts first for locate"), Quickwire->Locations[0].bProducer);
			TestEqual(TEXT("consumer identity retained"), Quickwire->Locations[0].ActorName, FString(TEXT("Build_Assembler_1")));
			TestTrue(TEXT("consumer world location retained"), Quickwire->Locations[0].Location.Equals(FVector(400.0f, 500.0f, 600.0f)));
		}
	}
	return true;
}

// --- Constraint attribution distinguishes installed capacity from a proven saturated edge. ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowConstraintAttributionTest,
	"CriticalPath.Flow.ConstraintAttributionNamesCapacityAndEdge",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowConstraintAttributionTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;

	const int32 CapacityProducer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Capacity producer"));
	G.Nodes[CapacityProducer].Rates.Add(Rate(TEXT("Capacity Part"), 40.0f));
	const int32 CapacityConsumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Capacity consumer"));
	G.Nodes[CapacityConsumer].Rates.Add(Rate(TEXT("Capacity Part"), 100.0f));
	AddEdge(G, CapacityProducer, CapacityConsumer, 100.0f);

	const int32 EdgeProducer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Edge producer"));
	G.Nodes[EdgeProducer].Rates.Add(Rate(TEXT("Edge Part"), 100.0f));
	const int32 EdgeConsumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Edge consumer"));
	G.Nodes[EdgeConsumer].Rates.Add(Rate(TEXT("Edge Part"), 100.0f));
	const int32 LimitedEdge = AddEdge(G, EdgeProducer, EdgeConsumer, 60.0f);

	const FCPFlowSolveResult Solved = Solve(G);
	TArray<FCPItemBalance> Balances;
	FCPFlowAnalysis::BuildItemBalances(G, Solved, Solved, Balances);
	const FCPItemBalance* Capacity = Balances.FindByPredicate([](const FCPItemBalance& Balance)
	{
		return Balance.Item.Name == TEXT("Capacity Part");
	});
	const FCPItemBalance* Edge = Balances.FindByPredicate([](const FCPItemBalance& Balance)
	{
		return Balance.Item.Name == TEXT("Edge Part");
	});
	TestNotNull(TEXT("capacity-limited balance exists"), Capacity);
	TestNotNull(TEXT("edge-limited balance exists"), Edge);
	if (Capacity)
	{
		TestTrue(TEXT("installed capacity is the constraint"),
			Capacity->DesignConstraintKind == ECPFlowConstraintKind::InstalledCapacity);
		TestFalse(TEXT("capacity constraint has no edge evidence"), Capacity->DesignEdgeEvidence.IsSet());
	}
	if (Edge)
	{
		TestTrue(TEXT("saturated edge is the constraint"),
			Edge->DesignConstraintKind == ECPFlowConstraintKind::DeliveryEdge);
		TestTrue(TEXT("exact limited edge retained"), Edge->DesignEdgeEvidence.EdgeIndex == LimitedEdge);
		TestTrue(TEXT("edge item flow retained"), Near(Edge->DesignEdgeEvidence.ItemFlowPerMinute, 60.0f));
		TestTrue(TEXT("edge total flow retained"), Near(Edge->DesignEdgeEvidence.TotalFlowPerMinute, 60.0f));
		TestTrue(TEXT("edge capacity retained"), Near(Edge->DesignEdgeEvidence.CapacityPerMinute, 60.0f));
		TestTrue(TEXT("edge endpoint labels retained"),
			Edge->DesignEdgeEvidence.FromLabel == TEXT("Edge producer") &&
			Edge->DesignEdgeEvidence.ToLabel == TEXT("Edge consumer"));
	}
	return true;
}

// --- Marginal value is a full topology re-solve, not baseline ratio arithmetic. ---------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowMarginalResolveTest,
	"CriticalPath.Flow.MarginalValueResolvesMachineAndEdgeActions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowMarginalResolveTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	{
		FCPFlowGraph G;
		const int32 Producer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Constructor"));
		G.Nodes[Producer].ActorName = TEXT("Constructor_1");
		G.Nodes[Producer].Rates.Add(Rate(TEXT("Part"), 40.0f));
		const int32 Consumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Demand"));
		G.Nodes[Consumer].ActorName = TEXT("Consumer_1");
		G.Nodes[Consumer].Rates.Add(Rate(TEXT("Part"), 100.0f));
		AddEdge(G, Producer, Consumer, 60.0f); // +1 machine reaches 80 capacity, but belt stops at 60
		const int32 OrphanConsumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Missing producer"));
		G.Nodes[OrphanConsumer].ActorName = TEXT("OrphanConsumer_1");
		G.Nodes[OrphanConsumer].Rates.Add(Rate(TEXT("Orphan part"), 10.0f));

		FCPFlowSolveParams DesignParams;
		DesignParams.bUseDesignRates = true;
		FCPFlowSolveResult Baseline;
		FCPFlowSolver::Solve(G, DesignParams, Baseline);
		FCPAnalysisResult Result;
		FCPFlowAnalysis::BuildItemBalances(G, Baseline, Baseline, Result.ItemBalances);
		TestTrue(TEXT("machine scenario has two balances"), Result.ItemBalances.Num() == 2);
		const FCPItemBalance* PartBalance = Result.ItemBalances.FindByPredicate([](const FCPItemBalance& Balance)
		{
			return Balance.Item.Name == TEXT("Part");
		});
		const FCPItemBalance* OrphanBalance = Result.ItemBalances.FindByPredicate([](const FCPItemBalance& Balance)
		{
			return Balance.Item.Name == TEXT("Orphan part");
		});
		TestTrue(TEXT("machine balance found"), PartBalance != nullptr);
		TestTrue(TEXT("zero-installed orphan balance found"), OrphanBalance != nullptr);
		FCPObjectiveReport& Objective = Result.Objectives.AddDefaulted_GetRef();
		FCPPartReport& OrphanPart = Objective.Parts.AddDefaulted_GetRef();
		OrphanPart.Limiter.Item.Name = TEXT("Orphan part");
		OrphanPart.Limiter.Ratio = 0.0f;
		OrphanPart.Limiter.bSolverDerived = true;
		OrphanPart.Limiter.ConstraintKind = ECPFlowConstraintKind::InstalledCapacity;
		OrphanPart.Limiter.DomainId = OrphanBalance ? OrphanBalance->DomainId : INDEX_NONE;
		FCPPartReport& Part = Objective.Parts.AddDefaulted_GetRef();
		Part.Limiter.Item.Name = TEXT("Part");
		Part.Limiter.Ratio = 0.4f;
		Part.Limiter.bSolverDerived = true;
		Part.Limiter.ConstraintKind = ECPFlowConstraintKind::InstalledCapacity;
		Part.Limiter.DomainId = PartBalance ? PartBalance->DomainId : INDEX_NONE;
		Part.Limiter.MachineName = TEXT("Constructor");
		FCPFlowAnalysis::ApplyPrimaryMarginalValue(G, Baseline, Result);

		TestTrue(TEXT("zero-installed limiter does not consume marginal solve"),
			Result.Objectives[0].Parts[0].Limiter.MarginalAction == ECPMarginalActionKind::None);
		const FCPLimiter& Marginal = Result.Objectives[0].Parts[1].Limiter;
		TestTrue(TEXT("machine action selected"), Marginal.MarginalAction == ECPMarginalActionKind::AddAverageMachine);
		TestTrue(TEXT("machine marginal is solver-derived"), Marginal.bMarginalSolverDerived);
		TestTrue(TEXT("machine output is forty per minute"), Near(Marginal.PerMachinePerMinute, 40.0f));
		TestTrue(TEXT("downstream belt caps exact marginal delivery at sixty"), Near(Marginal.MarginalDeliveredPerMinute, 60.0f));
		TestTrue(TEXT("exact machine marginal ratio is sixty percent"),
			FMath::IsNearlyEqual(Marginal.MarginalRatio, 0.6f, 0.01f));
	}
	{
		FCPAnalysisResult Result;
		FCPPartReport& Part = Result.Objectives.AddDefaulted_GetRef().Parts.AddDefaulted_GetRef();
		Part.Status = ECPNodeStatus::Fulfilled;
		Part.Limiter.Item.Name = TEXT("Stale limiter");
		Part.Limiter.Ratio = 0.0f;
		Part.Limiter.bSolverDerived = true;
		FCPFlowAnalysis::ApplySolverPathInterpretation(FCPFactorySnapshot(), TArray<FCPItemBalance>(), Result);
		TestTrue(TEXT("fulfilled part discards stale limiter evidence"), !Part.Limiter.IsSet());
	}
	{
		FCPAnalysisResult Result;
		FCPPartReport& Part = Result.Objectives.AddDefaulted_GetRef().Parts.AddDefaulted_GetRef();
		Part.Item.Name = TEXT("Resolved part");
		Part.Status = ECPNodeStatus::UnderSupplied;
		Part.Blocker.Reason = ECPBlockerReason::SupplyBelowDemand;
		Part.Limiter.Item.Name = TEXT("Stale limiter");
		Part.Limiter.bSolverDerived = true;
		FCPFactorySnapshot Snapshot;
		FCPProductRow& Product = Snapshot.Products.AddDefaulted_GetRef();
		Product.Item = Part.Item;
		Product.ConfiguredBuildings = 1;
		Product.ProducingBuildings = 1;
		FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, TArray<FCPItemBalance>(), Result);
		TestTrue(TEXT("resolved rate blocker becomes fulfilled"), Part.Status == ECPNodeStatus::Fulfilled);
		TestTrue(TEXT("newly fulfilled part does not regenerate limiter evidence"), !Part.Limiter.IsSet());
	}
	{
		FCPFlowGraph G;
		const int32 Producer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Producer"));
		G.Nodes[Producer].ActorName = TEXT("Producer_1");
		G.Nodes[Producer].Rates.Add(Rate(TEXT("Part"), 200.0f));
		const int32 Consumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Consumer"));
		G.Nodes[Consumer].ActorName = TEXT("Consumer_1");
		G.Nodes[Consumer].Rates.Add(Rate(TEXT("Part"), 200.0f));
		AddEdge(G, Producer, Consumer, 60.0f);

		FCPFlowSolveParams DesignParams;
		DesignParams.bUseDesignRates = true;
		FCPFlowSolveResult Baseline;
		FCPFlowSolver::Solve(G, DesignParams, Baseline);
		FCPAnalysisResult Result;
		FCPFlowAnalysis::BuildItemBalances(G, Baseline, Baseline, Result.ItemBalances);
		TestTrue(TEXT("edge scenario has one balance"), Result.ItemBalances.Num() == 1);
		FCPPartReport& Part = Result.Objectives.AddDefaulted_GetRef().Parts.AddDefaulted_GetRef();
		Part.Limiter.Item.Name = TEXT("Part");
		Part.Limiter.Ratio = 0.3f;
		Part.Limiter.bSolverDerived = true;
		Part.Limiter.ConstraintKind = ECPFlowConstraintKind::DeliveryEdge;
		Part.Limiter.DomainId = Result.ItemBalances[0].DomainId;
		Part.Limiter.EdgeEvidence = Result.ItemBalances[0].DesignEdgeEvidence;
		FCPFlowAnalysis::ApplyPrimaryMarginalValue(G, Baseline, Result);

		const FCPLimiter& Marginal = Result.Objectives[0].Parts[0].Limiter;
		TestTrue(TEXT("edge action selected"), Marginal.MarginalAction == ECPMarginalActionKind::UpgradeEdge);
		TestTrue(TEXT("edge marginal is solver-derived"), Marginal.bMarginalSolverDerived);
		TestTrue(TEXT("Mk.1 belt advances to one hundred twenty"), Near(Marginal.MarginalEdgeCapacityPerMinute, 120.0f));
		TestTrue(TEXT("upgraded belt delivers one hundred twenty"), Near(Marginal.MarginalDeliveredPerMinute, 120.0f));
		TestTrue(TEXT("exact edge marginal ratio is sixty percent"),
			FMath::IsNearlyEqual(Marginal.MarginalRatio, 0.6f, 0.01f));
	}
	return true;
}

// --- Item sufficiency aggregates disjoint solver domains once and preserves current/design
// evidence instead of summing overlapping legacy network walks. --------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPSolverSufficiencyDomainsTest,
	"CriticalPath.Flow.SolverSufficiencyUsesDisjointDomains",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPSolverSufficiencyDomainsTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P1 = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Line 1 producer"));
	G.Nodes[P1].Rates.Add(Rate(TEXT("Part"), 20.0f));
	G.Nodes[P1].DesignRates.Add(Rate(TEXT("Part"), 40.0f));
	const int32 C1 = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Line 1 consumer"));
	G.Nodes[C1].Rates.Add(Rate(TEXT("Part"), 25.0f));
	G.Nodes[C1].DesignRates.Add(Rate(TEXT("Part"), 50.0f));
	AddEdge(G, P1, C1, 100.0f);

	const int32 P2 = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Line 2 producer"));
	G.Nodes[P2].Rates.Add(Rate(TEXT("Part"), 50.0f));
	G.Nodes[P2].DesignRates.Add(Rate(TEXT("Part"), 100.0f));
	const int32 C2 = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Line 2 consumer"));
	G.Nodes[C2].Rates.Add(Rate(TEXT("Part"), 25.0f));
	G.Nodes[C2].DesignRates.Add(Rate(TEXT("Part"), 50.0f));
	AddEdge(G, P2, C2, 100.0f);

	FCPFlowSolveResult Current;
	FCPFlowSolver::Solve(G, FCPFlowSolveParams(), Current);
	FCPFlowSolveParams DesignParams;
	DesignParams.bUseDesignRates = true;
	FCPFlowSolveResult Design;
	FCPFlowSolver::Solve(G, DesignParams, Design);
	TArray<FCPItemBalance> Balances;
	FCPFlowAnalysis::BuildItemBalances(G, Current, Design, Balances);
	TArray<FCPItemSufficiency> Sufficiency;
	FCPFlowAnalysis::BuildSolverSufficiency(Balances, Sufficiency);

	TestTrue(TEXT("two disjoint balances"), Balances.Num() == 2);
	TestTrue(TEXT("one item summary"), Sufficiency.Num() == 1);
	if (Sufficiency.Num() == 1)
	{
		const FCPItemSufficiency& Part = Sufficiency[0];
		TestTrue(TEXT("summary is solver-derived"), Part.bSolverDerived);
		TestTrue(TEXT("two consumers counted once"), Part.ConsumerBuildings == 2);
		TestTrue(TEXT("two domains counted once"), Part.NetworkCount == 2);
		TestTrue(TEXT("current is known"), Part.Current.bKnown);
		TestTrue(TEXT("current demand aggregates to 50"), Near(Part.Current.DemandPerMinute, 50.0f));
		TestTrue(TEXT("current delivery aggregates to 45"), Near(Part.Current.DeliveredPerMinute, 45.0f));
		TestTrue(TEXT("current aggregate ratio is 90 percent"), Near(Part.Current.AggregateRatio, 0.9f));
		TestTrue(TEXT("current limiting domain delivers 20"), Near(Part.Current.LimitingDomainDeliveredPerMinute, 20.0f));
		TestTrue(TEXT("current limiting domain demands 25"), Near(Part.Current.LimitingDomainDemandPerMinute, 25.0f));
		TestTrue(TEXT("current limiting ratio is 80 percent"), Near(Part.Current.LimitingDomainRatio, 0.8f));
		TestTrue(TEXT("design is known"), Part.Design.bKnown);
		TestTrue(TEXT("design demand aggregates to 100"), Near(Part.Design.DemandPerMinute, 100.0f));
		TestTrue(TEXT("design delivery aggregates to 90"), Near(Part.Design.DeliveredPerMinute, 90.0f));
		TestTrue(TEXT("legacy demand aliases design"), Near(Part.ConfiguredDemandPerMinute, 100.0f));
		TestTrue(TEXT("legacy supply aliases design delivery"), Near(Part.ConnectedSupplyPerMinute, 90.0f));
		TestTrue(TEXT("legacy limiter aliases design"), Near(Part.LimitingNetworkRatio, 0.8f));
		TestTrue(TEXT("legacy known aliases design"), Part.bKnown);
	}
	return true;
}

// --- Scheduled transport is topology-only today. A converged solve crossing it must not become
// a definitive public rate claim until measured throughput exists. -------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPSolverSufficiencyUnknownTransportTest,
	"CriticalPath.Flow.SolverSufficiencyMarksUnknownTransport",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPSolverSufficiencyUnknownTransportTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 Producer = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Station source"));
	G.Nodes[Producer].Rates.Add(Rate(TEXT("Part"), 60.0f));
	const int32 Consumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Station sink"));
	G.Nodes[Consumer].Rates.Add(Rate(TEXT("Part"), 60.0f));
	const int32 EdgeIndex = AddEdge(G, Producer, Consumer, TNumericLimits<float>::Max());
	G.Edges[EdgeIndex].bTransport = true;

	const FCPFlowSolveResult Current = Solve(G);
	const FCPFlowSolveResult Design = Solve(G);
	TArray<FCPItemBalance> Balances;
	FCPFlowAnalysis::BuildItemBalances(G, Current, Design, Balances);
	TArray<FCPItemSufficiency> Sufficiency;
	FCPFlowAnalysis::BuildSolverSufficiency(Balances, Sufficiency);

	TestTrue(TEXT("one transport balance"), Balances.Num() == 1);
	TestTrue(TEXT("transport topology retained"), Balances.Num() == 1 && Balances[0].bUsesTransport);
	TestTrue(TEXT("one item summary"), Sufficiency.Num() == 1);
	if (Sufficiency.Num() == 1)
	{
		const FCPItemSufficiency& Part = Sufficiency[0];
		TestTrue(TEXT("current transport rate is unknown"), !Part.Current.bKnown);
		TestTrue(TEXT("current has one unknown domain"), Part.Current.UnknownDomainCount == 1);
		TestTrue(TEXT("design transport rate is unknown"), !Part.Design.bKnown);
		TestTrue(TEXT("design has one unknown domain"), Part.Design.UnknownDomainCount == 1);
		TestTrue(TEXT("legacy contract remains unknown"), !Part.bKnown);
	}
	return true;
}

// --- Phase C Path interpretation follows the configured dependency tree but takes its rate
// verdict and limiter from solved, disjoint domains. --------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPSolverPathInterpretationTest,
	"CriticalPath.Flow.SolverPathChoosesDeepestDeficientDomain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPSolverPathInterpretationTest::RunTest(const FString& Parameters)
{
	FCPFactorySnapshot Snapshot;
	FCPProductRow RootProduct;
	RootProduct.Item.Name = TEXT("Root");
	RootProduct.ConfiguredBuildings = 1;
	RootProduct.MissingInputBuildings = 1;
	Snapshot.Products.Add(RootProduct);
	FCPProductRow IngredientProduct;
	IngredientProduct.Item.Name = TEXT("Ingredient");
	IngredientProduct.ConfiguredBuildings = 1;
	IngredientProduct.ProducingBuildings = 1;
	Snapshot.Products.Add(IngredientProduct);
	FCPRecipeEdge Recipe;
	FCPItemRate ProductRate;
	ProductRate.Item.Name = TEXT("Root");
	Recipe.Products.Add(ProductRate);
	FCPItemRate IngredientRate;
	IngredientRate.Item.Name = TEXT("Ingredient");
	Recipe.Ingredients.Add(IngredientRate);
	Snapshot.Edges.Add(Recipe);

	TArray<FCPItemBalance> Balances;
	FCPItemBalance RootBalance;
	RootBalance.Item.Name = TEXT("Root");
	RootBalance.DomainId = 10;
	RootBalance.ProducerBuildings = 1;
	RootBalance.ConsumerBuildings = 1;
	RootBalance.Current.bKnown = true;
	RootBalance.Current.SustainablePerMinute = 2.0f;
	RootBalance.Design.bKnown = true;
	RootBalance.Design.DemandPerMinute = 100.0f;
	RootBalance.Design.DeliveredPerMinute = 80.0f;
	FCPBalanceLocation RootMachine;
	RootMachine.bProducer = true;
	RootMachine.ActorName = TEXT("RootMachine");
	RootMachine.Label = TEXT("Assembler");
	RootBalance.Locations.Add(RootMachine);
	Balances.Add(RootBalance);
	FCPItemBalance IngredientBalance;
	IngredientBalance.Item.Name = TEXT("Ingredient");
	IngredientBalance.DomainId = 20;
	IngredientBalance.ProducerBuildings = 1;
	IngredientBalance.ConsumerBuildings = 1;
	IngredientBalance.Design.bKnown = true;
	IngredientBalance.Design.DemandPerMinute = 100.0f;
	IngredientBalance.Design.InstalledPerMinute = 80.0f;
	IngredientBalance.Design.SustainablePerMinute = 50.0f;
	IngredientBalance.Design.DeliveredPerMinute = 40.0f;
	IngredientBalance.DesignConstraintKind = ECPFlowConstraintKind::InputSupport;
	IngredientBalance.BufferRunwayMinutes = 5.0f;
	FCPBalanceLocation RootInput;
	RootInput.bProducer = false;
	RootInput.ActorName = TEXT("RootMachine");
	IngredientBalance.Locations.Add(RootInput);
	Balances.Add(IngredientBalance);
	FCPItemBalance UnrelatedIngredientBalance;
	UnrelatedIngredientBalance.Item.Name = TEXT("Ingredient");
	UnrelatedIngredientBalance.DomainId = 30;
	UnrelatedIngredientBalance.ProducerBuildings = 1;
	UnrelatedIngredientBalance.ConsumerBuildings = 1;
	UnrelatedIngredientBalance.Design.bKnown = true;
	UnrelatedIngredientBalance.Design.DemandPerMinute = 100.0f;
	UnrelatedIngredientBalance.Design.DeliveredPerMinute = 10.0f;
	FCPBalanceLocation OtherInput;
	OtherInput.bProducer = false;
	OtherInput.ActorName = TEXT("OtherMachine");
	UnrelatedIngredientBalance.Locations.Add(OtherInput);
	Balances.Add(UnrelatedIngredientBalance);

	FCPAnalysisResult Result;
	FCPObjectiveReport& Objective = Result.Objectives.AddDefaulted_GetRef();
	FCPPartReport& Part = Objective.Parts.AddDefaulted_GetRef();
	Part.Item.Name = TEXT("Root");
	Part.Remaining = 1000;
	Part.StillToProduce = 1000;
	Part.EffectiveRatePerMinute = 10.0f;
	Part.EtaMinutes = 100.0f;
	Part.Status = ECPNodeStatus::UnderSupplied;
	Part.Blocker.Reason = ECPBlockerReason::ConnectivityUnknown;
	FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, Balances, Result);

	const FCPPartReport& Solved = Result.Objectives[0].Parts[0];
	TestTrue(TEXT("blocker is solver-derived"), Solved.Blocker.bSolverDerived);
	TestTrue(TEXT("deep ingredient is blocker"), Solved.Blocker.Item.Name == TEXT("Ingredient"));
	TestTrue(TEXT("deep blocker is a solved supply deficit"), Solved.Blocker.Reason == ECPBlockerReason::SupplyBelowDemand);
	TestTrue(TEXT("deep blocker delivered 40"), CPFlowTest::Near(Solved.Blocker.SupplyPerMinute, 40.0f));
	TestTrue(TEXT("deep blocker demand 100"), CPFlowTest::Near(Solved.Blocker.DemandPerMinute, 100.0f));
	TestTrue(TEXT("blocker path has root and ingredient"), Solved.Blocker.Path.Num() == 2);
	TestTrue(TEXT("limiter is solver-derived"), Solved.Limiter.bSolverDerived);
	TestTrue(TEXT("deepest deficient domain wins limiter"), Solved.Limiter.Item.Name == TEXT("Ingredient"));
	TestTrue(TEXT("limiter ratio is 40 percent"), CPFlowTest::Near(Solved.Limiter.Ratio, 0.4f));
	TestTrue(TEXT("unrelated worse domain is excluded by machine identity"), Solved.Limiter.Ratio > 0.3f);
	TestTrue(TEXT("limiter domain retained"), Solved.Limiter.DomainId == 20);
	TestTrue(TEXT("limiter constraint retained"),
		Solved.Limiter.ConstraintKind == ECPFlowConstraintKind::InputSupport);
	TestTrue(TEXT("limiter installed rate retained"), CPFlowTest::Near(Solved.Limiter.InstalledPerMinute, 80.0f));
	TestTrue(TEXT("limiter sustainable rate retained"), CPFlowTest::Near(Solved.Limiter.SustainablePerMinute, 50.0f));
	TestTrue(TEXT("limiter runway comes from its domain"), CPFlowTest::Near(Solved.Limiter.BufferMinutes, 5.0f));
	TestTrue(TEXT("eta uses current rate through runway then sustainable rate"),
		CPFlowTest::Near(Solved.EtaMinutes, 480.0f));
	return true;
}

// A fulfilled intermediate may share its upstream network with unrelated hungry consumers.
// Once the intermediate receives everything this objective branch needs, the limiter walk must
// not continue through it and blame that shared upstream shortage.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPSolverPathStopsAtHealthyIntermediateTest,
	"CriticalPath.Flow.SolverPathStopsAtHealthyIntermediate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPSolverPathStopsAtHealthyIntermediateTest::RunTest(const FString& Parameters)
{
	FCPFactorySnapshot Snapshot;
	for (const TCHAR* Name : { TEXT("Root"), TEXT("Healthy Branch"), TEXT("Deficient Sibling"), TEXT("Unrelated Upstream") })
	{
		FCPProductRow& Product = Snapshot.Products.AddDefaulted_GetRef();
		Product.Item.Name = Name;
		Product.ConfiguredBuildings = 1;
		Product.ProducingBuildings = 1;
	}

	FCPRecipeEdge& RootRecipe = Snapshot.Edges.AddDefaulted_GetRef();
	RootRecipe.RecipeName = TEXT("Root Recipe");
	RootRecipe.Products.Add(CPFlowTest::Rate(TEXT("Root"), 100.0f));
	RootRecipe.Ingredients.Add(CPFlowTest::Rate(TEXT("Healthy Branch"), 100.0f));
	RootRecipe.Ingredients.Add(CPFlowTest::Rate(TEXT("Deficient Sibling"), 100.0f));
	FCPRecipeEdge& HealthyRecipe = Snapshot.Edges.AddDefaulted_GetRef();
	HealthyRecipe.RecipeName = TEXT("Healthy Recipe");
	HealthyRecipe.Products.Add(CPFlowTest::Rate(TEXT("Healthy Branch"), 100.0f));
	HealthyRecipe.Ingredients.Add(CPFlowTest::Rate(TEXT("Unrelated Upstream"), 100.0f));

	const auto AddBalance = [](TArray<FCPItemBalance>& Balances, const TCHAR* Item,
		int32 DomainId, float Delivered, float Demand, const TCHAR* ProducerActor,
		const TCHAR* ConsumerActor) -> FCPItemBalance&
	{
		FCPItemBalance& Balance = Balances.AddDefaulted_GetRef();
		Balance.Item.Name = Item;
		Balance.DomainId = DomainId;
		Balance.ProducerBuildings = 1;
		Balance.ConsumerBuildings = 1;
		Balance.Design.bKnown = true;
		Balance.Design.DemandPerMinute = Demand;
		Balance.Design.InstalledPerMinute = 100.0f;
		Balance.Design.SustainablePerMinute = Delivered;
		Balance.Design.DeliveredPerMinute = Delivered;
		FCPBalanceLocation& Producer = Balance.Locations.AddDefaulted_GetRef();
		Producer.bProducer = true;
		Producer.ActorName = ProducerActor;
		Producer.Label = TEXT("Producer");
		FCPBalanceLocation& Consumer = Balance.Locations.AddDefaulted_GetRef();
		Consumer.bProducer = false;
		Consumer.ActorName = ConsumerActor;
		return Balance;
	};

	TArray<FCPItemBalance> Balances;
	AddBalance(Balances, TEXT("Root"), 1, 80.0f, 100.0f, TEXT("RootMachine"), TEXT("Objective"));
	AddBalance(Balances, TEXT("Healthy Branch"), 2, 100.0f, 100.0f,
		TEXT("HealthyMachine"), TEXT("RootMachine"));
	AddBalance(Balances, TEXT("Deficient Sibling"), 3, 60.0f, 100.0f,
		TEXT("SiblingMachine"), TEXT("RootMachine"));
	AddBalance(Balances, TEXT("Unrelated Upstream"), 4, 10.0f, 100.0f,
		TEXT("UpstreamMachine"), TEXT("HealthyMachine"));

	FCPAnalysisResult Result;
	FCPPartReport& Part = Result.Objectives.AddDefaulted_GetRef().Parts.AddDefaulted_GetRef();
	Part.Item.Name = TEXT("Root");
	Part.Remaining = 1;
	Part.StillToProduce = 1;
	Part.Status = ECPNodeStatus::UnderSupplied;
	FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, Balances, Result);

	const FCPLimiter& Limiter = Result.Objectives[0].Parts[0].Limiter;
	TestTrue(TEXT("limiter is the deficient sibling"), Limiter.Item.Name == TEXT("Deficient Sibling"));
	TestTrue(TEXT("healthy branch upstream shortage is excluded"), Limiter.Ratio > 0.5f);
	TestTrue(TEXT("sibling ratio retained"), CPFlowTest::Near(Limiter.Ratio, 0.6f));
	TestTrue(TEXT("path stays on the constraining branch"),
		Limiter.Path.Num() == 2 && Limiter.Path[1].Name == TEXT("Deficient Sibling"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPSolverPathTrustsOperatingProducerTest,
	"CriticalPath.Flow.SolverPathTrustsOperatingProducer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPSolverPathTrustsOperatingProducerTest::RunTest(const FString& Parameters)
{
	FCPFactorySnapshot Snapshot;
	FCPProductRow& Root = Snapshot.Products.AddDefaulted_GetRef();
	Root.Item.Name = TEXT("Objective Part");
	Root.ConfiguredBuildings = 1;
	Root.MissingInputBuildings = 1;
	FCPProductRow& Ingredient = Snapshot.Products.AddDefaulted_GetRef();
	Ingredient.Item.Name = TEXT("Operating Ingredient");
	Ingredient.ConfiguredBuildings = 2;
	Ingredient.ProducingBuildings = 2;

	FCPRecipeEdge& Recipe = Snapshot.Edges.AddDefaulted_GetRef();
	Recipe.Products.Add(CPFlowTest::Rate(TEXT("Objective Part"), 1.0f));
	Recipe.Ingredients.Add(CPFlowTest::Rate(TEXT("Operating Ingredient"), 10.0f));

	TArray<FCPItemBalance> Balances;
	FCPItemBalance& RootBalance = Balances.AddDefaulted_GetRef();
	RootBalance.Item.Name = TEXT("Objective Part");
	RootBalance.DomainId = 1;
	RootBalance.ProducerBuildings = 1;
	FCPBalanceLocation& RootProducer = RootBalance.Locations.AddDefaulted_GetRef();
	RootProducer.bProducer = true;
	RootProducer.ActorName = TEXT("ObjectiveMachine");

	FCPItemBalance& IngredientBalance = Balances.AddDefaulted_GetRef();
	IngredientBalance.Item.Name = TEXT("Operating Ingredient");
	IngredientBalance.DomainId = 2;
	IngredientBalance.ProducerBuildings = 2;
	IngredientBalance.ConsumerBuildings = 1;
	IngredientBalance.Design.bKnown = true;
	IngredientBalance.Design.DemandPerMinute = 10.0f;
	IngredientBalance.Design.InstalledPerMinute = 20.0f;
	IngredientBalance.Design.SustainablePerMinute = 4.0f;
	IngredientBalance.Design.DeliveredPerMinute = 4.0f;
	FCPBalanceLocation& IngredientConsumer = IngredientBalance.Locations.AddDefaulted_GetRef();
	IngredientConsumer.ActorName = TEXT("ObjectiveMachine");
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FCPBalanceLocation& Producer = IngredientBalance.Locations.AddDefaulted_GetRef();
		Producer.bProducer = true;
		Producer.ActorName = FString::Printf(TEXT("IngredientMachine%d"), Index);
		Producer.bMachineStateKnown = true;
		Producer.bProducing = true;
	}

	FCPAnalysisResult Result;
	FCPPartReport& Part = Result.Objectives.AddDefaulted_GetRef().Parts.AddDefaulted_GetRef();
	Part.Item.Name = TEXT("Objective Part");
	Part.Remaining = 1;
	Part.StillToProduce = 1;
	Part.Status = ECPNodeStatus::UnderSupplied;
	Part.Blocker.Reason = ECPBlockerReason::ConnectivityUnknown;
	FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, Balances, Result);

	const FCPPartReport& Interpreted = Result.Objectives[0].Parts[0];
	TestTrue(TEXT("operating ingredient is not a blocker"),
		Interpreted.Blocker.Item.Name != TEXT("Operating Ingredient"));
	TestTrue(TEXT("operating ingredient is not a limiter"), !Interpreted.Limiter.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPSolverPathDisconnectedTest,
	"CriticalPath.Flow.SolverPathFindsDisconnectedConsumerDomain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPSolverPathDisconnectedTest::RunTest(const FString& Parameters)
{
	FCPFactorySnapshot Snapshot;
	FCPProductRow Product;
	Product.Item.Name = TEXT("Part");
	Product.ConfiguredBuildings = 1;
	Snapshot.Products.Add(Product);

	TArray<FCPItemBalance> Balances;
	FCPItemBalance ProducerDomain;
	ProducerDomain.Item.Name = TEXT("Part");
	ProducerDomain.DomainId = 1;
	ProducerDomain.ProducerBuildings = 1;
	ProducerDomain.Design.bKnown = true;
	ProducerDomain.Design.InstalledPerMinute = 50.0f;
	Balances.Add(ProducerDomain);
	FCPItemBalance ConsumerDomain;
	ConsumerDomain.Item.Name = TEXT("Part");
	ConsumerDomain.DomainId = 2;
	ConsumerDomain.ConsumerBuildings = 1;
	ConsumerDomain.Design.bKnown = true;
	ConsumerDomain.Design.DemandPerMinute = 50.0f;
	Balances.Add(ConsumerDomain);

	FCPAnalysisResult Result;
	FCPPartReport& Part = Result.Objectives.AddDefaulted_GetRef().Parts.AddDefaulted_GetRef();
	Part.Item.Name = TEXT("Part");
	Part.Remaining = 1;
	Part.StillToProduce = 1;
	Part.Status = ECPNodeStatus::UnderSupplied;
	Part.Blocker.Reason = ECPBlockerReason::ProducerNotConnected;
	FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, Balances, Result);

	const FCPBlocker& Blocker = Result.Objectives[0].Parts[0].Blocker;
	TestTrue(TEXT("disconnection is solver-derived"), Blocker.bSolverDerived);
	TestTrue(TEXT("producer-only and consumer-only domains prove disconnection"),
		Blocker.Reason == ECPBlockerReason::ProducerNotConnected);
	TestTrue(TEXT("disconnected domain has zero delivery"), CPFlowTest::Near(Blocker.SupplyPerMinute, 0.0f));
	TestTrue(TEXT("disconnected domain retains demand"), CPFlowTest::Near(Blocker.DemandPerMinute, 50.0f));
	TestTrue(TEXT("consumer-only domain is not presented as a limiter"),
		!Result.Objectives[0].Parts[0].Limiter.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPSolverPathUnknownTransportTest,
	"CriticalPath.Flow.SolverPathKeepsUnknownTransportHonest",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPSolverPathUnknownTransportTest::RunTest(const FString& Parameters)
{
	FCPFactorySnapshot Snapshot;
	FCPProductRow Product;
	Product.Item.Name = TEXT("Part");
	Product.ConfiguredBuildings = 1;
	Snapshot.Products.Add(Product);
	FCPItemBalance Balance;
	Balance.Item.Name = TEXT("Part");
	Balance.ProducerBuildings = 1;
	Balance.ConsumerBuildings = 1;
	Balance.Design.bKnown = true;
	Balance.Design.DemandPerMinute = 60.0f;
	Balance.Design.DeliveredPerMinute = 60.0f;
	Balance.bUsesTransport = true;
	Balance.bTransportRateKnown = false;

	FCPAnalysisResult Result;
	FCPPartReport& Part = Result.Objectives.AddDefaulted_GetRef().Parts.AddDefaulted_GetRef();
	Part.Item.Name = TEXT("Part");
	Part.Remaining = 1;
	Part.StillToProduce = 1;
	Part.Status = ECPNodeStatus::UnderSupplied;
	Part.Blocker.Reason = ECPBlockerReason::ConnectivityUnknown;
	TArray<FCPItemBalance> Balances;
	Balances.Add(Balance);
	FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, Balances, Result);

	const FCPPartReport& Solved = Result.Objectives[0].Parts[0];
	TestTrue(TEXT("transport blocker remains unknown"), Solved.Blocker.Reason == ECPBlockerReason::ConnectivityUnknown);
	TestTrue(TEXT("unknown blocker is solver-derived"), Solved.Blocker.bSolverDerived);
	TestTrue(TEXT("unknown cause specifically names unmeasured transport"),
		Solved.Blocker.UnknownReason == ECPFlowUnknownReason::TransportRateUnknown);
	TestTrue(TEXT("unknown transport is not named as a limiter"), !Solved.Limiter.IsSet());
	TestTrue(TEXT("unknown transport is disclosed on limiter walk"), Solved.Limiter.UnknownLinks == 1);

	// A failed solve outranks the transport qualification; do not claim the route rate is the
	// only unknown when the numerical result itself is incomplete.
	Balances[0].Design.bKnown = false;
	FCPAnalysisResult IncompleteResult;
	FCPPartReport& IncompletePart = IncompleteResult.Objectives.AddDefaulted_GetRef().Parts.AddDefaulted_GetRef();
	IncompletePart.Item.Name = TEXT("Part");
	IncompletePart.Remaining = 1;
	IncompletePart.StillToProduce = 1;
	IncompletePart.Status = ECPNodeStatus::UnderSupplied;
	IncompletePart.Blocker.Reason = ECPBlockerReason::ConnectivityUnknown;
	FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, Balances, IncompleteResult);
	TestTrue(TEXT("incomplete solve does not masquerade as transport-only unknown"),
		IncompleteResult.Objectives[0].Parts[0].Blocker.UnknownReason ==
		ECPFlowUnknownReason::IncompleteOrNonConverged);
	return true;
}

// --- A fluid producer may expose an incidental conveyor output on the same captured machine
// node. Descriptor-backed form must choose pipes, not whichever edge was encountered first. ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFluidBalanceFormTest, "CriticalPath.Flow.FluidBalanceUsesPipeDomains",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFluidBalanceFormTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P1 = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Refinery 1"));
	const int32 P2 = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Refinery 2"));
	const int32 Junction = AddNode(G, ECPFlowNodeKind::Passthrough, TEXT("Pipe Junction"));
	const int32 Consumer = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Packager"));
	const int32 IncidentalBelt = AddNode(G, ECPFlowNodeKind::Passthrough, TEXT("Unused conveyor output"));
	G.Nodes[P1].Rates.Add(Rate(TEXT("Turbofuel"), 20.0f, ECPItemForm::Fluid));
	G.Nodes[P1].DesignRates = G.Nodes[P1].Rates;
	G.Nodes[P2].Rates.Add(Rate(TEXT("Turbofuel"), 20.0f, ECPItemForm::Fluid));
	G.Nodes[P2].DesignRates = G.Nodes[P2].Rates;
	G.Nodes[Consumer].Rates.Add(Rate(TEXT("Turbofuel"), 40.0f, ECPItemForm::Fluid));
	G.Nodes[Consumer].DesignRates = G.Nodes[Consumer].Rates;

	// Deliberately first: the old heuristic saw this belt edge and classified Turbofuel solid.
	AddEdge(G, P1, IncidentalBelt, 60.0f);
	G.Edges[AddEdge(G, P1, Junction, 300.0f)].bFluid = true;
	G.Edges[AddEdge(G, P2, Junction, 300.0f)].bFluid = true;
	G.Edges[AddEdge(G, Junction, Consumer, 300.0f)].bFluid = true;

	const FCPFlowSolveResult Current = Solve(G);
	const FCPFlowSolveResult Design = Solve(G);
	TArray<FCPItemBalance> Balances;
	FCPFlowAnalysis::BuildItemBalances(G, Current, Design, Balances);
	const TArray<FCPItemBalance> TurboBalances = Balances.FilterByPredicate([](const FCPItemBalance& Balance)
	{
		return Balance.Item.Name == TEXT("Turbofuel");
	});
	TestTrue(TEXT("one Turbofuel pipe domain"), TurboBalances.Num() == 1);
	if (TurboBalances.Num() == 1)
	{
		TestTrue(TEXT("domain is fluid"), TurboBalances[0].bFluid);
		TestTrue(TEXT("both refinery producers retained"), TurboBalances[0].ProducerBuildings == 2);
		TestTrue(TEXT("packager consumer retained"), TurboBalances[0].ConsumerBuildings == 1);
	}
	return true;
}

// --- A slow (multi-hop) path must still bootstrap when the fast path cannot meet demand:
// offer-proportional acceptance alone locks the laggard at zero forever. -----------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowSlowPathTest, "CriticalPath.Flow.SlowPathBootstraps",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowSlowPathTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P1 = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P1 short"));
	G.Nodes[P1].Rates.Add(Rate(TEXT("Oil"), 50.0f));
	const int32 P2 = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P2 long"));
	G.Nodes[P2].Rates.Add(Rate(TEXT("Oil"), 100.0f));
	const int32 A = AddNode(G, ECPFlowNodeKind::Passthrough, TEXT("A"));
	const int32 B = AddNode(G, ECPFlowNodeKind::TransportPort, TEXT("B"));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Oil"), 150.0f));
	AddEdge(G, P1, C, 600.0f);
	AddEdge(G, P2, A, 600.0f);
	AddEdge(G, A, B, 600.0f);
	AddEdge(G, B, C, 600.0f);

	const FCPFlowSolveResult R = Solve(G);
	TestTrue(TEXT("converged"), R.bConverged);
	TestTrue(TEXT("both paths deliver (150)"), Near(R.DeliveredTo(C, TEXT("Oil")), 150.0f));
	return true;
}

// --- Contraction: junction chains collapse to one node, parallel pipes sum capacity, and the
// collapsed graph still solves correctly. --------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowContractionTest, "CriticalPath.Flow.ContractionMergesJunctions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowContractionTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Water"), 300.0f));
	const int32 J1 = AddNode(G, ECPFlowNodeKind::Passthrough, TEXT("Pipe Junction"));
	const int32 J2 = AddNode(G, ECPFlowNodeKind::Passthrough, TEXT("Pipe Junction"));
	G.Nodes[J1].bContractible = true;
	G.Nodes[J2].bContractible = true;
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Water"), 300.0f));
	// P -> J1, bidirectional J1<->J2 (two parallel pipes each way), J2 -> C.
	G.Edges[AddEdge(G, P, J1, 300.0f)].bFluid = true;
	G.Edges[AddEdge(G, J1, J2, 150.0f)].bFluid = true;
	G.Edges[AddEdge(G, J1, J2, 150.0f)].bFluid = true;
	G.Edges[AddEdge(G, J2, J1, 150.0f)].bFluid = true;
	G.Edges[AddEdge(G, J2, J1, 150.0f)].bFluid = true;
	G.Edges[AddEdge(G, J2, C, 300.0f)].bFluid = true;

	FCPFlowSolver::CollapseContractibleClusters(G);
	TestEqual(TEXT("junction pair merged"), G.Nodes.Num(), 3);
	TestEqual(TEXT("intra-cluster edges dropped"), G.Edges.Num(), 2);

	FCPFlowSolveResult Result;
	FCPFlowSolver::Solve(G, FCPFlowSolveParams(), Result);
	TestTrue(TEXT("converged"), Result.bConverged);
	int32 ConsumerNode = INDEX_NONE;
	for (int32 Index = 0; Index < G.Nodes.Num(); ++Index)
	{
		if (G.Nodes[Index].Kind == ECPFlowNodeKind::Consumer) { ConsumerNode = Index; }
	}
	TestTrue(TEXT("full delivery through collapsed manifold"), Near(Result.DeliveredTo(ConsumerNode, TEXT("Water")), 300.0f));
	return true;
}

// --- Transparent fluid buffers belong to the contractible manifold. Their bidirectional tank
// links otherwise create a non-physical fixed-point oscillation; contraction must retain stock. --
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowTankContractionTest, "CriticalPath.Flow.ContractionMergesFluidBuffers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowTankContractionTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Polymer Resin source"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Polymer Resin"), 300.0f, ECPItemForm::Fluid));
	const int32 T1 = AddNode(G, ECPFlowNodeKind::Storage, TEXT("Industrial Fluid Buffer"));
	const int32 T2 = AddNode(G, ECPFlowNodeKind::Storage, TEXT("Industrial Fluid Buffer"));
	G.Nodes[T1].bContractible = true;
	G.Nodes[T2].bContractible = true;
	FCPBufferedItem Stock1;
	Stock1.Item.Name = TEXT("Polymer Resin");
	Stock1.Amount = 10.0f;
	G.Nodes[T1].Stocks.Add(Stock1);
	FCPBufferedItem Stock2 = Stock1;
	Stock2.Amount = 20.0f;
	G.Nodes[T2].Stocks.Add(Stock2);
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Polymer Resin consumer"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Polymer Resin"), 300.0f, ECPItemForm::Fluid));
	G.Edges[AddEdge(G, P, T1, 600.0f)].bFluid = true;
	G.Edges[AddEdge(G, T1, T2, 600.0f)].bFluid = true;
	G.Edges[AddEdge(G, T2, T1, 600.0f)].bFluid = true;
	G.Edges[AddEdge(G, T2, C, 600.0f)].bFluid = true;

	FCPFlowSolver::CollapseContractibleClusters(G);
	TestTrue(TEXT("fluid buffers merged"), G.Nodes.Num() == 3);
	float RetainedStock = 0.0f;
	for (const FCPFlowNode& Node : G.Nodes)
	{
		for (const FCPBufferedItem& Stock : Node.Stocks) { RetainedStock += Stock.Amount; }
	}
	TestTrue(TEXT("both fluid-buffer stocks retained"), Near(RetainedStock, 30.0f));
	const FCPFlowSolveResult Result = Solve(G);
	TestTrue(TEXT("contracted fluid-buffer line converges"), Result.bConverged);
	int32 ConsumerNode = INDEX_NONE;
	for (int32 Index = 0; Index < G.Nodes.Num(); ++Index)
	{
		if (G.Nodes[Index].Kind == ECPFlowNodeKind::Consumer) { ConsumerNode = Index; }
	}
	TestTrue(TEXT("full delivery through contracted fluid buffers"),
		Near(Result.DeliveredTo(ConsumerNode, TEXT("Polymer Resin")), 300.0f));
	return true;
}

// --- A passive solid-storage loop is one buffered delivery domain. Internal circulation must
// disappear, while its aggregate stock and external belt gates remain exact. ------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowBufferedBeltLoopTest,
	"CriticalPath.Flow.ContractionMergesBufferedBeltLoops",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowBufferedBeltLoopTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("Iron Ore source"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Iron Ore"), 60.0f));
	const int32 A = AddNode(G, ECPFlowNodeKind::Storage, TEXT("Storage A"));
	const int32 M1 = AddNode(G, ECPFlowNodeKind::Merger, TEXT("Merger A"));
	const int32 B = AddNode(G, ECPFlowNodeKind::Storage, TEXT("Storage B"));
	const int32 M2 = AddNode(G, ECPFlowNodeKind::Merger, TEXT("Merger B"));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("Smelter"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Iron Ore"), 30.0f));
	FCPBufferedItem StockA;
	StockA.Item.Name = TEXT("Iron Ore");
	StockA.Amount = 100.0f;
	G.Nodes[A].Stocks.Add(StockA);
	FCPBufferedItem StockB = StockA;
	StockB.Amount = 50.0f;
	G.Nodes[B].Stocks.Add(StockB);
	AddEdge(G, P, A, 60.0f);
	AddEdge(G, A, M1, 120.0f);
	AddEdge(G, M1, B, 120.0f);
	AddEdge(G, B, M2, 120.0f);
	AddEdge(G, M2, A, 120.0f);
	AddEdge(G, M2, C, 30.0f);

	FCPFlowSolver::CollapseContractibleClusters(G);
	TestTrue(TEXT("four passive loop nodes collapse to one domain"), G.Nodes.Num() == 3);
	TestTrue(TEXT("only the external ingress and delivery gates remain"), G.Edges.Num() == 2);
	const int32 Loop = G.Nodes.IndexOfByPredicate([](const FCPFlowNode& Node)
	{
		return Node.Label == TEXT("Buffered Belt Loop");
	});
	const int32 Consumer = G.Nodes.IndexOfByPredicate([](const FCPFlowNode& Node)
	{
		return Node.Label == TEXT("Smelter");
	});
	TestTrue(TEXT("collapsed domain is identified"), Loop != INDEX_NONE);
	float RetainedStock = 0.0f;
	if (G.Nodes.IsValidIndex(Loop))
	{
		for (const FCPBufferedItem& Stock : G.Nodes[Loop].Stocks)
		{
			RetainedStock += Stock.Amount;
		}
	}
	TestTrue(TEXT("all loop stock is retained"), Near(RetainedStock, 150.0f));
	const FCPFlowEdge* Ingress = G.Edges.FindByPredicate([P, Loop](const FCPFlowEdge& Edge)
	{
		return Edge.FromNode == P && Edge.ToNode == Loop;
	});
	const FCPFlowEdge* Delivery = G.Edges.FindByPredicate([Consumer, Loop](const FCPFlowEdge& Edge)
	{
		return Edge.FromNode == Loop && Edge.ToNode == Consumer;
	});
	TestTrue(TEXT("60/min ingress gate retained"), Ingress && Near(Ingress->CapacityPerMinute, 60.0f));
	TestTrue(TEXT("30/min delivery gate retained"), Delivery && Near(Delivery->CapacityPerMinute, 30.0f));

	const FCPFlowSolveResult Result = Solve(G);
	TestTrue(TEXT("collapsed buffered loop converges"), Result.bConverged);
	TestTrue(TEXT("delivery gate caps the consumer at 30/min"),
		Near(Result.DeliveredTo(Consumer, TEXT("Iron Ore")), 30.0f));
	return true;
}

// --- Prospective grafts may attach to one existing domain. Choose its sustainable design
// headroom; never add disconnected factories together or turn incomplete evidence into a number.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowProspectiveGraftTest,
	"CriticalPath.Flow.ProspectiveGraftUsesOneKnownDesignDomain",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowProspectiveGraftTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPPlannedNode Graft;
	Graft.Item.Name = TEXT("Wire");
	Graft.Kind = ECPPlanNodeKind::ExistingProduction;
	Graft.RequiredPerMinute = 25.0f;

	FCPAnalysisResult Result;
	FCPObjectiveReport Objective;
	FCPPartReport ObjectivePart;
	ObjectivePart.PlannedChain.Add(Graft);
	Objective.Parts.Add(ObjectivePart);
	Result.Objectives.Add(Objective);
	FCPResearchGapReport Research;
	FCPPartReport ResearchPart;
	ResearchPart.PlannedChain.Add(Graft);
	Research.CostParts.Add(ResearchPart);
	Result.ResearchPlans.Add(Research);

	FCPItemBalance DomainA;
	DomainA.Item.Name = TEXT("Wire");
	DomainA.DomainId = 1;
	DomainA.ProducerBuildings = 2;
	DomainA.Current.bKnown = true;
	DomainA.Current.SustainablePerMinute = 1.0f; // Planning deliberately uses Design, not Current.
	DomainA.Design.bKnown = true;
	DomainA.Design.SustainablePerMinute = 100.0f;
	DomainA.Design.DemandPerMinute = 80.0f;

	FCPItemBalance DomainB = DomainA;
	DomainB.DomainId = 2;
	DomainB.ProducerBuildings = 1;
	DomainB.Design.SustainablePerMinute = 50.0f;
	DomainB.Design.DemandPerMinute = 40.0f;

	FCPItemBalance ConsumerOnly = DomainA;
	ConsumerOnly.DomainId = 3;
	ConsumerOnly.ProducerBuildings = 0;
	ConsumerOnly.ConsumerBuildings = 1;
	ConsumerOnly.Design.bKnown = false;

	TArray<FCPItemBalance> Balances = { DomainA, DomainB, ConsumerOnly };
	FCPFlowAnalysis::ApplySolverPlanInterpretation(Balances, Result);
	const FCPPlannedNode& ObjectiveGraft = Result.Objectives[0].Parts[0].PlannedChain[0];
	const FCPPlannedNode& ResearchGraft = Result.ResearchPlans[0].CostParts[0].PlannedChain[0];
	TestTrue(TEXT("objective graft is known"), ObjectiveGraft.bSufficiencyKnown);
	TestTrue(TEXT("best single domain provides 20 per minute"),
		Near(ObjectiveGraft.AvailableHeadroomPerMinute, 20.0f));
	TestTrue(TEXT("disconnected domains are not summed"),
		FMath::Abs(ObjectiveGraft.SufficiencyRatio - 0.8f) < 0.01f);
	TestTrue(TEXT("research payment graft is also rewritten"),
		ResearchGraft.bSufficiencyKnown && Near(ResearchGraft.AvailableHeadroomPerMinute, 20.0f));

	FCPItemBalance UnknownProducer = DomainA;
	UnknownProducer.DomainId = 4;
	UnknownProducer.Design.bKnown = false;
	Balances.Add(UnknownProducer);
	FCPFlowAnalysis::ApplySolverPlanInterpretation(Balances, Result);
	const FCPPlannedNode& UnknownGraft = Result.Objectives[0].Parts[0].PlannedChain[0];
	TestTrue(TEXT("one unknown producer domain keeps the graft unknown"), !UnknownGraft.bSufficiencyKnown);
	TestTrue(TEXT("unknown graft does not retain stale numeric evidence"),
		Near(UnknownGraft.AvailableHeadroomPerMinute, 0.0f) && UnknownGraft.SufficiencyRatio < 0.0f);
	return true;
}

// --- The partial-success contract rests on this: a solve that did not settle must yield
// balances marked UNKNOWN, never confident zeroes. FCPWorldAnalysis returns a usable report in
// that state, so the honesty has to live here. ---------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowUnconvergedBalanceTest,
	"CriticalPath.Flow.UnconvergedBalancesAreUnknown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowUnconvergedBalanceTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	FCPFlowGraph G;
	const int32 P = AddNode(G, ECPFlowNodeKind::Producer, TEXT("P"));
	G.Nodes[P].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	G.Nodes[P].DesignRates.Add(Rate(TEXT("Iron"), 60.0f));
	const int32 C = AddNode(G, ECPFlowNodeKind::Consumer, TEXT("C"));
	G.Nodes[C].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	G.Nodes[C].DesignRates.Add(Rate(TEXT("Iron"), 60.0f));
	AddEdge(G, P, C, 780.0f);

	// One iteration cannot satisfy the convergence test, so this stands in for any solve that
	// hits the cap on a real factory.
	FCPFlowSolveParams Starved;
	Starved.MaxIterations = 1;
	FCPFlowSolveResult Unconverged;
	FCPFlowSolver::Solve(G, Starved, Unconverged);
	TestTrue(TEXT("precondition: solve did not converge"), !Unconverged.bConverged);

	TArray<FCPItemBalance> Balances;
	FCPFlowAnalysis::BuildItemBalances(G, Unconverged, Unconverged, Balances);
	for (const FCPItemBalance& Balance : Balances)
	{
		TestTrue(TEXT("non-converged current basis is marked unknown"), !Balance.Current.bKnown);
		TestTrue(TEXT("non-converged design basis is marked unknown"), !Balance.Design.bKnown);
	}

	// A truncated capture is the same class of doubt: the solver refuses to certify a graph that
	// is knowably incomplete, and the balances inherit that refusal.
	FCPFlowGraph Truncated = G;
	Truncated.bTruncated = true;
	FCPFlowSolveResult TruncatedSolve;
	FCPFlowSolver::Solve(Truncated, FCPFlowSolveParams(), TruncatedSolve);
	TestTrue(TEXT("truncated capture is never certified as converged"), !TruncatedSolve.bConverged);
	TArray<FCPItemBalance> TruncatedBalances;
	FCPFlowAnalysis::BuildItemBalances(Truncated, TruncatedSolve, TruncatedSolve, TruncatedBalances);
	for (const FCPItemBalance& Balance : TruncatedBalances)
	{
		TestTrue(TEXT("truncated capture is marked unknown"), !Balance.Current.bKnown);
	}
	return true;
}

// --- Phase B debug replay: when the live game dumps a graph the solver couldn't crack
// (Saved/CriticalPath/FlowGraphDebug.json), replay it here with full diagnostics. Passes
// vacuously when no dump exists so CI stays green. --------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFlowReplayTest, "CriticalPath.Flow.DebugReplay",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFlowReplayTest::RunTest(const FString& Parameters)
{
	using namespace CPFlowTest;
	const FString DumpPath = FPaths::ProjectSavedDir() / TEXT("CriticalPath") / TEXT("FlowGraphDebug.json");
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *DumpPath))
	{
		AddInfo(TEXT("No FlowGraphDebug.json - nothing to replay."));
		return true;
	}
	FCPFlowGraph Graph;
	if (!FJsonObjectConverter::JsonObjectStringToUStruct(Json, &Graph))
	{
		AddError(TEXT("FlowGraphDebug.json failed to parse."));
		return false;
	}
	AddInfo(FString::Printf(TEXT("Replaying dumped graph: %d nodes, %d edges"), Graph.Nodes.Num(), Graph.Edges.Num()));

	// Older dumps predate bContractible; backfill from the capture label so the replay
	// exercises the same collapse the live path runs.
	for (FCPFlowNode& Node : Graph.Nodes)
	{
		if (Node.Kind == ECPFlowNodeKind::Passthrough &&
			(Node.Label == TEXT("Pipe Junction") || Node.Label == TEXT("Pump") || Node.Label == TEXT("Valve")))
		{
			Node.bContractible = true;
		}
		else if (Node.Kind == ECPFlowNodeKind::Storage && Node.Label == TEXT("Industrial Fluid Buffer"))
		{
			Node.bContractible = true;
		}
	}
	FCPFlowSolver::CollapseContractibleClusters(Graph);
	AddInfo(FString::Printf(TEXT("After collapse: %d nodes, %d edges"), Graph.Nodes.Num(), Graph.Edges.Num()));

	FCPFlowSolveParams Params;
	Params.MaxIterations = 256; // diagnostic headroom
	FCPFlowSolveResult Result;
	// Wall-clock the real graph: this dump is the only reproducible large-save benchmark we have
	// offline, and interactive-refresh cost is a release gate.
	const double SolveStart = FPlatformTime::Seconds();
	FCPFlowSolver::Solve(Graph, Params, Result);
	const double SolveMs = (FPlatformTime::Seconds() - SolveStart) * 1000.0;
	AddInfo(FString::Printf(TEXT("PERF single solve: %.1f ms over %d iterations (%d nodes, %d edges)"),
		SolveMs, Result.Iterations, Graph.Nodes.Num(), Graph.Edges.Num()));

	// Per-node candidate items are an OPTIMIZATION: visiting every item at every node must reach
	// exactly the same fixed point. Prove it on the real graph, not just synthetic cases.
	FCPFlowSolveParams DenseParams = Params;
	DenseParams.bDenseItemIteration = true;
	const double DenseStart = FPlatformTime::Seconds();
	FCPFlowSolveResult DenseResult;
	FCPFlowSolver::Solve(Graph, DenseParams, DenseResult);
	const double DenseMs = (FPlatformTime::Seconds() - DenseStart) * 1000.0;
	AddInfo(FString::Printf(TEXT("PERF dense (parity) solve: %.1f ms - sparse is %.1fx faster"),
		DenseMs, DenseMs / FMath::Max(SolveMs, 0.001)));

	TestEqual(TEXT("parity: same convergence"), DenseResult.bConverged, Result.bConverged);
	TestEqual(TEXT("parity: same iteration count"), DenseResult.Iterations, Result.Iterations);
	TestEqual(TEXT("parity: same delivery count"), DenseResult.Deliveries.Num(), Result.Deliveries.Num());
	float WorstDelta = 0.0f;
	FString WorstWhere;
	for (const FCPConsumerDelivery& Sparse : Result.Deliveries)
	{
		const float DenseDelivered = DenseResult.DeliveredTo(Sparse.NodeIndex, Sparse.ItemName);
		const float Delta = FMath::Abs(DenseDelivered - Sparse.DeliveredPerMinute);
		if (Delta > WorstDelta)
		{
			WorstDelta = Delta;
			WorstWhere = FString::Printf(TEXT("node %d %s: sparse %.3f vs dense %.3f"),
				Sparse.NodeIndex, *Sparse.ItemName, Sparse.DeliveredPerMinute, DenseDelivered);
		}
	}
	AddInfo(FString::Printf(TEXT("parity worst delivery delta: %.4f/min %s"), WorstDelta, *WorstWhere));
	TestTrue(TEXT("parity: deliveries match dense iteration"), WorstDelta <= 0.5f);

	FString History;
	const int32 HistoryStart = FMath::Max(0, Result.DeltaHistory.Num() - 16);
	for (int32 Index = HistoryStart; Index < Result.DeltaHistory.Num(); ++Index)
	{
		History += FString::Printf(TEXT("%.3f "), Result.DeltaHistory[Index]);
	}
	AddInfo(FString::Printf(TEXT("converged=%d iterations=%d lastDeltas=[%s]"),
		Result.bConverged ? 1 : 0, Result.Iterations, *History));
	if (Result.LastMaxDeltaEdge != INDEX_NONE && Graph.Edges.IsValidIndex(Result.LastMaxDeltaEdge))
	{
		const FCPFlowEdge& Worst = Graph.Edges[Result.LastMaxDeltaEdge];
		const FCPFlowNode& From = Graph.Nodes[Worst.FromNode];
		const FCPFlowNode& To = Graph.Nodes[Worst.ToNode];
		AddInfo(FString::Printf(TEXT("worst edge #%d item=%s: [%d]%s(kind %d) -> [%d]%s(kind %d) cap=%.1f fluid=%d"),
			Result.LastMaxDeltaEdge, *Result.LastMaxDeltaItem,
			Worst.FromNode, *From.Label, (int32)From.Kind,
			Worst.ToNode, *To.Label, (int32)To.Kind, Worst.CapacityPerMinute, Worst.bFluid ? 1 : 0));
	}
	TestTrue(TEXT("dumped real-world graph converges"), Result.bConverged);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
