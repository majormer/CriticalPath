// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

// Synthetic-factory regression suite (PRD §13): the analysis core is UObject-free, so every
// pathological case the prototype discovered live is encoded here as a constructed snapshot.
// NOTE: use TestTrue with explicit comparisons — mixed-int TestEqual overloads are a known
// C2666 trap in this codebase's toolchain.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "CPAnalysis.h"
#include "CPEngineTypes.h"

namespace CPTest
{
FCPItemRef Item(const TCHAR* Name)
{
	FCPItemRef Ref;
	Ref.Name = Name;
	return Ref;
}

FCPProductRow& AddProduct(FCPFactorySnapshot& S, const TCHAR* Name, float Capacity, int32 Configured, int32 Producing, float Productivity = 100.0f)
{
	FCPProductRow Row;
	Row.Item = Item(Name);
	Row.ConfiguredCapacityPerMinute = Capacity;
	Row.ConfiguredBuildings = Configured;
	Row.ProducingBuildings = Producing;
	Row.AverageProductivityPercent = Productivity;
	S.Products.Add(Row);
	return S.Products.Last();
}

void AddDemand(FCPFactorySnapshot& S, const TCHAR* Name, float Demand)
{
	FCPDemandRow Row;
	Row.Item = Item(Name);
	Row.ConfiguredDemandPerMinute = Demand;
	Row.ConsumingBuildings = 1;
	S.Demands.Add(Row);
}

void AddFlowNetwork(FCPFactorySnapshot& S, const TCHAR* Name, int32 NetworkId, float Supply,
	float Demand, bool bComplete = true, int32 Consumers = 1, int32 InactiveConsumers = 0)
{
	FCPFlowNetwork Network;
	Network.Item = Item(Name);
	Network.NetworkId = NetworkId;
	Network.ConnectedSupplyPerMinute = Supply;
	Network.ConfiguredDemandPerMinute = Demand;
	Network.ProducerBuildings = Supply > 0.0f ? 1 : 0;
	Network.ConsumerBuildings = Consumers;
	Network.InactiveConsumerBuildings = InactiveConsumers;
	Network.bConnectivityComplete = bComplete;
	S.FlowNetworks.Add(Network);
}

void AddEdge(FCPFactorySnapshot& S, const TCHAR* Product, std::initializer_list<const TCHAR*> Ingredients)
{
	FCPRecipeEdge Edge;
	Edge.RecipeName = Product;
	Edge.MachineCount = 1;
	FCPItemRate ProductRate;
	ProductRate.Item = Item(Product);
	Edge.Products.Add(ProductRate);
	for (const TCHAR* Ingredient : Ingredients)
	{
		FCPItemRate Rate;
		Rate.Item = Item(Ingredient);
		Edge.Ingredients.Add(Rate);
	}
	S.Edges.Add(MoveTemp(Edge));
}

void AddObjective(FCPFactorySnapshot& S, const TCHAR* Name, const TCHAR* ItemName, int32 Required, int32 Remaining)
{
	FCPObjective Objective;
	Objective.Name = Name;
	FCPObjectiveItem ObjItem;
	ObjItem.Item = Item(ItemName);
	ObjItem.Required = Required;
	ObjItem.Remaining = Remaining;
	ObjItem.Delivered = Required - Remaining;
	Objective.Items.Add(ObjItem);
	S.Objectives.Add(MoveTemp(Objective));
}

void AddOwned(FCPFactorySnapshot& S, const TCHAR* Name, int64 InStorage)
{
	FCPOwnedRow Row;
	Row.Item = Item(Name);
	Row.InStorage = InStorage;
	S.Owned.Add(Row);
}
} // namespace CPTest

// --- The base walk follows categorical starvation but does not infer an extraction-rate
// shortfall from retired island aggregates. Flow-domain interpretation continues from here. ---
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPWalkRootCauseTest, "CriticalPath.Analysis.BaseWalkDefersRateRootCause",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPWalkRootCauseTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	FCPProductRow& VF = AddProduct(S, TEXT("Versatile Framework"), 25.0f, 2, 1, 50.0f);
	VF.MissingInputBuildings = 1;
	FCPProductRow& Beam = AddProduct(S, TEXT("Steel Beam"), 120.0f, 2, 1, 60.0f);
	Beam.MissingInputBuildings = 1;
	FCPProductRow& Ore = AddProduct(S, TEXT("Iron Ore"), 990.0f, 7, 7);
	Ore.bIsExtraction = true;
	Ore.bConnectivityComplete = true;
	Ore.ConnectedCapacityPerMinute = 990.0f;
	AddDemand(S, TEXT("Steel Beam"), 150.0f);
	AddDemand(S, TEXT("Iron Ore"), 1485.0f);
	AddFlowNetwork(S, TEXT("Iron Ore"), 1, 990.0f, 1485.0f);
	AddEdge(S, TEXT("Versatile Framework"), { TEXT("Steel Beam") });
	AddEdge(S, TEXT("Steel Beam"), { TEXT("Iron Ore") });

	FCPBlocker Blocker;
	const bool bFound = FCPAnalysis::FindBlocker(S, TEXT("Versatile Framework"), Blocker);
	TestTrue(TEXT("blocker found"), bFound);
	TestTrue(TEXT("base walk stops at the starved intermediate"), Blocker.Item.Name == TEXT("Steel Beam"));
	TestTrue(TEXT("rate-shaped cause remains unknown until flow analysis"),
		Blocker.Reason == ECPBlockerReason::ConnectivityUnknown);
	TestTrue(TEXT("base path contains the objective and starved intermediate"), Blocker.Path.Num() == 2);
	return true;
}

// --- Saturation and standby reserves are benign: never blockers, correctly classified. -------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPBenignStatesTest, "CriticalPath.Analysis.BenignStatesAreNotFaults",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPBenignStatesTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	FCPProductRow& Quickwire = AddProduct(S, TEXT("Quickwire"), 1080.0f, 18, 0, 0.0f);
	Quickwire.OutputBlockedBuildings = 1;
	Quickwire.StandbyBuildings = 17;
	FCPProductRow& Coal = AddProduct(S, TEXT("Charcoal"), 1740.0f, 7, 0, 0.0f);
	Coal.StandbyBuildings = 7;

	TestTrue(TEXT("blocked+standby = saturated"),
		FCPAnalysis::ClassifyItem(S, TEXT("Quickwire")) == ECPNodeStatus::Saturated);
	TestTrue(TEXT("pure standby = deliberate reserve"),
		FCPAnalysis::ClassifyItem(S, TEXT("Charcoal")) == ECPNodeStatus::StandbyReserve);

	FCPBlocker Blocker;
	TestTrue(TEXT("saturated chain has no blocker"), !FCPAnalysis::FindBlocker(S, TEXT("Quickwire"), Blocker));
	return true;
}

// --- Exactly-funded batch run: banked stock covers the requirement -> payable, no ETA panic,
// and a fully-covered objective reports payable-now. ------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFundedRunTest, "CriticalPath.Analysis.FundedRunIsPayableNotFailing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFundedRunTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	AddObjective(S, TEXT("Main Body"), TEXT("Versatile Framework"), 2500, 500);
	AddOwned(S, TEXT("Versatile Framework"), 600);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	TestTrue(TEXT("one objective"), Result.Objectives.Num() == 1);
	if (Result.Objectives.Num() == 1)
	{
		const FCPObjectiveReport& Report = Result.Objectives[0];
		TestTrue(TEXT("objective payable now"), Report.bPayableNow);
		TestTrue(TEXT("one part"), Report.Parts.Num() == 1);
		if (Report.Parts.Num() == 1)
		{
			TestTrue(TEXT("banked clamped to remaining"), Report.Parts[0].Banked == 500);
			TestTrue(TEXT("nothing left to produce"), Report.Parts[0].StillToProduce == 0);
			TestTrue(TEXT("part payable"), Report.Parts[0].bPayable);
		}
	}
	return true;
}

// --- Banked-aware ETA: remaining minus banked, divided by prorated effective rate. -----------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPEtaTest, "CriticalPath.Analysis.BankedAwareEta",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPEtaTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	AddProduct(S, TEXT("Modular Engine"), 5.0f, 2, 2, 100.0f);
	AddObjective(S, TEXT("Main Body"), TEXT("Modular Engine"), 500, 480);
	AddOwned(S, TEXT("Modular Engine"), 120);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	TestTrue(TEXT("one objective, one part"), Result.Objectives.Num() == 1 && Result.Objectives[0].Parts.Num() == 1);
	if (Result.Objectives.Num() == 1 && Result.Objectives[0].Parts.Num() == 1)
	{
		const FCPPartReport& Part = Result.Objectives[0].Parts[0];
		TestTrue(TEXT("banked 120"), Part.Banked == 120);
		TestTrue(TEXT("360 to produce"), Part.StillToProduce == 360);
		TestTrue(TEXT("time-limited status"), Part.Status == ECPNodeStatus::TimeLimited);
		TestTrue(TEXT("eta ~72 min"), FMath::IsNearlyEqual(Part.EtaMinutes, 72.0f, 0.5f));
	}
	return true;
}

// --- Recipe cycles must not hang or overflow the walk. ---------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPCycleTest, "CriticalPath.Analysis.CyclesTerminate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPCycleTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	FCPProductRow& Rubber = AddProduct(S, TEXT("Rubber"), 20.0f, 1, 0, 0.0f);
	Rubber.MissingInputBuildings = 1;
	FCPProductRow& Plastic = AddProduct(S, TEXT("Plastic"), 20.0f, 1, 0, 0.0f);
	Plastic.MissingInputBuildings = 1;
	AddEdge(S, TEXT("Rubber"), { TEXT("Plastic") });
	AddEdge(S, TEXT("Plastic"), { TEXT("Rubber") });

	FCPBlocker Blocker;
	const bool bFound = FCPAnalysis::FindBlocker(S, TEXT("Rubber"), Blocker);
	TestTrue(TEXT("cycle resolves to a blocker without hanging"), bFound);
	TestTrue(TEXT("cycle does not invent a routing verdict"), Blocker.Reason == ECPBlockerReason::ConnectivityUnknown);
	return true;
}

// --- Base analysis does not turn the retired overlapping connectivity aggregates into a
// routing verdict. Disjoint flow-domain tests own the definitive not-connected case. ----------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPOrphanedProducerTest, "CriticalPath.Analysis.BaseDefersConnectivityVerdict",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPOrphanedProducerTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	FCPProductRow& Motors = AddProduct(S, TEXT("Motor"), 120.0f, 4, 4);
	Motors.bConnectivityComplete = true;
	Motors.ConnectedCapacityPerMinute = 0.0f;
	Motors.ConnectedBuildings = 0;
	AddDemand(S, TEXT("Motor"), 50.0f);

	FCPBlocker Blocker;
	const bool bFound = FCPAnalysis::FindBlocker(S, TEXT("Motor"), Blocker);
	TestTrue(TEXT("base pass leaves routing verdict to flow domains"), !bFound);
	TestTrue(TEXT("base pass does not classify from legacy connectivity"),
		FCPAnalysis::ClassifyItem(S, TEXT("Motor")) != ECPNodeStatus::UnderSupplied);
	return true;
}

// --- Base analysis remains neutral for connected and incomplete legacy aggregates alike. ----
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPConnectedProducerTest, "CriticalPath.Analysis.ConnectedProducerCountsOnlyReachableSupply",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPConnectedProducerTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	FCPProductRow& Motors = AddProduct(S, TEXT("Motor"), 120.0f, 4, 4);
	Motors.bConnectivityComplete = true;
	Motors.ConnectedCapacityPerMinute = 60.0f;
	Motors.ConnectedBuildings = 2;
	AddDemand(S, TEXT("Motor"), 50.0f);

	FCPBlocker Blocker;
	TestTrue(TEXT("connected capacity satisfies demand"), !FCPAnalysis::FindBlocker(S, TEXT("Motor"), Blocker));
	TestTrue(TEXT("connected item remains healthy"), FCPAnalysis::ClassifyItem(S, TEXT("Motor")) != ECPNodeStatus::UnderSupplied);

	Motors.bConnectivityComplete = false;
	Motors.ConnectedCapacityPerMinute = 0.0f;
	TestTrue(TEXT("incomplete graph does not claim producer disconnected"), !FCPAnalysis::FindBlocker(S, TEXT("Motor"), Blocker));
	return true;
}

// --- Public sufficiency is empty until the disjoint flow-domain pass supplies it. -------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPNetworkSufficiencyTest, "CriticalPath.Analysis.BaseDefersSufficiencyToFlow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPNetworkSufficiencyTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	FCPProductRow& Plates = AddProduct(S, TEXT("Iron Plate"), 160.0f, 3, 3);
	Plates.bConnectivityComplete = true;
	Plates.ConnectedCapacityPerMinute = 160.0f;
	AddDemand(S, TEXT("Iron Plate"), 100.0f);
	AddFlowNetwork(S, TEXT("Iron Plate"), 10, 140.0f, 50.0f, true, 2, 0);
	AddFlowNetwork(S, TEXT("Iron Plate"), 20, 20.0f, 50.0f, true, 1, 1);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	TestTrue(TEXT("base pass emits no legacy sufficiency summary"), Result.Sufficiency.Num() == 0);
	TestTrue(TEXT("base pass does not classify from overlapping islands"),
		FCPAnalysis::ClassifyItem(S, TEXT("Iron Plate")) != ECPNodeStatus::UnderSupplied);
	return true;
}

// --- A capped graph exposes lower bounds but never promotes them to a complete ratio. --------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPUnknownSufficiencyTest, "CriticalPath.Analysis.IncompleteSufficiencyIsUnknown",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPUnknownSufficiencyTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	FCPProductRow& Motors = AddProduct(S, TEXT("Motor"), 100.0f, 2, 2);
	Motors.bConnectivityComplete = false;
	Motors.ConnectedCapacityPerMinute = 10.0f;
	AddDemand(S, TEXT("Motor"), 50.0f);
	AddFlowNetwork(S, TEXT("Motor"), 1, 10.0f, 50.0f, false);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	TestTrue(TEXT("base pass emits no legacy sufficiency summary"), Result.Sufficiency.Num() == 0);
	FCPBlocker Blocker;
	TestTrue(TEXT("lower-bound supply does not manufacture a shortage verdict"),
		!FCPAnalysis::FindBlocker(S, TEXT("Motor"), Blocker));
	return true;
}

// --- M2 prospective chain: a no-producer objective part expands through the catalog slice to
// graft points; sibling branches may repeat an item (only true cycles are cut); alt flags and
// research locks surface on their nodes. ------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPPlannedChainTest, "CriticalPath.Analysis.PlannedChainGrafts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPPlannedChainTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	// Real factory produces Iron Plate only.
	FCPProductRow& Plates = AddProduct(S, TEXT("Iron Plate"), 100.0f, 2, 2);
	Plates.bConnectivityComplete = true;
	Plates.ConnectedCapacityPerMinute = 100.0f;
	AddFlowNetwork(S, TEXT("Iron Plate"), 1, 100.0f, 80.0f);
	// Catalog: Widget = Gadget + Iron Plate; Gadget = Iron Plate + Mystery Goo (no recipe, not raw).
	{
		FCPCatalogRecipe Widget;
		Widget.Product = Item(TEXT("Widget"));
		Widget.RecipeName = TEXT("Widget");
		Widget.MachineName = TEXT("Assembler");
		Widget.ProductPerMinutePerMachine = 2.0f;
		Widget.bAlternatesExist = true;
		FCPItemRate G; G.Item = Item(TEXT("Gadget")); G.RatePerMinute = 4.0f;
		FCPItemRate P; P.Item = Item(TEXT("Iron Plate")); P.RatePerMinute = 3.0f;
		Widget.Ingredients.Add(G);
		Widget.Ingredients.Add(P);
		S.Catalog.Add(MoveTemp(Widget));

		FCPCatalogRecipe Gadget;
		Gadget.Product = Item(TEXT("Gadget"));
		Gadget.RecipeName = TEXT("Gadget");
		Gadget.MachineName = TEXT("Constructor");
		Gadget.ProductPerMinutePerMachine = 4.0f;
		FCPItemRate P2; P2.Item = Item(TEXT("Iron Plate")); P2.RatePerMinute = 6.0f;
		FCPItemRate Goo; Goo.Item = Item(TEXT("Mystery Goo"));
		Gadget.Ingredients.Add(P2);
		Gadget.Ingredients.Add(Goo);
		S.Catalog.Add(MoveTemp(Gadget));
	}
	FCPResearchGap Gap;
	Gap.NeededItem = Item(TEXT("Mystery Goo"));
	S.ResearchGaps.Add(Gap);
	AddObjective(S, TEXT("Phase"), TEXT("Widget"), 100, 100);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	TestTrue(TEXT("one objective"), Result.Objectives.Num() == 1);
	const FCPPartReport& Part = Result.Objectives[0].Parts[0];
	TestTrue(TEXT("blocked: no producer"), Part.Blocker.Reason == ECPBlockerReason::NoProducer);

	// Expected depth-first flatten:
	// Widget(planned,alts) > Gadget(planned) > Iron Plate(graft) > Mystery Goo(locked) > Iron Plate(graft again)
	TestTrue(TEXT("5 planned nodes"), Part.PlannedChain.Num() == 5);
	if (Part.PlannedChain.Num() == 5)
	{
		TestTrue(TEXT("root is planned Widget"), Part.PlannedChain[0].Item.Name == TEXT("Widget") && Part.PlannedChain[0].Kind == ECPPlanNodeKind::PlannedLine);
		TestTrue(TEXT("root flags alternates"), Part.PlannedChain[0].bAlternatesExist);
		TestTrue(TEXT("Gadget planned at depth 1"), Part.PlannedChain[1].Item.Name == TEXT("Gadget") && Part.PlannedChain[1].Depth == 1);
		TestTrue(TEXT("Iron Plate grafts under Gadget"), Part.PlannedChain[2].Kind == ECPPlanNodeKind::ExistingProduction && Part.PlannedChain[2].Depth == 2);
		TestTrue(TEXT("graft inherits six per minute requirement"), FMath::IsNearlyEqual(Part.PlannedChain[2].RequiredPerMinute, 6.0f));
		TestTrue(TEXT("base graft headroom waits for flow domains"), !Part.PlannedChain[2].bSufficiencyKnown);
		TestTrue(TEXT("Mystery Goo research-locked"), Part.PlannedChain[3].Kind == ECPPlanNodeKind::ResearchLocked);
		TestTrue(TEXT("Iron Plate repeats as Widget's own ingredient"), Part.PlannedChain[4].Item.Name == TEXT("Iron Plate") && Part.PlannedChain[4].Depth == 1);
	}
	return true;
}

// --- Base analysis no longer publishes a limiter from overlapping network walks. -------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPLimiterWalkTest, "CriticalPath.Analysis.BaseDefersLimiterToFlow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPLimiterWalkTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	// Chain: Part <- Mid <- Deep. Part itself is fine; Mid runs at 80%, Deep at 50%.
	// Unknown: an extra ingredient with incomplete connectivity must count as unknown only.
	FCPProductRow& PartRow = AddProduct(S, TEXT("Part"), 10.0f, 1, 1);
	PartRow.bConnectivityComplete = true;
	PartRow.ConnectedCapacityPerMinute = 10.0f;
	FCPProductRow& Mid = AddProduct(S, TEXT("Mid"), 80.0f, 2, 2);
	Mid.bConnectivityComplete = true;
	Mid.ConnectedCapacityPerMinute = 80.0f;
	FCPProductRow& Deep = AddProduct(S, TEXT("Deep"), 50.0f, 1, 1);
	Deep.bConnectivityComplete = true;
	Deep.ConnectedCapacityPerMinute = 50.0f;
	FCPProductRow& Foggy = AddProduct(S, TEXT("Foggy"), 30.0f, 1, 1);
	Foggy.bConnectivityComplete = false; // ratio unknowable
	AddDemand(S, TEXT("Mid"), 100.0f);
	AddDemand(S, TEXT("Deep"), 100.0f);
	AddDemand(S, TEXT("Foggy"), 100.0f);
	AddEdge(S, TEXT("Part"), { TEXT("Mid"), TEXT("Foggy") });
	AddEdge(S, TEXT("Mid"), { TEXT("Deep") });
	AddObjective(S, TEXT("Phase"), TEXT("Part"), 100, 100);
	// M2.5 runway: 500 stored Deep over a 50/min deficit = ~10 minutes of buffer.
	AddOwned(S, TEXT("Deep"), 500);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	const FCPPartReport& Report = Result.Objectives[0].Parts[0];
	TestTrue(TEXT("base pass leaves limiter unknown"), !Report.Limiter.IsSet());
	return true;
}

// --- Legacy equality does not resurrect a base-pass limiter. ---------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPLimiterTieTest, "CriticalPath.Analysis.BaseIgnoresLegacyLimiterTie",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPLimiterTieTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	FCPProductRow& PartRow = AddProduct(S, TEXT("Part"), 10.0f, 1, 1);
	PartRow.bConnectivityComplete = true;
	PartRow.ConnectedCapacityPerMinute = 10.0f;
	FCPProductRow& Mid = AddProduct(S, TEXT("Mid"), 50.0f, 1, 1);
	Mid.bConnectivityComplete = true;
	Mid.ConnectedCapacityPerMinute = 50.0f;
	FCPProductRow& Deep = AddProduct(S, TEXT("Deep"), 50.0f, 1, 1);
	Deep.bConnectivityComplete = true;
	Deep.ConnectedCapacityPerMinute = 50.0f;
	AddDemand(S, TEXT("Mid"), 100.0f);
	AddDemand(S, TEXT("Deep"), 100.0f);
	AddEdge(S, TEXT("Part"), { TEXT("Mid") });
	AddEdge(S, TEXT("Mid"), { TEXT("Deep") });
	AddObjective(S, TEXT("Phase"), TEXT("Part"), 100, 100);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	const FCPPartReport& Report = Result.Objectives[0].Parts[0];
	TestTrue(TEXT("base pass leaves limiter unknown"), !Report.Limiter.IsSet());
	return true;
}

// --- Build tiers: planned lines aggregate ACROSS parts into a bottom-up build order — tier 1
// depends only on existing production, each later tier on the ones before; dedup keeps an
// item at its deepest requirement. ------------------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPBuildTiersTest, "CriticalPath.Analysis.BuildTiersBottomUp",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPBuildTiersTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	AddProduct(S, TEXT("Iron Plate"), 100.0f, 2, 2);
	// Widget = Gadget + Iron Plate; Gadget = Iron Plate. Two objectives: one needs Widget
	// (tiers: Gadget=1, Widget=2), the other needs Gadget directly (tier 1 — dedup, deepest wins).
	{
		FCPCatalogRecipe Widget;
		Widget.Product = Item(TEXT("Widget"));
		Widget.RecipeName = TEXT("Widget");
		FCPItemRate G; G.Item = Item(TEXT("Gadget"));
		FCPItemRate P; P.Item = Item(TEXT("Iron Plate"));
		Widget.Ingredients.Add(G);
		Widget.Ingredients.Add(P);
		S.Catalog.Add(MoveTemp(Widget));

		FCPCatalogRecipe Gadget;
		Gadget.Product = Item(TEXT("Gadget"));
		Gadget.RecipeName = TEXT("Gadget");
		FCPItemRate P2; P2.Item = Item(TEXT("Iron Plate"));
		Gadget.Ingredients.Add(P2);
		S.Catalog.Add(MoveTemp(Gadget));
	}
	AddObjective(S, TEXT("Phase"), TEXT("Widget"), 100, 100);
	AddObjective(S, TEXT("Milestone"), TEXT("Gadget"), 10, 10);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	TestTrue(TEXT("two tiers"), Result.BuildTiers.Num() == 2);
	if (Result.BuildTiers.Num() == 2)
	{
		TestTrue(TEXT("tier 1 is exactly Gadget"),
			Result.BuildTiers[0].Items.Num() == 1 && Result.BuildTiers[0].Items[0].Item.Name == TEXT("Gadget"));
		TestTrue(TEXT("tier 2 is exactly Widget"),
			Result.BuildTiers[1].Items.Num() == 1 && Result.BuildTiers[1].Items[0].Item.Name == TEXT("Widget"));
	}
	return true;
}

// --- M2.7b research plans: unlock costs run the same part pipeline (ledger + planned chain)
// and their missing items join the build tiers. ------------------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPResearchPlanTest, "CriticalPath.Analysis.ResearchCostsJoinThePlan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPResearchPlanTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	AddProduct(S, TEXT("Iron Plate"), 100.0f, 2, 2);
	AddOwned(S, TEXT("Doohickey"), 30);
	// Gap: "Exotic Part" unlocks via "Fancy Research" costing 50 Doohickey (30 banked) +
	// 10 Widget (no line; catalog: Widget = Iron Plate -> planned tier 1).
	{
		FCPCatalogRecipe Widget;
		Widget.Product = Item(TEXT("Widget"));
		Widget.RecipeName = TEXT("Widget");
		FCPItemRate P; P.Item = Item(TEXT("Iron Plate"));
		Widget.Ingredients.Add(P);
		S.Catalog.Add(MoveTemp(Widget));
	}
	FCPResearchGap Gap;
	Gap.NeededItem = Item(TEXT("Exotic Part"));
	Gap.UnlockSchematicName = TEXT("Fancy Research");
	FCPObjectiveItem CostA;
	CostA.Item = Item(TEXT("Doohickey"));
	CostA.Required = 50; CostA.Remaining = 50;
	FCPObjectiveItem CostB;
	CostB.Item = Item(TEXT("Widget"));
	CostB.Required = 10; CostB.Remaining = 10;
	Gap.UnlockCosts.Add(CostA);
	Gap.UnlockCosts.Add(CostB);
	S.ResearchGaps.Add(Gap);
	AddObjective(S, TEXT("Phase"), TEXT("Exotic Part"), 5, 5);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	TestTrue(TEXT("one research plan"), Result.ResearchPlans.Num() == 1);
	if (Result.ResearchPlans.Num() == 1)
	{
		const FCPResearchGapReport& Plan = Result.ResearchPlans[0];
		TestTrue(TEXT("two cost parts"), Plan.CostParts.Num() == 2);
		TestTrue(TEXT("Doohickey ledger: 30 banked of 50"),
			Plan.CostParts[0].Banked == 30 && Plan.CostParts[0].StillToProduce == 20);
		TestTrue(TEXT("Widget cost gets a planned chain"),
			Plan.CostParts[1].PlannedChain.Num() > 0);
	}
	// Widget's planned line joins the build tiers even though no OBJECTIVE part plans it.
	TestTrue(TEXT("Widget lands in tier 1"),
		Result.BuildTiers.Num() >= 1 && Result.BuildTiers[0].Items.ContainsByPredicate(
			[](const FCPBuildStep& S) { return S.Item.Name == TEXT("Widget"); }));
	return true;
}

// --- M2.7 byproduct disposal: a fully stalled line with outputs full and a consumer-less
// co-product names that co-product as the suspected clog. --------------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPByproductTest, "CriticalPath.Analysis.ByproductBackupNamed",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPByproductTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	// Consumer of Fuel starves; Fuel refineries are fully stalled with outputs backed up.
	// The Fuel recipe co-produces Resin, which nothing consumes -> Resin is the suspect.
	FCPProductRow& Widget = AddProduct(S, TEXT("Widget"), 10.0f, 1, 0, 20.0f);
	Widget.MissingInputBuildings = 1;
	FCPProductRow& Fuel = AddProduct(S, TEXT("Fuel"), 60.0f, 2, 0, 10.0f);
	Fuel.OutputBlockedBuildings = 2;
	AddDemand(S, TEXT("Fuel"), 40.0f);
	AddEdge(S, TEXT("Widget"), { TEXT("Fuel") });
	{
		FCPRecipeEdge FuelEdge;
		FuelEdge.RecipeName = TEXT("Fuel");
		FuelEdge.MachineCount = 2;
		FCPItemRate FuelOut; FuelOut.Item = Item(TEXT("Fuel"));
		FCPItemRate ResinOut; ResinOut.Item = Item(TEXT("Polymer Resin"));
		FuelEdge.Products.Add(FuelOut);
		FuelEdge.Products.Add(ResinOut);
		S.Edges.Add(MoveTemp(FuelEdge));
	}

	FCPBlocker Blocker;
	const bool bFound = FCPAnalysis::FindBlocker(S, TEXT("Widget"), Blocker);
	TestTrue(TEXT("blocker found"), bFound);
	TestTrue(TEXT("reason is byproduct backup"), Blocker.Reason == ECPBlockerReason::ByproductBackedUp);
	TestTrue(TEXT("blocker at Fuel"), Blocker.Item.Name == TEXT("Fuel"));
	TestTrue(TEXT("byproduct named Polymer Resin"), Blocker.Byproduct.Name == TEXT("Polymer Resin"));
	return true;
}

// --- Concurrent at-machine issues are ALL enumerated, worst first (a machine can be unpowered
// AND unfed AND clogged at once — one reason is half the truth). ------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPIssueListTest, "CriticalPath.Analysis.EnumeratesConcurrentIssues",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPIssueListTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	// Two machines: both unpowered, both starving, one output-blocked (no byproduct edge).
	FCPProductRow& Row = AddProduct(S, TEXT("Gizmo"), 20.0f, 2, 0, 0.0f);
	Row.NoPowerBuildings = 2;
	Row.MissingInputBuildings = 2;
	Row.OutputBlockedBuildings = 1;
	AddObjective(S, TEXT("Phase"), TEXT("Gizmo"), 100, 100);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	const FCPPartReport& Part = Result.Objectives[0].Parts[0];
	TestTrue(TEXT("three issues enumerated"), Part.Issues.Num() == 3);
	if (Part.Issues.Num() == 3)
	{
		TestTrue(TEXT("worst first: no power"), Part.Issues[0].Reason == ECPBlockerReason::NoPower);
		TestTrue(TEXT("power affects 2 of 2"), Part.Issues[0].AffectedBuildings == 2 && Part.Issues[0].TotalBuildings == 2);
		TestTrue(TEXT("then outputs full"), Part.Issues[1].Reason == ECPBlockerReason::OutputsFull);
		TestTrue(TEXT("outputs affect 1 of 2"), Part.Issues[1].AffectedBuildings == 1);
		TestTrue(TEXT("then inputs starved"), Part.Issues[2].Reason == ECPBlockerReason::InputsStarved);
	}
	return true;
}

// --- A line that EXISTS but starves extends the prospective frontier: its unproduced
// ingredients stay in the build order (a dead machine must not swallow the plan). ------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPStarvingFrontierTest, "CriticalPath.Analysis.StarvingLineExtendsFrontier",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPStarvingFrontierTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	// Gizmo line exists but is unpowered AND starving; its ingredient Doohickey has no line.
	FCPProductRow& Gizmo = AddProduct(S, TEXT("Gizmo"), 10.0f, 1, 0, 0.0f);
	Gizmo.NoPowerBuildings = 1;
	Gizmo.MissingInputBuildings = 1;
	AddProduct(S, TEXT("Iron Plate"), 100.0f, 2, 2);
	AddEdge(S, TEXT("Gizmo"), { TEXT("Doohickey") });
	{
		FCPCatalogRecipe Doohickey;
		Doohickey.Product = Item(TEXT("Doohickey"));
		Doohickey.RecipeName = TEXT("Doohickey");
		Doohickey.MachineName = TEXT("Constructor");
		FCPItemRate P; P.Item = Item(TEXT("Iron Plate"));
		Doohickey.Ingredients.Add(P);
		S.Catalog.Add(MoveTemp(Doohickey));
	}
	AddObjective(S, TEXT("Phase"), TEXT("Gizmo"), 100, 100);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	const FCPPartReport& Part = Result.Objectives[0].Parts[0];
	TestTrue(TEXT("both issues enumerated"), Part.Issues.Num() == 2);
	TestTrue(TEXT("planned chain reaches Doohickey through the dead line"),
		Part.PlannedChain.ContainsByPredicate([](const FCPPlannedNode& N)
			{ return N.Item.Name == TEXT("Doohickey") && N.Kind == ECPPlanNodeKind::PlannedLine; }));
	TestTrue(TEXT("Doohickey stays in the build order"),
		Result.BuildTiers.Num() >= 1 && Result.BuildTiers[0].Items.ContainsByPredicate(
			[](const FCPBuildStep& Step) { return Step.Item.Name == TEXT("Doohickey"); }));
	return true;
}

// --- Build order interleaves FIX steps: dependents of a broken-but-existing line sort AFTER
// its repair, which sorts after the lines that must feed it. ----------------------------------
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCPFixTierTest, "CriticalPath.Analysis.BuildOrderInterleavesFixes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FCPFixTierTest::RunTest(const FString& Parameters)
{
	using namespace CPTest;
	FCPFactorySnapshot S;
	// Widget (no line) <- Gizmo (line EXISTS, broken: unpowered + starving) <- Doohickey (no
	// line) <- Iron Plate (healthy). Expected order: 1) build Doohickey, 2) fix Gizmo,
	// 3) build Widget.
	FCPProductRow& Gizmo = AddProduct(S, TEXT("Gizmo"), 10.0f, 1, 0, 0.0f);
	Gizmo.NoPowerBuildings = 1;
	Gizmo.MissingInputBuildings = 1;
	AddProduct(S, TEXT("Iron Plate"), 100.0f, 2, 2);
	AddEdge(S, TEXT("Gizmo"), { TEXT("Doohickey") });
	{
		FCPCatalogRecipe Widget;
		Widget.Product = Item(TEXT("Widget"));
		Widget.RecipeName = TEXT("Widget");
		FCPItemRate G; G.Item = Item(TEXT("Gizmo"));
		Widget.Ingredients.Add(G);
		S.Catalog.Add(MoveTemp(Widget));

		FCPCatalogRecipe Doohickey;
		Doohickey.Product = Item(TEXT("Doohickey"));
		Doohickey.RecipeName = TEXT("Doohickey");
		FCPItemRate P; P.Item = Item(TEXT("Iron Plate"));
		Doohickey.Ingredients.Add(P);
		S.Catalog.Add(MoveTemp(Doohickey));
	}
	AddObjective(S, TEXT("Phase"), TEXT("Widget"), 100, 100);
	AddObjective(S, TEXT("Milestone"), TEXT("Gizmo"), 10, 10);

	const FCPAnalysisResult Result = FCPAnalysis::Analyze(S);
	TestTrue(TEXT("three tiers"), Result.BuildTiers.Num() == 3);
	if (Result.BuildTiers.Num() == 3)
	{
		TestTrue(TEXT("tier 1 builds Doohickey"),
			Result.BuildTiers[0].Items.Num() == 1 && Result.BuildTiers[0].Items[0].Item.Name == TEXT("Doohickey"));
		TestTrue(TEXT("tier 2 fixes Gizmo"),
			Result.BuildTiers[1].Fixes.Num() == 1 && Result.BuildTiers[1].Fixes[0].Item.Name == TEXT("Gizmo") &&
			Result.BuildTiers[1].Items.Num() == 0);
		TestTrue(TEXT("tier 3 builds Widget"),
			Result.BuildTiers[2].Items.Num() == 1 && Result.BuildTiers[2].Items[0].Item.Name == TEXT("Widget"));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
