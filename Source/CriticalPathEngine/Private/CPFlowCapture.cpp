// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPFlowCapture.h"

#include "CPFlowSolver.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableAttachmentMerger.h"
#include "Buildables/FGBuildableAttachmentSplitter.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildableGeneratorFuel.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableMergerPriority.h"
#include "Buildables/FGBuildablePipeline.h"
#include "Buildables/FGBuildablePipelineJunction.h"
#include "Buildables/FGBuildablePipelinePump.h"
#include "Buildables/FGBuildablePassthroughBase.h"
#include "Buildables/FGBuildablePipeReservoir.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableResourceSink.h"
#include "Buildables/FGBuildableSplitterSmart.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableDroneStation.h"
#include "FGBuildableSubsystem.h"
#include "FGDroneStationInfo.h"
#include "FGDroneSubsystem.h"
#include "FGRailroadSubsystem.h"
#include "FGRailroadTimeTable.h"
#include "FGTrain.h"
#include "FGTrainPlatformConnection.h"
#include "FGTrainStationIdentifier.h"
#include "WheeledVehicles/FGVehicleSubsystem.h"
#include "WheeledVehicles/FGWheeledVehicleIdentifier.h"
#include "WheeledVehicles/FGDockingStationIdentifier.h"
#include "Buildables/FGBuildableRailroadStation.h"
#include "Buildables/FGBuildableTrainPlatform.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "FGConveyorItem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGFactorySettings.h"
#include "FGInventoryComponent.h"
#include "FGPipeConnectionComponent.h"
#include "FGPipeConnectionFactory.h"
#include "Components/SplineComponent.h"
#include "FGRecipe.h"
#include "Resources/FGAnyUndefinedDescriptor.h"
#include "Resources/FGExtractableResourceInterface.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGItemDescriptorNuclearFuel.h"
#include "Resources/FGNoneDescriptor.h"
#include "Resources/FGOverflowDescriptor.h"
#include "Resources/FGPowerShardDescriptor.h"
#include "Resources/FGWildCardDescriptor.h"

namespace
{
// The game's convention: conveyor mSpeed is items/min * 2 (Mk1 speed 120 = 60 items/min).
float BeltItemsPerMinute(const AFGBuildableConveyorBase* Belt)
{
	return Belt ? Belt->GetSpeed() * 0.5f : 0.0f;
}

bool IsGasClass(TSubclassOf<UFGItemDescriptor> ItemClass)
{
	return ItemClass && UFGItemDescriptor::GetForm(ItemClass) == EResourceForm::RF_GAS;
}

bool IsFluidClass(TSubclassOf<UFGItemDescriptor> ItemClass)
{
	if (!ItemClass)
	{
		return false;
	}
	const EResourceForm Form = UFGItemDescriptor::GetForm(ItemClass);
	return Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
}

/** Recipe/extraction amounts for fluids are LITERS; the model runs in display units (m3/min). */
float ToDisplayRate(TSubclassOf<UFGItemDescriptor> ItemClass, float PerMinute)
{
	return IsFluidClass(ItemClass) ? PerMinute / 1000.0f : PerMinute;
}

/** Factory-settings production pressure (~10m) applied to producer pipe outputs (FlowModel S11). */
float SettingsProductionHeadM()
{
	static const FFloatProperty* Prop = FindFProperty<FFloatProperty>(
		UFGFactorySettings::StaticClass(), TEXT("mAddedPipeProductionPressure"));
	const UFGFactorySettings* Settings = UFGFactorySettings::Get();
	return (Prop && Settings) ? Prop->GetPropertyValue_InContainer(Settings) : 0.0f;
}

/** Per-connection opt-out of the settings pressure ("checkbox on fluid outputs"). */
bool AppliesProductionPressure(const UFGPipeConnectionComponent* Connection)
{
	const UFGPipeConnectionFactory* Factory = Cast<UFGPipeConnectionFactory>(Connection);
	if (!Factory)
	{
		return false;
	}
	static const FBoolProperty* Prop = FindFProperty<FBoolProperty>(
		UFGPipeConnectionFactory::StaticClass(), TEXT("mApplyAdditionalPressure"));
	return Prop ? Prop->GetPropertyValue_InContainer(Factory) : true;
}

FCPItemRef MakeFlowItemRef(TSubclassOf<UFGItemDescriptor> ItemClass)
{
	FCPItemRef Item;
	Item.Name = UFGItemDescriptor::GetItemName(ItemClass).ToString();
	Item.DescriptorClassPath = ItemClass->GetPathName();
	Item.Form = IsFluidClass(ItemClass) ? ECPItemForm::Fluid : ECPItemForm::Solid;
	return Item;
}

FCPItemRate MakeRate(TSubclassOf<UFGItemDescriptor> ItemClass, float PerMinute)
{
	FCPItemRate Rate;
	Rate.Item = MakeFlowItemRef(ItemClass);
	Rate.RatePerMinute = PerMinute;
	return Rate;
}

FCPBufferedItem MakeStock(TSubclassOf<UFGItemDescriptor> ItemClass, float Amount)
{
	FCPBufferedItem Stock;
	Stock.Item = MakeFlowItemRef(ItemClass);
	Stock.Amount = Amount;
	return Stock;
}

TSubclassOf<UFGItemDescriptor> ResolveGeneratorFuel(AFGBuildableGeneratorFuel* Generator)
{
	if (!Generator)
	{
		return nullptr;
	}
	if (TSubclassOf<UFGItemDescriptor> Fuel = Generator->GetCurrentFuelClass())
	{
		return Fuel;
	}
	if (UFGInventoryComponent* Inventory = Generator->GetFuelInventory())
	{
		for (int32 Index = 0; Index < Inventory->GetSizeLinear(); ++Index)
		{
			FInventoryStack Stack;
			if (Inventory->GetStackFromIndex(Index, Stack) && Stack.HasItems() &&
				Stack.Item.GetItemClass() && Generator->IsValidFuel(Stack.Item.GetItemClass()))
			{
				return Stack.Item.GetItemClass();
			}
		}
	}
	TArray<UFGPipeConnectionComponent*> PipeConnections;
	Generator->GetComponents<UFGPipeConnectionComponent>(PipeConnections);
	for (UFGPipeConnectionComponent* Connection : PipeConnections)
	{
		if (Connection)
		{
			TSubclassOf<UFGItemDescriptor> Fluid = Connection->GetFluidDescriptor();
			if (Fluid && Generator->IsValidFuel(Fluid))
			{
				return Fluid;
			}
		}
	}
	return nullptr; // No configured/observed fuel: absence is safer than choosing an allowed fuel.
}

float GeneratorFuelRatePerMinute(AFGBuildableGeneratorFuel* Generator,
	TSubclassOf<UFGItemDescriptor> Fuel, bool bDesign)
{
	const float EnergyPerInternalUnit = Fuel ? UFGItemDescriptor::GetEnergyValue(Fuel) : 0.0f;
	if (!Generator || EnergyPerInternalUnit <= SMALL_NUMBER)
	{
		return 0.0f;
	}
	float PowerMW = Generator->GetPowerProductionCapacity();
	if (!bDesign)
	{
		if (Generator->IsProductionPaused())
		{
			PowerMW = 0.0f;
		}
		else if (Generator->HasFuel())
		{
			// Load-following generators consume less when the grid does not request full output.
			// A fuel-starved generator keeps maximum demand so starvation remains diagnosable.
			PowerMW *= Generator->GetLoadPercentage();
		}
	}
	const float InternalUnitsPerMinute = PowerMW * 60.0f / EnergyPerInternalUnit;
	return ToDisplayRate(Fuel, InternalUnitsPerMinute);
}

float NormalizeProductivityPercent(float Value)
{
	return FMath::Clamp(Value <= 1.5f ? Value * 100.0f : Value, 0.0f, 100.0f);
}

void CaptureMachineState(FCPFlowNode& Node, AFGBuildableFactory* Factory)
{
	if (!Factory)
	{
		return;
	}
	Node.BuildingItem = MakeFlowItemRef(Factory->GetBuiltWithDescriptor());
	Node.bMachineStateKnown = true;
	Node.bProducing = Factory->IsProducing();
	Node.ProductivityPercent = NormalizeProductivityPercent(Factory->GetProductivity());
	Node.bNoPower = Factory->RunsOnPower() && !Factory->HasPower();
	Node.bPaused = Factory->IsProductionPaused();
	if (UFGInventoryComponent* PotentialInventory = Factory->GetPotentialInventory())
	{
		TArray<FInventoryStack> Stacks;
		PotentialInventory->GetInventoryStacks(Stacks);
		for (const FInventoryStack& Stack : Stacks)
		{
			TSubclassOf<UFGItemDescriptor> ItemClass = Stack.Item.GetItemClass();
			if (!Stack.HasItems() || !ItemClass || !ItemClass->IsChildOf(UFGPowerShardDescriptor::StaticClass()))
			{
				continue;
			}
			const TSubclassOf<UFGPowerShardDescriptor> ShardClass(ItemClass.Get());
			switch (UFGPowerShardDescriptor::GetPowerShardType(ShardClass))
			{
			case EPowerShardType::PST_Overclock:
				Node.PowerShardCount += Stack.NumItems;
				Node.PowerShardItem = MakeFlowItemRef(ItemClass);
				break;
			case EPowerShardType::PST_ProductionBoost:
				Node.SomersloopCount += Stack.NumItems;
				Node.SomersloopItem = MakeFlowItemRef(ItemClass);
				break;
			default:
				break;
			}
		}
	}
	if (Node.bProducing)
	{
		return; // breathing input/output buffers are normal while a machine is running
	}

	if (AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Factory))
	{
		if (TSubclassOf<UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe())
		{
			if (UFGInventoryComponent* InputInventory = Manufacturer->GetInputInventory())
			{
				for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(Manufacturer, Recipe))
				{
					if (Ingredient.ItemClass && Ingredient.Amount > 0 &&
						!InputInventory->HasItems(Ingredient.ItemClass, Ingredient.Amount))
					{
						Node.bMissingInput = true;
						Node.StarvedOfItemName = UFGItemDescriptor::GetItemName(Ingredient.ItemClass).ToString();
						break;
					}
				}
			}
			// Starved with a foreign item on a feeding belt = jam, not shortage. Scoped to belts
			// terminating at this machine: a mixed belt elsewhere may be a sushi line with a
			// splitter waiting to sort it, but a machine input is a terminus.
			if (Node.bMissingInput)
			{
				TSet<TSubclassOf<UFGItemDescriptor>> Ingredients;
				for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(Manufacturer, Recipe))
				{
					if (Ingredient.ItemClass) { Ingredients.Add(Ingredient.ItemClass); }
				}
				TInlineComponentArray<UFGFactoryConnectionComponent*> Connections(Manufacturer);
				for (UFGFactoryConnectionComponent* Connection : Connections)
				{
					if (!Node.JammedByItemName.IsEmpty()) { break; }
					if (!Connection || Connection->GetDirection() != EFactoryConnectionDirection::FCD_INPUT ||
						!Connection->IsConnected())
					{
						continue;
					}
					UFGFactoryConnectionComponent* Peer = Connection->GetConnection();
					AFGBuildableConveyorBelt* Belt = Peer ? Cast<AFGBuildableConveyorBelt>(Peer->GetOwner()) : nullptr;
					if (!Belt) { continue; }
					TArray<FConveyorBeltItem*> BeltItems;
					Belt->GetConveyorBeltItems(BeltItems);
					for (const FConveyorBeltItem* BeltItem : BeltItems)
					{
						const TSubclassOf<UFGItemDescriptor> ItemClass = BeltItem ? BeltItem->Item.GetItemClass() : nullptr;
						if (ItemClass && !Ingredients.Contains(ItemClass))
						{
							Node.JammedByItemName = UFGItemDescriptor::GetItemName(ItemClass).ToString();
							break;
						}
					}
				}
			}
			if (UFGInventoryComponent* OutputInventory = Manufacturer->GetOutputInventory())
			{
				for (int32 Index = 0; Index < OutputInventory->GetSizeLinear(); ++Index)
				{
					FInventoryStack Stack;
					if (OutputInventory->GetStackFromIndex(Index, Stack) && Stack.HasItems())
					{
						const int32 SlotSize = OutputInventory->GetSlotSizeForItem(
							Index, Stack.Item.GetItemClass(), &Stack.Item);
						if (SlotSize > 0 && Stack.NumItems >= SlotSize)
						{
							Node.bOutputBlocked = true;
							break;
						}
					}
				}
			}
		}
	}
}

/** Follow a conveyor run from an output connection to the first non-conveyor buildable.
 *  Returns the terminal buildable's input connection owner and the run's min capacity. */
AFGBuildable* WalkBeltRun(UFGFactoryConnectionComponent* FromOutput, int32 MaxHops,
	float& OutCapacity, UFGFactoryConnectionComponent*& OutTerminalConnection)
{
	OutCapacity = 0.0f;
	OutTerminalConnection = nullptr;
	UFGFactoryConnectionComponent* Cursor = FromOutput;
	float MinCapacity = BIG_NUMBER;
	for (int32 Hop = 0; Hop < MaxHops && Cursor && Cursor->IsConnected(); ++Hop)
	{
		UFGFactoryConnectionComponent* Peer = Cursor->GetConnection();
		if (!Peer)
		{
			return nullptr;
		}
		AFGBuildable* PeerOwner = Cast<AFGBuildable>(Peer->GetOwner());
		if (!PeerOwner)
		{
			return nullptr;
		}
		if (AFGBuildableConveyorBase* Belt = Cast<AFGBuildableConveyorBase>(PeerOwner))
		{
			MinCapacity = FMath::Min(MinCapacity, BeltItemsPerMinute(Belt));
			// Continue out the belt's other end.
			UFGFactoryConnectionComponent* Next = nullptr;
			TInlineComponentArray<UFGFactoryConnectionComponent*> Components(Belt);
			for (UFGFactoryConnectionComponent* Component : Components)
			{
				if (Component != Peer && Component->GetDirection() == EFactoryConnectionDirection::FCD_OUTPUT)
				{
					Next = Component;
					break;
				}
			}
			Cursor = Next;
			continue;
		}
		// Terminal buildable reached. Direct machine-to-machine connections have no belt:
		// capacity is effectively unbounded at this granularity.
		OutCapacity = MinCapacity >= BIG_NUMBER * 0.5f ? 1.0e9f : MinCapacity;
		OutTerminalConnection = Peer;
		return PeerOwner;
	}
	return nullptr;
}

/** Follow a pipeline run from a pipe connection to the first non-pipeline buildable.
 *  Tracks min flow capacity (m3/min) and MAX spline Z in meters (humps count, FlowModel S11.3). */
AFGBuildable* WalkPipeRun(UFGPipeConnectionComponent* FromConnection, int32 MaxHops,
	float& OutCapacity, float& OutMaxZMeters, UFGPipeConnectionComponentBase*& OutTerminalConnection)
{
	OutCapacity = 0.0f;
	OutMaxZMeters = -BIG_NUMBER;
	OutTerminalConnection = nullptr;
	UFGPipeConnectionComponentBase* Cursor = FromConnection;
	OutMaxZMeters = FromConnection->GetComponentLocation().Z / 100.0f;
	float MinCapacity = BIG_NUMBER;
	for (int32 Hop = 0; Hop < MaxHops && Cursor && Cursor->IsConnected(); ++Hop)
	{
		UFGPipeConnectionComponentBase* Peer = Cursor->GetConnection();
		if (!Peer)
		{
			return nullptr;
		}
		AFGBuildable* PeerOwner = Cast<AFGBuildable>(Peer->GetOwner());
		if (!PeerOwner)
		{
			return nullptr;
		}
		if (AFGBuildablePipeline* Pipeline = Cast<AFGBuildablePipeline>(PeerOwner))
		{
			MinCapacity = FMath::Min(MinCapacity, Pipeline->GetFlowLimit() * 60.0f); // m3/s -> m3/min
			if (const USplineComponent* Spline = Pipeline->GetSplineComponent())
			{
				const int32 Points = Spline->GetNumberOfSplinePoints();
				for (int32 Index = 0; Index < Points; ++Index)
				{
					OutMaxZMeters = FMath::Max(OutMaxZMeters,
						Spline->GetLocationAtSplinePoint(Index, ESplineCoordinateSpace::World).Z / 100.0f);
				}
			}
			Cursor = Pipeline->GetConnection0() == Peer ? Pipeline->GetConnection1() : Pipeline->GetConnection0();
			continue;
		}
		if (Cast<AFGBuildablePassthroughBase>(PeerOwner))
		{
			// Foundation/wall passthrough hole: purely conductive, hop out the other side.
			UFGPipeConnectionComponentBase* Next = nullptr;
			TInlineComponentArray<UFGPipeConnectionComponentBase*> Passages(PeerOwner);
			for (UFGPipeConnectionComponentBase* Passage : Passages)
			{
				if (Passage != Peer)
				{
					Next = Passage;
					break;
				}
			}
			OutMaxZMeters = FMath::Max(OutMaxZMeters, Peer->GetComponentLocation().Z / 100.0f);
			Cursor = Next;
			continue;
		}
		// Terminal buildable. Direct connections (no pipe) are unbounded at this granularity.
		OutCapacity = MinCapacity >= BIG_NUMBER * 0.5f ? 1.0e9f : MinCapacity;
		OutMaxZMeters = FMath::Max(OutMaxZMeters, Peer->GetComponentLocation().Z / 100.0f);
		OutTerminalConnection = Peer;
		return PeerOwner;
	}
	return nullptr;
}

struct FCaptureContext
{
	FCPFlowGraph& Graph;
	TMap<AFGBuildable*, int32> RoutingNodeByActor;   // splitter/merger/storage/sink/passthrough
	TMap<AFGBuildable*, int32> ConsumerNodeByActor;  // manufacturers (input side)
	TMap<AFGBuildable*, int32> ProducerNodeByActor;  // manufacturers/extractors (output side)

	// Head-lift gate scratch (FlowModel S11), all in METERS (world cm / 100).
	TMap<int32, float> HeadSourceByNode;   // node -> budget it seeds (producers/pumps/reservoirs)
	TSet<int32> PumpNodes;                 // budget-group breakers: pass their OWN head, not the inherited one
	TSet<int32> ClosedValveNodes;          // mUserFlowLimit == 0: nothing crosses
	TSet<int32> GasNodes;                  // touches a gas-form item: seeds gas-network spread
	struct FPipeEdgeMeta { int32 EdgeIndex = INDEX_NONE; float MaxZM = 0.0f; bool bGas = false; };
	TArray<FPipeEdgeMeta> PipeEdges;

	explicit FCaptureContext(FCPFlowGraph& InGraph) : Graph(InGraph) {}

	int32 AddNode(ECPFlowNodeKind Kind, const FString& Label, const AActor* Actor = nullptr)
	{
		FCPFlowNode Node;
		Node.Kind = Kind;
		Node.Label = Label;
		if (Actor)
		{
			Node.ActorName = Actor->GetName();
			Node.Location = Actor->GetActorLocation();
		}
		Graph.Nodes.Add(MoveTemp(Node));
		return Graph.Nodes.Num() - 1;
	}
};

/** Smart-splitter rule mapping per output index (0..2). No rule on an output = closed. */
void BuildSplitterRules(AFGBuildableSplitterSmart* Smart, TArray<FCPSplitterRule>& OutRules)
{
	OutRules.SetNum(3);
	for (FCPSplitterRule& Rule : OutRules)
	{
		Rule.Rule = ECPSplitterOutRule::None;
	}
	for (int32 RuleIndex = 0; RuleIndex < Smart->GetNumSortRules(); ++RuleIndex)
	{
		const FSplitterSortRule Sort = Smart->GetSortRuleAt(RuleIndex);
		if (!OutRules.IsValidIndex(Sort.OutputIndex) || !Sort.ItemClass)
		{
			continue;
		}
		FCPSplitterRule& Rule = OutRules[Sort.OutputIndex];
		if (Sort.ItemClass->IsChildOf(UFGWildCardDescriptor::StaticClass()))
		{
			Rule.Rule = ECPSplitterOutRule::Any;
		}
		else if (Sort.ItemClass->IsChildOf(UFGOverflowDescriptor::StaticClass()))
		{
			Rule.Rule = ECPSplitterOutRule::Overflow;
		}
		else if (Sort.ItemClass->IsChildOf(UFGAnyUndefinedDescriptor::StaticClass()))
		{
			Rule.Rule = ECPSplitterOutRule::AnyUndefined;
		}
		else if (Sort.ItemClass->IsChildOf(UFGNoneDescriptor::StaticClass()))
		{
			Rule.Rule = ECPSplitterOutRule::None;
		}
		else
		{
			// A specific item filter; multiple rules on one output accumulate items.
			if (Rule.Rule != ECPSplitterOutRule::Filtered)
			{
				Rule.Rule = ECPSplitterOutRule::Filtered;
				Rule.Items.Reset();
			}
			Rule.Items.Add(UFGItemDescriptor::GetItemName(Sort.ItemClass).ToString());
		}
	}
}

bool StorageIsFull(AFGBuildableStorage* Storage)
{
	UFGInventoryComponent* Inventory = Storage ? Storage->GetStorageInventory() : nullptr;
	if (!Inventory)
	{
		return false;
	}
	// A slotless inventory is not evidence of fullness: the loop below would fall straight through
	// to "full". That happens on a remote client, where stacks are not replicated and every
	// container would otherwise be reported jammed.
	if (Inventory->GetSizeLinear() <= 0)
	{
		return false;
	}
	// Full = no empty slot and every stack at its slot cap (same pattern as the
	// output-blocked check in CPSnapshot).
	for (int32 Index = 0; Index < Inventory->GetSizeLinear(); ++Index)
	{
		FInventoryStack Stack;
		if (!Inventory->GetStackFromIndex(Index, Stack) || !Stack.HasItems())
		{
			return false;
		}
		const int32 SlotSize = Inventory->GetSlotSizeForItem(Index, Stack.Item.GetItemClass(), &Stack.Item);
		if (SlotSize > 0 && Stack.NumItems < SlotSize)
		{
			return false;
		}
	}
	return true;
}
} // namespace

bool FCPFlowCapture::Capture(UObject* WorldContext, const FCPFlowCaptureParams& Params, FCPFlowGraph& OutGraph, FString& OutError)
{
	OutGraph = FCPFlowGraph();
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		OutError = TEXT("No world");
		return false;
	}
	AFGBuildableSubsystem* BuildableSubsystem = AFGBuildableSubsystem::Get(World);
	if (!BuildableSubsystem)
	{
		OutError = TEXT("No buildable subsystem");
		return false;
	}
	OutGraph.bAuthoritative = World->GetNetMode() != NM_Client;

	FCaptureContext Ctx(OutGraph);

	// ---- pass 1: nodes -------------------------------------------------------------------
	TArray<AFGBuildable*> AllBuildables;
	BuildableSubsystem->GetTypedBuildable(AFGBuildable::StaticClass(), AllBuildables);

	for (AFGBuildable* Buildable : AllBuildables)
	{
		if (OutGraph.Nodes.Num() >= Params.MaxNodes)
		{
			OutGraph.bTruncated = true;
			break;
		}
		if (!IsValid(Buildable) || Cast<AFGBuildableConveyorBase>(Buildable))
		{
			continue; // belts are edges
		}

		if (AFGBuildableGeneratorFuel* Generator = Cast<AFGBuildableGeneratorFuel>(Buildable))
		{
			const int32 ConsumerNode = Ctx.AddNode(ECPFlowNodeKind::Consumer, Generator->mDisplayName.ToString(), Generator);
			CaptureMachineState(OutGraph.Nodes[ConsumerNode], Generator);
			if (!Generator->HasFuel() && !OutGraph.Nodes[ConsumerNode].bProducing)
			{
				OutGraph.Nodes[ConsumerNode].bMissingInput = true;
			}
			if (Generator->GetRequiresSupplementalResource() &&
				!Generator->HasSupplementalResource() && !OutGraph.Nodes[ConsumerNode].bProducing)
			{
				OutGraph.Nodes[ConsumerNode].bMissingInput = true;
			}
			const TSubclassOf<UFGItemDescriptor> Fuel = ResolveGeneratorFuel(Generator);
			if (Fuel)
			{
				OutGraph.Nodes[ConsumerNode].Rates.Add(MakeRate(Fuel, GeneratorFuelRatePerMinute(Generator, Fuel, false)));
				OutGraph.Nodes[ConsumerNode].DesignRates.Add(MakeRate(Fuel, GeneratorFuelRatePerMinute(Generator, Fuel, true)));
				if (const UFGInventoryComponent* FuelInventory = Generator->GetFuelInventory())
				{
					const float InternalAmount = static_cast<float>(FuelInventory->GetNumItems(Fuel));
					if (InternalAmount > 0.0f)
					{
						OutGraph.Nodes[ConsumerNode].Stocks.Add(MakeStock(
							Fuel, IsFluidClass(Fuel) ? InternalAmount / 1000.0f : InternalAmount));
					}
				}
				if (IsGasClass(Fuel)) { Ctx.GasNodes.Add(ConsumerNode); }
			}
			if (Generator->GetRequiresSupplementalResource())
			{
				if (TSubclassOf<UFGItemDescriptor> Supplemental = Generator->GetSupplementalResourceClass())
				{
					const float CurrentRate = Generator->IsProductionPaused()
						? 0.0f : Generator->GetSupplementalConsumptionRateCurrent() * 60.0f;
					const float DesignRate = Generator->GetSupplementalConsumptionRateMaximum() * 60.0f;
					OutGraph.Nodes[ConsumerNode].Rates.Add(MakeRate(Supplemental, CurrentRate));
					OutGraph.Nodes[ConsumerNode].DesignRates.Add(MakeRate(Supplemental, DesignRate));
					if (IsGasClass(Supplemental)) { Ctx.GasNodes.Add(ConsumerNode); }
				}
			}
			Ctx.ConsumerNodeByActor.Add(Buildable, ConsumerNode);

			// A nuclear generator is the one power source that PRODUCES something, and the waste
			// matters twice over: it has to leave the reactor or the reactor stops, and it is a
			// real ingredient downstream (Plutonium). Modelled exactly like a manufacturer's
			// output side — a Producer node paired to the fuel Consumer — so the belt walk
			// originates waste edges from it and the solver ties output to input.
			//
			// Read from the fuel descriptor rather than any table: GetSpentFuelClass and
			// GetAmountWasteCreated are properties of whatever nuclear fuel this installation
			// has, so a modded fuel with a modded waste product is captured on the same path.
			if (UClass* FuelClass = Fuel.Get())
			{
				if (FuelClass->IsChildOf(UFGItemDescriptorNuclearFuel::StaticClass()) && !IsFluidClass(Fuel))
				{
					const TSubclassOf<UFGItemDescriptorNuclearFuel> NuclearFuel = FuelClass;
					const TSubclassOf<UFGItemDescriptor> WasteClass =
						UFGItemDescriptorNuclearFuel::GetSpentFuelClass(NuclearFuel);
					const int32 WastePerFuelItem = UFGItemDescriptorNuclearFuel::GetAmountWasteCreated(NuclearFuel);
					if (WasteClass && WastePerFuelItem > 0)
					{
						// Waste count scales with fuel ITEMS burned. Nuclear fuel is solid, so the
						// display rate above is already an item rate — the fluid guard keeps that
						// assumption from silently breaking on a modded liquid fuel.
						const float CurrentWaste =
							GeneratorFuelRatePerMinute(Generator, Fuel, false) * WastePerFuelItem;
						const float DesignWaste =
							GeneratorFuelRatePerMinute(Generator, Fuel, true) * WastePerFuelItem;

						const int32 ProducerNode = Ctx.AddNode(
							ECPFlowNodeKind::Producer, Generator->mDisplayName.ToString(), Generator);
						OutGraph.Nodes[ProducerNode].Rates.Add(MakeRate(WasteClass, CurrentWaste));
						OutGraph.Nodes[ProducerNode].DesignRates.Add(MakeRate(WasteClass, DesignWaste));
						OutGraph.Nodes[ConsumerNode].PairedNodeIndex = ProducerNode;
						OutGraph.Nodes[ProducerNode].PairedNodeIndex = ConsumerNode;
						CaptureMachineState(OutGraph.Nodes[ProducerNode], Generator);
						Ctx.ProducerNodeByActor.Add(Buildable, ProducerNode);
					}
				}
			}
			continue;
		}

		if (AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Buildable))
		{
			TSubclassOf<UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe();
			if (!Recipe)
			{
				continue;
			}
			const float Duration = UFGRecipe::GetManufacturingDuration(Recipe);
			if (Duration <= SMALL_NUMBER)
			{
				continue;
			}
			const int32 ConsumerNode = Ctx.AddNode(ECPFlowNodeKind::Consumer, Manufacturer->mDisplayName.ToString(), Manufacturer);
			CaptureMachineState(OutGraph.Nodes[ConsumerNode], Manufacturer);
			// Machine-state coupling (FlowModel §3): unpowered, paused, or output-blocked
			// machines contribute zero CURRENT demand/supply. Missing-input demand remains: it
			// is the requirement that the factory is presently failing to serve.
			const bool bDead = (Manufacturer->RunsOnPower() && !Manufacturer->HasPower()) ||
				Manufacturer->IsProductionPaused() || OutGraph.Nodes[ConsumerNode].bOutputBlocked;
			const float InMult = Manufacturer->GetCurrentPotential() * Manufacturer->GetManufacturingSpeed();
			const float OutMult = InMult * Manufacturer->GetCurrentProductionBoost();
			const float DesignCraftsInPerMin = 60.0f * InMult / Duration;
			const float DesignCraftsOutPerMin = 60.0f * OutMult / Duration;
			const float CraftsInPerMin = bDead ? 0.0f : DesignCraftsInPerMin;
			const float CraftsOutPerMin = bDead ? 0.0f : DesignCraftsOutPerMin;

			for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(Manufacturer, Recipe))
			{
				if (Ingredient.ItemClass && Ingredient.Amount > 0)
				{
					OutGraph.Nodes[ConsumerNode].Rates.Add(MakeRate(Ingredient.ItemClass, ToDisplayRate(Ingredient.ItemClass, Ingredient.Amount * CraftsInPerMin)));
					if (IsGasClass(Ingredient.ItemClass)) { Ctx.GasNodes.Add(ConsumerNode); }
					OutGraph.Nodes[ConsumerNode].DesignRates.Add(MakeRate(Ingredient.ItemClass, ToDisplayRate(Ingredient.ItemClass, Ingredient.Amount * DesignCraftsInPerMin)));
				}
			}
			Ctx.ConsumerNodeByActor.Add(Buildable, ConsumerNode);

			const int32 ProducerNode = Ctx.AddNode(ECPFlowNodeKind::Producer, Manufacturer->mDisplayName.ToString(), Manufacturer);
			for (const FItemAmount& Product : UFGRecipe::GetProducts(Recipe))
			{
				if (Product.ItemClass && Product.Amount > 0)
				{
					OutGraph.Nodes[ProducerNode].Rates.Add(MakeRate(Product.ItemClass, ToDisplayRate(Product.ItemClass, Product.Amount * CraftsOutPerMin)));
					if (IsGasClass(Product.ItemClass)) { Ctx.GasNodes.Add(ProducerNode); }
					OutGraph.Nodes[ProducerNode].DesignRates.Add(MakeRate(Product.ItemClass, ToDisplayRate(Product.ItemClass, Product.Amount * DesignCraftsOutPerMin)));
				}
			}
			OutGraph.Nodes[ConsumerNode].PairedNodeIndex = ProducerNode;
			OutGraph.Nodes[ProducerNode].PairedNodeIndex = ConsumerNode;
			CaptureMachineState(OutGraph.Nodes[ProducerNode], Manufacturer);
			Ctx.ProducerNodeByActor.Add(Buildable, ProducerNode);
			continue;
		}
		if (AFGBuildableResourceExtractor* Extractor = Cast<AFGBuildableResourceExtractor>(Buildable))
		{
			IFGExtractableResourceInterface* Resource = Extractor->GetExtractableResource().GetInterface();
			const TSubclassOf<UFGResourceDescriptor> ResourceClass = Resource ? Resource->GetResourceClass() : nullptr;
			if (!ResourceClass)
			{
				continue;
			}
			const bool bDead = (Extractor->RunsOnPower() && !Extractor->HasPower()) || Extractor->IsProductionPaused();
			const int32 ProducerNode = Ctx.AddNode(ECPFlowNodeKind::Producer, Extractor->mDisplayName.ToString(), Extractor);
			// GetExtractionPerMinute is already in DISPLAY units for fluids (m3/min) --
			// unlike recipe FItemAmount, which is liters. No conversion here.
			OutGraph.Nodes[ProducerNode].Rates.Add(MakeRate(ResourceClass, bDead ? 0.0f : Extractor->GetExtractionPerMinute()));
			if (IsGasClass(ResourceClass)) { Ctx.GasNodes.Add(ProducerNode); }
			OutGraph.Nodes[ProducerNode].DesignRates.Add(MakeRate(ResourceClass, Extractor->GetExtractionPerMinute()));
			CaptureMachineState(OutGraph.Nodes[ProducerNode], Extractor);
			Ctx.ProducerNodeByActor.Add(Buildable, ProducerNode);
			continue;
		}
		if (AFGBuildableSplitterSmart* Smart = Cast<AFGBuildableSplitterSmart>(Buildable))
		{
			const int32 Node = Ctx.AddNode(ECPFlowNodeKind::Splitter, TEXT("Smart Splitter"), Smart);
			BuildSplitterRules(Smart, OutGraph.Nodes[Node].OutRules);
			Ctx.RoutingNodeByActor.Add(Buildable, Node);
			continue;
		}
		if (Cast<AFGBuildableAttachmentSplitter>(Buildable))
		{
			const int32 Node = Ctx.AddNode(ECPFlowNodeKind::Splitter, TEXT("Splitter"), Buildable);
			Ctx.RoutingNodeByActor.Add(Buildable, Node);
			continue;
		}
		if (Cast<AFGBuildableAttachmentMerger>(Buildable))
		{
			Ctx.RoutingNodeByActor.Add(Buildable, Ctx.AddNode(ECPFlowNodeKind::Merger, TEXT("Merger"), Buildable));
			continue;
		}
		if (Cast<AFGBuildableMergerPriority>(Buildable))
		{
			const int32 Node = Ctx.AddNode(ECPFlowNodeKind::Merger, TEXT("Priority Merger"), Buildable);
			OutGraph.Nodes[Node].bPriorityMerger = true;
			Ctx.RoutingNodeByActor.Add(Buildable, Node);
			continue;
		}
		if (AFGBuildableStorage* Storage = Cast<AFGBuildableStorage>(Buildable))
		{
			const int32 Node = Ctx.AddNode(ECPFlowNodeKind::Storage, Storage->mDisplayName.ToString(), Storage);
			OutGraph.Nodes[Node].bStorageFull = StorageIsFull(Storage);
			if (UFGInventoryComponent* Inventory = Storage->GetStorageInventory())
			{
				TArray<FInventoryStack> Stacks;
				Inventory->GetInventoryStacks(Stacks);
				for (const FInventoryStack& Stack : Stacks)
				{
					if (Stack.HasItems() && Stack.Item.GetItemClass())
					{
						OutGraph.Nodes[Node].Stocks.Add(MakeStock(Stack.Item.GetItemClass(), Stack.NumItems));
					}
				}
			}
			Ctx.RoutingNodeByActor.Add(Buildable, Node);
			continue;
		}
		if (AFGBuildablePipelinePump* Pump = Cast<AFGBuildablePipelinePump>(Buildable))
		{
			// Valves ARE pumps with zero head (FlowModel S11); mUserFlowLimit == 0 is CLOSED.
			const bool bValve = Pump->GetDesignHeadLift() <= KINDA_SMALL_NUMBER;
			const int32 Node = Ctx.AddNode(ECPFlowNodeKind::Passthrough, bValve ? TEXT("Valve") : TEXT("Pump"), Pump);
			// Contractible: by solve time the head gate has consumed pump semantics (budgets,
			// group breaks, closed valves -> blocked edges), leaving a pure passthrough.
			OutGraph.Nodes[Node].bContractible = true;
			Ctx.RoutingNodeByActor.Add(Buildable, Node);
			if (!bValve)
			{
				const bool bPumpDead = (Pump->RunsOnPower() && !Pump->HasPower()) || Pump->IsProductionPaused();
				if (!bPumpDead)
				{
					Ctx.PumpNodes.Add(Node);
					Ctx.HeadSourceByNode.Add(Node, Pump->GetActorLocation().Z / 100.0f + Pump->GetDesignHeadLift());
				}
			}
			if (FMath::IsNearlyZero(Pump->GetUserFlowLimit()))
			{
				Ctx.ClosedValveNodes.Add(Node);
			}
			continue;
		}
		if (Cast<AFGBuildablePipelineJunction>(Buildable))
		{
			const int32 JunctionNode = Ctx.AddNode(ECPFlowNodeKind::Passthrough, TEXT("Pipe Junction"), Buildable);
			OutGraph.Nodes[JunctionNode].bContractible = true;
			Ctx.RoutingNodeByActor.Add(Buildable, JunctionNode);
			continue;
		}
		if (AFGBuildablePipeReservoir* Reservoir = Cast<AFGBuildablePipeReservoir>(Buildable))
		{
			const int32 Node = Ctx.AddNode(ECPFlowNodeKind::Storage, Reservoir->mDisplayName.ToString(), Reservoir);
			// Fluid storage is transparent at steady state. Contracting tank/junction webs removes
			// bidirectional fixed-point oscillation while their stock is merged and retained.
			OutGraph.Nodes[Node].bContractible = true;
			Ctx.RoutingNodeByActor.Add(Buildable, Node);
			if (FFluidBox* Box = Reservoir->GetFluidBox())
			{
				OutGraph.Nodes[Node].bStorageFull = Box->MaxContent > 0.0f && Box->Content >= Box->MaxContent * 0.98f;
				if (const TSubclassOf<UFGItemDescriptor> Fluid = Reservoir->GetFluidDescriptor(); Fluid && Reservoir->GetFluidContent() > 0.0f)
				{
					OutGraph.Nodes[Node].Stocks.Add(MakeStock(Fluid, Reservoir->GetFluidContent()));
				}
				// Passive column head at top-of-fluid Z (the "tower head"; varies with fullness).
				const float FillPct = Box->MaxContent > 0.0f ? FMath::Clamp(Box->Content / Box->MaxContent, 0.0f, 1.0f) : 0.0f;
				if (FillPct > 0.01f)
				{
					Ctx.HeadSourceByNode.Add(Node, Reservoir->GetActorLocation().Z / 100.0f + Box->Height * FillPct);
				}
			}
			continue;
		}
		if (Cast<AFGBuildableResourceSink>(Buildable))
		{
			Ctx.RoutingNodeByActor.Add(Buildable, Ctx.AddNode(ECPFlowNodeKind::Sink, TEXT("AWESOME Sink"), Buildable));
			continue;
		}
	}

	// ---- pass 1b: drone ports (Phase T) ----------------------------------------------------
	// Ports register as TransportPort routing nodes BEFORE the edge passes so their belt
	// connections wire them onto their islands; the pairing then bridges islands with a
	// transport edge. Info actors are replicated but GetStation() is SERVER-ONLY: on a remote
	// client it returns null for every port, so the whole drone network silently disappears
	// rather than degrading. That is why callers must honour bAuthoritative instead of trusting
	// a clean-looking graph -- capture does NOT necessarily run on the server.
	struct FDronePortPair { int32 FromNode = INDEX_NONE; int32 ToNode = INDEX_NONE; };
	TArray<FDronePortPair> DronePairs;
	if (AFGDroneSubsystem* DroneSubsystem = AFGDroneSubsystem::Get(World))
	{
		TMap<AFGDroneStationInfo*, int32> PortNodeByInfo;
		for (AFGDroneStationInfo* Info : DroneSubsystem->GetAllStations())
		{
			AFGBuildableDroneStation* Station = Info ? Info->GetStation() : nullptr;
			if (!Station)
			{
				continue;
			}
			const int32 Node = Ctx.AddNode(ECPFlowNodeKind::TransportPort, Station->mDisplayName.ToString(), Station);
			Ctx.RoutingNodeByActor.Add(Station, Node);
			PortNodeByInfo.Add(Info, Node);
		}
		for (const TPair<AFGDroneStationInfo*, int32>& Pair : PortNodeByInfo)
		{
			AFGDroneStationInfo* Paired = Pair.Key->GetPairedStation();
			const int32* ToNode = Paired ? PortNodeByInfo.Find(Paired) : nullptr;
			if (ToNode && *ToNode != Pair.Value)
			{
				DronePairs.Add({ Pair.Value, *ToNode });
			}
		}
	}

	// ---- pass 1c: train cargo platforms (Phase T) --------------------------------------------
	// Each train's timetable stops resolve to stations, stations to their platform chains;
	// cargo platforms register as TransportPort nodes (their belt/pipe connections wire the
	// islands). Per train: every LOAD platform bridges to every UNLOAD platform at the
	// train's OTHER stops -- batch service, capacity unknowable, so connectivity only.
	struct FTrainService { TArray<int32> LoadNodes; TArray<int32> UnloadNodes; };
	TArray<FTrainService> TrainServices;
	if (AFGRailroadSubsystem* Railroads = AFGRailroadSubsystem::Get(World))
	{
		TMap<AFGBuildableTrainPlatformCargo*, int32> PlatformNodeByActor;
		TArray<AFGTrain*> Trains;
		Railroads->GetAllTrains(Trains);
		for (AFGTrain* Train : Trains)
		{
			AFGRailroadTimeTable* TimeTable = Train ? Train->GetTimeTable() : nullptr;
			if (!TimeTable)
			{
				continue;
			}
			TArray<FTimeTableStop> Stops;
			TimeTable->GetStops(Stops);
			FTrainService Service;
			for (const FTimeTableStop& Stop : Stops)
			{
				AFGBuildableRailroadStation* Station = Stop.Station ? Stop.Station->GetStation() : nullptr;
				if (!Station)
				{
					continue;
				}
				UFGTrainPlatformConnection* Cursor = Station->GetStationOutputConnection();
				for (int32 Hop = 0; Hop < 64 && Cursor; ++Hop)
				{
					UFGTrainPlatformConnection* Peer = Cursor->GetConnectedTo();
					AFGBuildableTrainPlatform* Platform = Peer ? Peer->GetPlatformOwner() : nullptr;
					if (!Platform)
					{
						break;
					}
					if (AFGBuildableTrainPlatformCargo* Cargo = Cast<AFGBuildableTrainPlatformCargo>(Platform))
					{
						int32 Node;
						if (const int32* Existing = PlatformNodeByActor.Find(Cargo))
						{
							Node = *Existing;
						}
						else
						{
							Node = Ctx.AddNode(ECPFlowNodeKind::TransportPort, Cargo->mDisplayName.ToString(), Cargo);
							Ctx.RoutingNodeByActor.Add(Cargo, Node);
							PlatformNodeByActor.Add(Cargo, Node);
						}
						(Cargo->GetIsInLoadMode() ? Service.LoadNodes : Service.UnloadNodes).AddUnique(Node);
					}
					Cursor = Platform->GetConnectionInOppositeDirection(Peer);
				}
			}
			if (Service.LoadNodes.Num() > 0 && Service.UnloadNodes.Num() > 0)
			{
				TrainServices.Add(MoveTemp(Service));
			}
		}
	}

	// ---- pass 1d: truck routes (Phase T) ------------------------------------------------------
	// An autopiloted vehicle's route is an ordered list of path-node GUIDs; the ones that
	// resolve to docking stations define the service. Same bridge semantics as trains:
	// load stations -> unload stations, connectivity only (no filters exist on stations).
	if (AFGVehicleSubsystem* Vehicles = AFGVehicleSubsystem::Get(World))
	{
		TMap<AFGBuildableDockingStation*, int32> StationNodeByActor;
		for (AFGWheeledVehicleIdentifier* Vehicle : Vehicles->GetAllVehicles())
		{
			if (!Vehicle || !Vehicle->IsAutopilotEnabled())
			{
				continue; // a manually driven truck is not a standing route
			}
			FTrainService Service;
			for (const FGuid& PathNodeGuid : Vehicle->GetVehicleRoute())
			{
				AFGDockingStationIdentifier* Identifier = Vehicles->FindDockingStationIdentifierForPathNodeGuid(PathNodeGuid);
				AFGBuildableDockingStation* Station = Identifier ? Identifier->GetStation() : nullptr;
				if (!Station)
				{
					continue;
				}
				int32 Node;
				if (const int32* Existing = StationNodeByActor.Find(Station))
				{
					Node = *Existing;
				}
				else if (const int32* Routed = Ctx.RoutingNodeByActor.Find(Station))
				{
					Node = *Routed; // shared by another vehicle's route
					StationNodeByActor.Add(Station, Node);
				}
				else
				{
					Node = Ctx.AddNode(ECPFlowNodeKind::TransportPort, Station->mDisplayName.ToString(), Station);
					Ctx.RoutingNodeByActor.Add(Station, Node);
					StationNodeByActor.Add(Station, Node);
				}
				(Station->GetIsInLoadMode() ? Service.LoadNodes : Service.UnloadNodes).AddUnique(Node);
			}
			if (Service.LoadNodes.Num() > 0 && Service.UnloadNodes.Num() > 0)
			{
				TrainServices.Add(MoveTemp(Service));
			}
		}
	}

	// ---- pass 2: edges (walk belt runs from every captured actor's outputs) ---------------
	const auto SourceNodeFor = [&Ctx](AFGBuildable* Actor) -> int32
	{
		if (const int32* Producer = Ctx.ProducerNodeByActor.Find(Actor))
		{
			return *Producer; // machine outputs originate from the producer side
		}
		if (const int32* Routing = Ctx.RoutingNodeByActor.Find(Actor))
		{
			return *Routing;
		}
		return INDEX_NONE;
	};
	const auto TargetNodeFor = [&Ctx](AFGBuildable* Actor) -> int32
	{
		if (const int32* Consumer = Ctx.ConsumerNodeByActor.Find(Actor))
		{
			return *Consumer; // machine inputs land on the consumer side
		}
		if (const int32* Routing = Ctx.RoutingNodeByActor.Find(Actor))
		{
			return *Routing;
		}
		return INDEX_NONE;
	};

	TArray<AFGBuildable*> SourceActors;
	Ctx.ProducerNodeByActor.GetKeys(SourceActors);
	TArray<AFGBuildable*> RoutingActors;
	Ctx.RoutingNodeByActor.GetKeys(RoutingActors);
	SourceActors.Append(RoutingActors);

	for (AFGBuildable* Actor : SourceActors)
	{
		const int32 FromNode = SourceNodeFor(Actor);
		if (FromNode == INDEX_NONE)
		{
			continue;
		}
		// Deterministic port ordering so splitter OutRules line up with edge FromPortIndex.
		TInlineComponentArray<UFGFactoryConnectionComponent*> Components(Actor);
		UFGFactoryConnectionComponent::SortComponentList(Components);
		int32 OutputOrdinal = 0;
		for (UFGFactoryConnectionComponent* Component : Components)
		{
			if (Component->GetDirection() != EFactoryConnectionDirection::FCD_OUTPUT)
			{
				continue;
			}
			const int32 PortIndex = OutputOrdinal++;
			if (!Component->IsConnected())
			{
				continue;
			}
			float Capacity = 0.0f;
			UFGFactoryConnectionComponent* TerminalConnection = nullptr;
			AFGBuildable* Terminal = WalkBeltRun(Component, 512, Capacity, TerminalConnection);
			if (!Terminal)
			{
				continue;
			}
			const int32 ToNode = TargetNodeFor(Terminal);
			if (ToNode == INDEX_NONE || ToNode == FromNode)
			{
				continue;
			}
			FCPFlowEdge Edge;
			Edge.FromNode = FromNode;
			Edge.ToNode = ToNode;
			Edge.CapacityPerMinute = Capacity;
			Edge.FromPortIndex = OutGraph.Nodes[FromNode].Kind == ECPFlowNodeKind::Splitter ? PortIndex : INDEX_NONE;
			if (AFGBuildableMergerPriority* PriorityMerger = Cast<AFGBuildableMergerPriority>(Terminal))
			{
				Edge.MergerInputPriority = PriorityMerger->GetPriorityByInputConnection(TerminalConnection);
			}
			OutGraph.Edges.Add(MoveTemp(Edge));
		}
	}

	// ---- pass 3: pipe edges (FlowModel S11) ------------------------------------------------
	// Machines expose typed pipe connections (PCT_PRODUCER = output, PCT_CONSUMER = input);
	// junctions/pumps/reservoirs expose ANY-typed ones and get edges in both directions from
	// each side walking its own connections. Reverse entry through a typed port is refused.
	const float ProductionHeadM = SettingsProductionHeadM();
	TMap<int32, float> ProducerHeadByNode; // producer node -> best head budget among its fluid outputs

	for (AFGBuildable* Actor : SourceActors)
	{
		const int32 FromNode = SourceNodeFor(Actor);
		if (FromNode == INDEX_NONE)
		{
			continue;
		}
		TInlineComponentArray<UFGPipeConnectionComponent*> PipeComponents(Actor);
		for (UFGPipeConnectionComponent* Component : PipeComponents)
		{
			if (!Component || !Component->IsConnected())
			{
				continue;
			}
			const EPipeConnectionType FromType = Component->GetPipeConnectionType();
			if (FromType == EPipeConnectionType::PCT_CONSUMER || FromType == EPipeConnectionType::PCT_SNAP_ONLY)
			{
				continue; // inputs do not originate flow
			}
			float Capacity = 0.0f;
			float MaxZMeters = 0.0f;
			UFGPipeConnectionComponentBase* TerminalConnection = nullptr;
			AFGBuildable* Terminal = WalkPipeRun(Component, 4096, Capacity, MaxZMeters, TerminalConnection);
			if (!Terminal)
			{
				continue;
			}
			if (const UFGPipeConnectionComponent* TerminalPipe = Cast<UFGPipeConnectionComponent>(TerminalConnection))
			{
				if (TerminalPipe->GetPipeConnectionType() == EPipeConnectionType::PCT_PRODUCER)
				{
					continue; // never flow INTO an output port
				}
			}
			const int32 ToNode = TargetNodeFor(Terminal);
			if (ToNode == INDEX_NONE || ToNode == FromNode)
			{
				continue;
			}
			// Producer-type outputs seed the head budget: actor Z + settings pressure when the
			// connection applies it (the per-connection checkbox, FlowModel S11.1). ANY node
			// kind qualifies -- freight platforms and docking stations drain like producers;
			// gating on Producer-kind left TransportPort outputs at -inf and blocked every
			// pipe downstream of an unloader.
			if (FromType == EPipeConnectionType::PCT_PRODUCER)
			{
				const float Head = Component->GetComponentLocation().Z / 100.0f
					+ (AppliesProductionPressure(Component) ? ProductionHeadM : 0.0f);
				float& Best = ProducerHeadByNode.FindOrAdd(FromNode, -BIG_NUMBER);
				Best = FMath::Max(Best, Head);
			}
			// Fluid form: gas lines skip gravity entirely (any connected gas line is reachable).
			TSubclassOf<UFGItemDescriptor> Fluid = Component->GetFluidDescriptor();
			if (!Fluid)
			{
				if (const UFGPipeConnectionComponent* TerminalPipe = Cast<UFGPipeConnectionComponent>(TerminalConnection))
				{
					Fluid = TerminalPipe->GetFluidDescriptor();
				}
			}
			const bool bGas = Fluid && UFGItemDescriptor::GetForm(Fluid) == EResourceForm::RF_GAS;

			FCPFlowEdge Edge;
			Edge.FromNode = FromNode;
			Edge.ToNode = ToNode;
			Edge.CapacityPerMinute = Capacity;
			Edge.bFluid = true;
			Edge.bBlocked = Ctx.ClosedValveNodes.Contains(FromNode) || Ctx.ClosedValveNodes.Contains(ToNode);
			const int32 EdgeIndex = OutGraph.Edges.Add(MoveTemp(Edge));
			Ctx.PipeEdges.Add({ EdgeIndex, MaxZMeters, bGas });
		}
	}

	// ---- pass 4: transport edges (Phase T) --------------------------------------------------
	for (const FDronePortPair& Pair : DronePairs)
	{
		FCPFlowEdge Edge;
		Edge.FromNode = Pair.FromNode;
		Edge.ToNode = Pair.ToNode;
		Edge.CapacityPerMinute = 1.0e9f; // batched route, rate unknown: connected, never guessed
		Edge.bTransport = true;
		OutGraph.Edges.Add(MoveTemp(Edge));
	}

	for (const FTrainService& Service : TrainServices)
	{
		for (int32 LoadNode : Service.LoadNodes)
		{
			for (int32 UnloadNode : Service.UnloadNodes)
			{
				if (LoadNode == UnloadNode)
				{
					continue;
				}
				FCPFlowEdge Edge;
				Edge.FromNode = LoadNode;
				Edge.ToNode = UnloadNode;
				Edge.CapacityPerMinute = 1.0e9f;
				Edge.bTransport = true;
				OutGraph.Edges.Add(MoveTemp(Edge));
			}
		}
	}

	// ---- head-lift connectivity gate (FlowModel S11: static budget, groups break at pumps) --
	{
		for (const TPair<int32, float>& Pair : ProducerHeadByNode)
		{
			float& Existing = Ctx.HeadSourceByNode.FindOrAdd(Pair.Key, -BIG_NUMBER);
			Existing = FMath::Max(Existing, Pair.Value);
		}
		TArray<float> Budget;
		Budget.Init(-BIG_NUMBER, OutGraph.Nodes.Num());
		for (const TPair<int32, float>& Pair : Ctx.HeadSourceByNode)
		{
			Budget[Pair.Key] = Pair.Value;
		}
		// Gas networks skip gravity entirely (FlowModel S11) — but mid-manifold connections
		// often report a null fluid descriptor, so descriptor sniffing alone under-detects.
		// Spread gas-ness from gas-touching machines across the pipe adjacency: pipes cannot
		// mix fluids, so one gas endpoint marks the whole network.
		{
			bool bGasChanged = true;
			while (bGasChanged)
			{
				bGasChanged = false;
				for (FCaptureContext::FPipeEdgeMeta& Meta : Ctx.PipeEdges)
				{
					if (Meta.bGas)
					{
						continue;
					}
					const FCPFlowEdge& Edge = OutGraph.Edges[Meta.EdgeIndex];
					if (Ctx.GasNodes.Contains(Edge.FromNode) || Ctx.GasNodes.Contains(Edge.ToNode))
					{
						Meta.bGas = true;
						bGasChanged |= !Ctx.GasNodes.Contains(Edge.FromNode) || !Ctx.GasNodes.Contains(Edge.ToNode);
						Ctx.GasNodes.Add(Edge.FromNode);
						Ctx.GasNodes.Add(Edge.ToNode);
					}
				}
			}
		}

		// Relax: a budget crosses an edge when it clears the run's max Z; pumps re-seed their
		// own budget (group break) instead of passing the inherited one along.
		const float ToleranceM = 0.5f;
		bool bChanged = true;
		for (int32 Pass = 0; Pass < 64 && bChanged; ++Pass)
		{
			bChanged = false;
			for (const FCaptureContext::FPipeEdgeMeta& Meta : Ctx.PipeEdges)
			{
				const FCPFlowEdge& Edge = OutGraph.Edges[Meta.EdgeIndex];
				if (Edge.bBlocked || Meta.bGas)
				{
					continue;
				}
				if (Budget[Edge.FromNode] + ToleranceM < Meta.MaxZM)
				{
					continue;
				}
				const float Carried = Ctx.PumpNodes.Contains(Edge.ToNode)
					? Budget[Edge.ToNode] // pump already seeds its own head
					: Budget[Edge.FromNode];
				if (Carried > Budget[Edge.ToNode] + KINDA_SMALL_NUMBER)
				{
					Budget[Edge.ToNode] = Carried;
					bChanged = true;
				}
			}
		}
		for (const FCaptureContext::FPipeEdgeMeta& Meta : Ctx.PipeEdges)
		{
			FCPFlowEdge& Edge = OutGraph.Edges[Meta.EdgeIndex];
			if (!Edge.bBlocked && !Meta.bGas && Budget[Edge.FromNode] + ToleranceM < Meta.MaxZM)
			{
				Edge.bBlocked = true; // head-capped: the line cannot climb (S11.5)
			}
		}
	}

	// Junction manifolds form dense bidirectional cycle webs; collapse them so the solver
	// converges in tens of iterations instead of crawling (head gate already ran above --
	// blocked edges keep their clusters apart).
	FCPFlowSolver::CollapseContractibleClusters(OutGraph);

	return true;
}
