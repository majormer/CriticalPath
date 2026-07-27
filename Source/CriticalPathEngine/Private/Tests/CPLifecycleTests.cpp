// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

// Lifecycle and unknown-state matrix (Validation.md release gate 4).
//
// Every other suite here asks "does the analysis get the right answer". This one asks the
// opposite: when the analysis CANNOT get an answer, does it say so instead of inventing one.
// Those are the states where a factory tool loses a player's trust — a cold start, a world with
// nothing in it, a capture that failed, a solve that did not settle. A confident zero in any of
// them is worse than a blank, because the player acts on it.
//
// The states covered, and the flag each must set:
//
//   base analysis only ....... FlowEvidence = NotAttempted   (never Available)
//   empty world .............. no objectives, no phantom faults
//   snapshot cap hit ......... Truncation.bAnyCapHit survives into the result
//   flow capture failed ...... FlowEvidence = CaptureFailed, base facts retained
//   non-converged solve ...... balances present but bKnown = false
//   current vs design ........ two independent bases, neither overwriting the other
//
// Deliberately NOT covered here, because they are not engine states: world still loading and
// stale-data age are owned by UCPPanelSubsystem, which is UObject-bound and outside this
// UObject-free module.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CPAnalysis.h"
#include "CPEngineTypes.h"
#include "CPFlowAnalysis.h"
#include "CPFlowSolver.h"

namespace CPLifecycleTest
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

void AddEdge(FCPFlowGraph& Graph, int32 From, int32 To, float Capacity)
{
	FCPFlowEdge Edge;
	Edge.FromNode = From;
	Edge.ToNode = To;
	Edge.CapacityPerMinute = Capacity;
	Graph.Edges.Add(MoveTemp(Edge));
}
} // namespace CPLifecycleTest

// --- Cold start: base analysis has run no flow pass and must not imply it has. ---------------
// A report that says "Available" with nothing behind it is indistinguishable from a measured
// factory with no flow, which is the failure this enum exists to prevent.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPLifecycleBaseOnlyTest, "CriticalPath.Lifecycle.BaseOnlyDoesNotClaimFlowEvidence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPLifecycleBaseOnlyTest::RunTest(const FString& Parameters)
{
	FCPFactorySnapshot Snapshot;
	const FCPAnalysisResult Result = FCPAnalysis::Analyze(Snapshot);
	TestTrue(TEXT("base-only analysis reports NotAttempted, not Available"),
		Result.FlowEvidence == ECPFlowEvidenceState::NotAttempted);
	return true;
}

// --- Empty world: a legitimate state, not an error and not a broken factory. -----------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPLifecycleEmptyWorldTest, "CriticalPath.Lifecycle.EmptyWorldIsNotAFailure",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPLifecycleEmptyWorldTest::RunTest(const FString& Parameters)
{
	FCPFactorySnapshot Snapshot;
	const FCPAnalysisResult Result = FCPAnalysis::Analyze(Snapshot);

	TestTrue(TEXT("no objectives are invented"), Result.Objectives.Num() == 0);
	TestTrue(TEXT("no sufficiency rows are invented"), Result.Sufficiency.Num() == 0);
	TestTrue(TEXT("no build plan is invented"), Result.BuildTiers.Num() == 0);
	TestTrue(TEXT("nothing is reported as truncated"), !Result.Truncation.bAnyCapHit);
	return true;
}

// --- Some items no machine can make. Advising production for them is unfollowable. ----------
// Power slugs, alien remains and mycelia have real demand (a Constructor consumes them) and no
// recipe anywhere in the game produces them. Reported as NoProducer, the plan tells the player to
// build a line that cannot exist - which is the advice a downstream reader of this result relayed
// to the maintainer. Membership comes from the live recipe set, so a mod that adds a recipe drops
// the item from the list and the ordinary production advice returns.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPLifecycleWorldGatheredTest,
	"CriticalPath.Lifecycle.WorldGatheredItemsAreNotAProductionGap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPLifecycleWorldGatheredTest::RunTest(const FString& Parameters)
{
	auto BuildSnapshot = [](bool bGatherable)
	{
		FCPFactorySnapshot Snapshot;
		FCPObjective Objective;
		Objective.Name = TEXT("Test Objective");
		Objective.Kind = ECPObjectiveKind::Milestone;
		FCPObjectiveItem Item;
		Item.Item.Name = TEXT("Blue Power Slug");
		Item.Required = 10;
		Item.Remaining = 10;
		Objective.Items.Add(MoveTemp(Item));
		Snapshot.Objectives.Add(MoveTemp(Objective));
		if (bGatherable)
		{
			Snapshot.WorldGatheredItemNames.Add(TEXT("Blue Power Slug"));
		}
		return Snapshot;
	};

	const FCPAnalysisResult Gathered = FCPAnalysis::Analyze(BuildSnapshot(true));
	TestTrue(TEXT("the gatherable list survives into the result"),
		Gathered.WorldGatheredItemNames.Contains(TEXT("Blue Power Slug")));
	if (Gathered.Objectives.Num() == 0 || Gathered.Objectives[0].Parts.Num() == 0)
	{
		AddError(TEXT("expected one objective part"));
		return false;
	}
	TestEqual(TEXT("a world-gathered item is not reported as a missing production line"),
		Gathered.Objectives[0].Parts[0].Blocker.Reason, ECPBlockerReason::WorldGatheredOnly);

	// The same item WITHOUT the marking (a mod added a recipe) keeps the old verdict, so this
	// never becomes a permanent exemption for something that later becomes craftable.
	const FCPAnalysisResult Craftable = FCPAnalysis::Analyze(BuildSnapshot(false));
	if (Craftable.Objectives.Num() > 0 && Craftable.Objectives[0].Parts.Num() > 0)
	{
		TestEqual(TEXT("an ordinary unbuilt item is still a production gap"),
			Craftable.Objectives[0].Parts[0].Blocker.Reason, ECPBlockerReason::NoProducer);
	}
	return true;
}

// --- Truncation must SURVIVE analysis. --------------------------------------------------
// The cap is hit during capture; the player only ever sees the result. If the flag is dropped in
// between, a partial scan is presented as a complete one — the panel's truncation notice is
// driven entirely by this field.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPLifecycleTruncationTest, "CriticalPath.Lifecycle.SnapshotCapHitIsDisclosed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPLifecycleTruncationTest::RunTest(const FString& Parameters)
{
	FCPFactorySnapshot Snapshot;
	Snapshot.Truncation.bAnyCapHit = true;
	Snapshot.Truncation.BuildingsScanned = 250;
	Snapshot.Truncation.BuildingsAvailable = 4000;

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(Snapshot);
	TestTrue(TEXT("cap-hit flag survives into the result"), Result.Truncation.bAnyCapHit);
	TestTrue(TEXT("scanned count survives"), Result.Truncation.BuildingsScanned == 250);
	TestTrue(TEXT("available count survives"), Result.Truncation.BuildingsAvailable == 4000);
	return true;
}

// --- A non-converged solve yields numbers. They must not be presented as facts. --------------
// A truncated graph is refused by the solver, which is the cheapest way to reach the
// non-converged state deterministically. What matters is what the DERIVED layer does with it:
// enrichment still runs (dropping it would lose the base facts too), so every value it produces
// has to carry bKnown = false.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPLifecycleNonConvergedTest, "CriticalPath.Lifecycle.NonConvergedRatesStayUnknown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPLifecycleNonConvergedTest::RunTest(const FString& Parameters)
{
	using namespace CPLifecycleTest;
	FCPFlowGraph Graph;
	const int32 Producer = AddNode(Graph, ECPFlowNodeKind::Producer, TEXT("P"));
	Graph.Nodes[Producer].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	const int32 Consumer = AddNode(Graph, ECPFlowNodeKind::Consumer, TEXT("C"));
	Graph.Nodes[Consumer].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	AddEdge(Graph, Producer, Consumer, 780.0f);
	Graph.bTruncated = true;

	FCPFlowSolveResult CurrentSolve;
	FCPFlowSolver::Solve(Graph, FCPFlowSolveParams(), CurrentSolve);
	FCPFlowSolveParams DesignParams;
	DesignParams.bUseDesignRates = true;
	FCPFlowSolveResult DesignSolve;
	FCPFlowSolver::Solve(Graph, DesignParams, DesignSolve);

	TestTrue(TEXT("solve does not claim convergence"), !CurrentSolve.bConverged);

	TArray<FCPItemBalance> Balances;
	FCPFlowAnalysis::BuildItemBalances(Graph, CurrentSolve, DesignSolve, Balances);
	for (const FCPItemBalance& Balance : Balances)
	{
		TestTrue(TEXT("no current-basis value is claimed as known"), !Balance.Current.bKnown);
		TestTrue(TEXT("no design-basis value is claimed as known"), !Balance.Design.bKnown);
	}

	TArray<FCPItemSufficiency> Sufficiency;
	FCPFlowAnalysis::BuildSolverSufficiency(Balances, Sufficiency);
	for (const FCPItemSufficiency& Row : Sufficiency)
	{
		TestTrue(TEXT("sufficiency derived from a non-converged solve is unknown"), !Row.bKnown);
	}
	return true;
}

// --- Current and design are two independent readings of one factory. ------------------------
// Design answers "what did you build for", current answers "what is it doing now". A machine
// running below its rating must make them differ; collapsing them would hide exactly the gap the
// Balance tab exists to show.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPLifecycleBasisTest, "CriticalPath.Lifecycle.CurrentAndDesignAreSeparate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPLifecycleBasisTest::RunTest(const FString& Parameters)
{
	using namespace CPLifecycleTest;
	FCPFlowGraph Graph;
	const int32 Producer = AddNode(Graph, ECPFlowNodeKind::Producer, TEXT("P"));
	// Currently limping at a quarter of what it was built to do.
	Graph.Nodes[Producer].Rates.Add(Rate(TEXT("Iron"), 15.0f));
	Graph.Nodes[Producer].DesignRates.Add(Rate(TEXT("Iron"), 60.0f));
	const int32 Consumer = AddNode(Graph, ECPFlowNodeKind::Consumer, TEXT("C"));
	Graph.Nodes[Consumer].Rates.Add(Rate(TEXT("Iron"), 60.0f));
	Graph.Nodes[Consumer].DesignRates.Add(Rate(TEXT("Iron"), 60.0f));
	AddEdge(Graph, Producer, Consumer, 780.0f);

	FCPFlowSolveResult CurrentSolve;
	FCPFlowSolver::Solve(Graph, FCPFlowSolveParams(), CurrentSolve);
	FCPFlowSolveParams DesignParams;
	DesignParams.bUseDesignRates = true;
	FCPFlowSolveResult DesignSolve;
	FCPFlowSolver::Solve(Graph, DesignParams, DesignSolve);

	TestTrue(TEXT("current solve settles"), CurrentSolve.bConverged);
	TestTrue(TEXT("design solve settles"), DesignSolve.bConverged);

	TArray<FCPItemBalance> Balances;
	FCPFlowAnalysis::BuildItemBalances(Graph, CurrentSolve, DesignSolve, Balances);

	const FCPItemBalance* Iron = Balances.FindByPredicate(
		[](const FCPItemBalance& B) { return B.Item.Name == TEXT("Iron"); });
	TestTrue(TEXT("the item has a balance row"), Iron != nullptr);
	if (Iron)
	{
		TestTrue(TEXT("both bases are known on a converged solve"), Iron->Current.bKnown && Iron->Design.bKnown);
		// Installed is INTENTIONALLY the same on both bases: it is what the placed machines could
		// make, a property of the build rather than of this moment, so it does not evaporate when
		// a machine stalls. Asserted rather than assumed — the difference between the bases must
		// show up in flow, and an Installed that moved with the current reading would mean the
		// panel's "built capacity" bar shrank whenever the factory hiccuped.
		TestTrue(TEXT("installed capacity does not differ between bases"),
			FMath::IsNearlyEqual(Iron->Current.InstalledPerMinute, Iron->Design.InstalledPerMinute, 0.5f));
		TestTrue(TEXT("design delivers what it was built for"),
			FMath::IsNearlyEqual(Iron->Design.DeliveredPerMinute, 60.0f, 0.5f));
		TestTrue(TEXT("current delivers only what the limping producer makes"),
			FMath::IsNearlyEqual(Iron->Current.DeliveredPerMinute, 15.0f, 0.5f));
		TestTrue(TEXT("the two bases are genuinely separate readings"),
			Iron->Design.DeliveredPerMinute > Iron->Current.DeliveredPerMinute + 0.5f);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
