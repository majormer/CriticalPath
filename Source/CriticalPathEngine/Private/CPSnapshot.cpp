// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPSnapshot.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableFactory.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Buildables/FGBuildableResourceExtractor.h"
#include "Buildables/FGBuildableStorage.h"
#include "Buildables/FGCentralStorageContainer.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableDroneStation.h"
#include "Buildables/FGBuildableTrainPlatformCargo.h"
#include "FGBuildableSubsystem.h"
#include "FGCentralStorageSubsystem.h"
#include "FGCharacterPlayer.h"
#include "FGConveyorItem.h"
#include "FGFactoryConnectionComponent.h"
#include "FGGamePhase.h"
#include "FGGamePhaseManager.h"
#include "FGInventoryComponent.h"
#include "FGPlayerController.h"
#include "FGPipeConnectionComponent.h"
#include "FGRecipe.h"
#include "FGRecipeManager.h"
#include "FGSchematic.h"
#include "FGSchematicManager.h"
#include "FGStatisticsSubsystem.h"
#include "ItemAmount.h"
#include "Resources/FGExtractableResourceInterface.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGItemDescriptorNuclearFuel.h"
#include "Resources/FGResourceDescriptor.h"
#include "Unlocks/FGUnlockRecipe.h"

namespace
{
// ---- item helpers ---------------------------------------------------------------------------

FCPItemRef MakeItemRef(TSubclassOf<UFGItemDescriptor> ItemClass)
{
	FCPItemRef Ref;
	if (ItemClass)
	{
		Ref.Name = UFGItemDescriptor::GetItemName(ItemClass).ToString();
		Ref.DescriptorClassPath = ItemClass->GetPathName();
		const EResourceForm Form = UFGItemDescriptor::GetForm(ItemClass);
		Ref.Form = Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS
			? ECPItemForm::Fluid : ECPItemForm::Solid;
	}
	return Ref;
}

// Fluids/gases are liters internally, m^3 in every UI. All engine amounts are display units.
bool IsFluid(TSubclassOf<UFGItemDescriptor> ItemClass)
{
	if (!ItemClass)
	{
		return false;
	}
	const EResourceForm Form = UFGItemDescriptor::GetForm(ItemClass);
	return Form == EResourceForm::RF_LIQUID || Form == EResourceForm::RF_GAS;
}

float ToDisplay(TSubclassOf<UFGItemDescriptor> ItemClass, float Raw)
{
	return IsFluid(ItemClass) ? Raw / 1000.0f : Raw;
}

FString NetModeToString(ENetMode NetMode)
{
	switch (NetMode)
	{
	case NM_Standalone: return TEXT("Standalone");
	case NM_DedicatedServer: return TEXT("DedicatedServer");
	case NM_ListenServer: return TEXT("ListenServer");
	case NM_Client: return TEXT("Client");
	default: return TEXT("Unknown");
	}
}

// ---- recipe selection -------------------------------------------------------------------------

/** Ranked default-recipe policy, shared by the research-gap walk and the catalog walk:
 *  non-alternate primary-product > primary-product > non-alternate byproduct > anything.
 *  FindRecipesByProduct matches ANY product slot including byproducts — without the primary
 *  preference, the Ficsonium Fuel Rod recipe becomes Dark Matter Residue's "default" and both
 *  walks route through fake circular dependencies (live-caught). */
TSubclassOf<UFGRecipe> PickRankedFrom(const TArray<TSubclassOf<UFGRecipe>>& Candidates, TSubclassOf<UFGItemDescriptor> Item)
{
	TSubclassOf<UFGRecipe> Best = nullptr;
	int32 BestRank = -1;
	for (const TSubclassOf<UFGRecipe>& Recipe : Candidates)
	{
		const TArray<FItemAmount> Products = UFGRecipe::GetProducts(Recipe);
		const bool bPrimary = Products.Num() > 0 && Products[0].ItemClass == Item;
		const bool bAlternate = UFGRecipe::GetRecipeName(Recipe).ToString().StartsWith(TEXT("Alternate"));
		const int32 Rank = (bPrimary ? 2 : 0) + (!bAlternate ? 1 : 0);
		if (Rank > BestRank)
		{
			Best = Recipe;
			BestRank = Rank;
		}
	}
	return Best;
}

TSubclassOf<UFGRecipe> PickDefaultRecipeRanked(AFGRecipeManager* RecipeManager, TSubclassOf<UFGItemDescriptor> Item, bool& bOutAlternates)
{
	const TArray<TSubclassOf<UFGRecipe>> Unlocked = RecipeManager->FindRecipesByProduct(Item, true, true);
	bOutAlternates = Unlocked.Num() > 1;
	return PickRankedFrom(Unlocked, Item);
}

/** As above, but falls back to a LOCKED recipe when nothing is unlocked yet.
 *
 *  Read straight from AFGRecipeManager, so it is whatever this installation actually has —
 *  modded recipes and modded objectives included. Nothing here is hardcoded or table-driven.
 *
 *  This is what lets the plan see past a research wall. Without it, a locked part is a dead end:
 *  we know it is locked and nothing more, so a step placed after the unlock silently claims the
 *  unlock was the last obstacle. It frequently is not — a milestone often unlocks a part AND an
 *  ingredient that part needs, which is a whole extra line to build. bOutLocked is the caller's
 *  obligation: capture the knowledge, keep saying "locked". */
TSubclassOf<UFGRecipe> PickRecipeAllowingLocked(AFGRecipeManager* RecipeManager, TSubclassOf<UFGItemDescriptor> Item,
	bool& bOutAlternates, bool& bOutLocked)
{
	bOutLocked = false;
	if (const TSubclassOf<UFGRecipe> Unlocked = PickDefaultRecipeRanked(RecipeManager, Item, bOutAlternates))
	{
		return Unlocked;
	}
	// onlyAvailableRecipes = false: the manager's full set, locked included.
	const TArray<TSubclassOf<UFGRecipe>> All = RecipeManager->FindRecipesByProduct(Item, false, false);
	TArray<TSubclassOf<UFGRecipe>> Locked;
	for (const TSubclassOf<UFGRecipe>& Recipe : All)
	{
		if (!RecipeManager->IsRecipeAvailable(Recipe))
		{
			Locked.Add(Recipe);
		}
	}
	bOutAlternates = Locked.Num() > 1;
	const TSubclassOf<UFGRecipe> Best = PickRankedFrom(Locked, Item);
	bOutLocked = Best != nullptr;
	return Best;
}

// ---- machine state --------------------------------------------------------------------------

struct FMachineState
{
	bool bProducing = false;
	ECPIdleReason IdleReason = ECPIdleReason::None;
	float ProductivityPercent = 0.0f;

	// Independent facts (an idle machine can be unpowered AND unfed AND clogged at once —
	// the game's single idle reason stops at the first, which hid concurrent conditions).
	bool bNoPower = false;
	bool bMissingInput = false;
	bool bOutputBlocked = false;

	/** An item is sitting at the delivery end of a feeding belt that this machine's recipe does
	 *  not use. Belts are FIFO, so it never enters and nothing behind it can either: the line is
	 *  jammed permanently even though the belt is connected and full. Distinct from a shortage —
	 *  the supply is present and stuck, so upstream capacity is not the problem. */
	FString JammedByItemName;
};

float NormalizePercent(float Value)
{
	return Value <= 1.5f ? Value * 100.0f : Value;
}

FMachineState ReadMachineState(AFGBuildableFactory* Factory)
{
	FMachineState State;
	State.bProducing = Factory->IsProducing();
	State.ProductivityPercent = NormalizePercent(Factory->GetProductivity());
	if (State.bProducing)
	{
		return State;
	}

	// Independent facts FIRST — evaluated for every idle machine regardless of which condition
	// the game's single idle reason would report. Shortage/blockage diagnosis is only
	// meaningful on idle machines: a running machine's buffers legitimately breathe between
	// cycles (prototype ground truth).
	State.bNoPower = Factory->RunsOnPower() && !Factory->HasPower();
	if (AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Factory))
	{
		if (TSubclassOf<UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe())
		{
			if (UFGInventoryComponent* InputInventory = Manufacturer->GetInputInventory())
			{
				for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(Manufacturer, Recipe))
				{
					if (Ingredient.ItemClass && Ingredient.Amount > 0 && !InputInventory->HasItems(Ingredient.ItemClass, Ingredient.Amount))
					{
						State.bMissingInput = true;
						break;
					}
				}
			}
			// A foreign item at a belt's delivery end blocks that belt forever. Only inspect
			// belts feeding THIS machine: elsewhere a mixed belt is a legitimate sushi line, and
			// smart/programmable splitters exist precisely to sort them. A manufacturer input is
			// a terminus, so anything it cannot consume is genuinely stranded there.
			if (State.bMissingInput)
			{
				TSet<TSubclassOf<UFGItemDescriptor>> Ingredients;
				for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(Manufacturer, Recipe))
				{
					if (Ingredient.ItemClass) { Ingredients.Add(Ingredient.ItemClass); }
				}
				TInlineComponentArray<UFGFactoryConnectionComponent*> Connections(Manufacturer);
				for (UFGFactoryConnectionComponent* Connection : Connections)
				{
					if (!State.JammedByItemName.IsEmpty()) { break; }
					if (!Connection || Connection->GetDirection() != EFactoryConnectionDirection::FCD_INPUT ||
						!Connection->IsConnected())
					{
						continue;
					}
					UFGFactoryConnectionComponent* Peer = Connection->GetConnection();
					AFGBuildableConveyorBelt* Belt = Peer ? Cast<AFGBuildableConveyorBelt>(Peer->GetOwner()) : nullptr;
					if (!Belt)
					{
						continue;
					}
					TArray<FConveyorBeltItem*> BeltItems;
					Belt->GetConveyorBeltItems(BeltItems);
					for (const FConveyorBeltItem* BeltItem : BeltItems)
					{
						const TSubclassOf<UFGItemDescriptor> ItemClass = BeltItem ? BeltItem->Item.GetItemClass() : nullptr;
						if (ItemClass && !Ingredients.Contains(ItemClass))
						{
							State.JammedByItemName = UFGItemDescriptor::GetItemName(ItemClass).ToString();
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
						const int32 SlotSize = OutputInventory->GetSlotSizeForItem(Index, Stack.Item.GetItemClass(), &Stack.Item);
						if (SlotSize > 0 && Stack.NumItems >= SlotSize)
						{
							State.bOutputBlocked = true;
							break;
						}
					}
				}
			}
		}
	}

	// Single primary reason (worst-first) kept for classification and benign-state rules.
	if (State.bNoPower)
	{
		State.IdleReason = ECPIdleReason::NoPower;
	}
	else if (Factory->IsProductionPaused())
	{
		State.IdleReason = ECPIdleReason::Paused;
	}
	else if (!Factory->IsConfigured())
	{
		State.IdleReason = ECPIdleReason::NotConfigured;
	}
	else if (State.bMissingInput)
	{
		State.IdleReason = ECPIdleReason::MissingInput;
	}
	else if (State.bOutputBlocked)
	{
		State.IdleReason = ECPIdleReason::OutputBlocked;
	}
	else
	{
		const EProductionStatus Status = Factory->GetProductionIndicatorStatus();
		if (Status == EProductionStatus::IS_STANDBY)
		{
			State.IdleReason = ECPIdleReason::Standby;
		}
		else if (!Factory->CanProduce())
		{
			State.IdleReason = ECPIdleReason::CannotProduce;
		}
		else if (Status == EProductionStatus::IS_ERROR)
		{
			State.IdleReason = ECPIdleReason::ProductionError;
		}
		else
		{
			State.IdleReason = ECPIdleReason::Unknown;
		}
	}
	return State;
}

struct FProductAggregate
{
	FCPProductRow Row;
	float ProductivityTotal = 0.0f;
};

struct FProducerCapture
{
	AFGBuildableFactory* Producer = nullptr;
	TSubclassOf<UFGItemDescriptor> ItemClass;
	FString ItemName;
	float CapacityPerMinute = 0.0f;
	UFGInventoryComponent* OutputInventory = nullptr;
};

struct FConsumerCapture
{
	AFGBuildableManufacturer* Consumer = nullptr;
	struct FIngredient
	{
		TSubclassOf<UFGItemDescriptor> ItemClass;
		FCPItemRef Item;
		float DemandPerMinute = 0.0f;
	};
	TArray<FIngredient> Ingredients;
	bool bProducing = false;
};

bool IsFactoryOutput(EFactoryConnectionDirection Direction)
{
	return Direction == EFactoryConnectionDirection::FCD_OUTPUT || Direction == EFactoryConnectionDirection::FCD_ANY;
}

bool IsFactoryInput(EFactoryConnectionDirection Direction)
{
	return Direction == EFactoryConnectionDirection::FCD_INPUT || Direction == EFactoryConnectionDirection::FCD_ANY;
}

bool IsPipeOutput(EPipeConnectionType Type)
{
	return Type == EPipeConnectionType::PCT_PRODUCER || Type == EPipeConnectionType::PCT_ANY;
}

bool IsPipeInput(EPipeConnectionType Type)
{
	return Type == EPipeConnectionType::PCT_CONSUMER || Type == EPipeConnectionType::PCT_ANY;
}

bool IsConnectivityEndpoint(const AActor* Actor)
{
	return Cast<AFGBuildableManufacturer>(Actor) ||
		Cast<AFGBuildableResourceExtractor>(Actor) ||
		Cast<AFGBuildableDockingStation>(Actor) ||
		Cast<AFGBuildableDroneStation>(Actor) ||
		Cast<AFGBuildableTrainPlatformCargo>(Actor);
}

/** Port filters are authoritative for machines with several products/ingredients. If a runtime
 *  class has no filter, form-correct fallback still handles ordinary single-port machines. */
bool FactoryPortCarries(UFGFactoryConnectionComponent* Connection, TSubclassOf<UFGItemDescriptor> ItemClass)
{
	if (!Connection || !ItemClass || IsFluid(ItemClass))
	{
		return false;
	}
	if (UFGInventoryComponent* Inventory = Connection->GetInventory())
	{
		const TSubclassOf<UFGItemDescriptor> Allowed = Inventory->GetAllowedItemOnIndex(Connection->GetInventoryAccessIndex());
		if (Allowed)
		{
			return Allowed == ItemClass;
		}
	}
	return true;
}

bool PipePortCarries(UFGPipeConnectionComponentBase* Connection, UFGInventoryComponent* Inventory,
	TSubclassOf<UFGItemDescriptor> ItemClass)
{
	if (!Connection || !ItemClass || !IsFluid(ItemClass))
	{
		return false;
	}
	if (UFGPipeConnectionComponent* Pipe = Cast<UFGPipeConnectionComponent>(Connection))
	{
		if (Inventory)
		{
			const TSubclassOf<UFGItemDescriptor> Allowed = Inventory->GetAllowedItemOnIndex(Pipe->GetInventoryAccessIndex());
			if (Allowed)
			{
				return Allowed == ItemClass;
			}
		}
	}
	return true;
}

// Condition counters are INDEPENDENT (a machine can be unpowered AND unfed AND clogged at
// once), so they may overlap; Paused/Standby stay exclusive via the primary reason.
void AccumulateState(FCPProductRow& Row, const FMachineState& State)
{
	Row.ConfiguredBuildings++;
	Row.ProducingBuildings += State.bProducing ? 1 : 0;
	if (!State.bProducing)
	{
		Row.NoPowerBuildings += State.bNoPower ? 1 : 0;
		Row.MissingInputBuildings += State.bMissingInput ? 1 : 0;
		Row.OutputBlockedBuildings += State.bOutputBlocked ? 1 : 0;
		Row.PausedBuildings += State.IdleReason == ECPIdleReason::Paused ? 1 : 0;
		Row.StandbyBuildings += State.IdleReason == ECPIdleReason::Standby ? 1 : 0;
		if (!State.JammedByItemName.IsEmpty())
		{
			Row.InputJammedBuildings++;
			if (Row.JammedByItemName.IsEmpty()) { Row.JammedByItemName = State.JammedByItemName; }
		}
	}
}

void AccumulateDemandState(FCPDemandRow& Row, const FMachineState& State)
{
	Row.ConsumingBuildings++;
	if (!State.bProducing)
	{
		Row.NoPowerBuildings += State.bNoPower ? 1 : 0;
		Row.MissingInputBuildings += State.bMissingInput ? 1 : 0;
		Row.OutputBlockedBuildings += State.bOutputBlocked ? 1 : 0;
		Row.PausedBuildings += State.IdleReason == ECPIdleReason::Paused ? 1 : 0;
		Row.StandbyBuildings += State.IdleReason == ECPIdleReason::Standby ? 1 : 0;
	}
}
} // namespace

bool FCPSnapshotCollector::Collect(UObject* WorldContext, const FCPSnapshotParams& Params, FCPFactorySnapshot& OutSnapshot, FString& OutError)
{
	OutSnapshot = FCPFactorySnapshot();
	OutError.Reset();

	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		OutError = TEXT("No world available for snapshot collection.");
		return false;
	}

	OutSnapshot.WorldTimeSeconds = World->GetTimeSeconds();
	OutSnapshot.NetMode = NetModeToString(World->GetNetMode());
	// Standalone and both server roles read real production state; a remote client does not.
	OutSnapshot.bAuthoritative = World->GetNetMode() != NM_Client;

	AFGBuildableSubsystem* BuildableSubsystem = AFGBuildableSubsystem::Get(World);
	if (!BuildableSubsystem)
	{
		OutError = TEXT("Buildable subsystem unavailable.");
		return false;
	}

	// ---- observed-stats availability (never used for rates; reported for consumers) ----------
	if (AFGStatisticsSubsystem* Statistics = AFGStatisticsSubsystem::Get(World))
	{
		OutSnapshot.bObservedStatsAvailable = Statistics->mItemsProducedPerFrequencyMap.Num() > 0 &&
			World->GetTimeSeconds() >= 300.0f;
	}

	// ---- manufacturers: products, demands, recipe edges --------------------------------------
	TMap<FString, FProductAggregate> ProductAggregates;
	TMap<FString, FCPDemandRow> DemandAggregates;
	// Classes of every ingredient referenced by a configured edge — with the objective items,
	// these seed the catalog walk (an ingredient with no product row is a no-producer frontier).
	TMap<FString, TSubclassOf<UFGItemDescriptor>> IngredientClasses;
	TMap<FString, FCPRecipeEdge> EdgeMap;
	TArray<FProducerCapture> ProducerCaptures;
	TArray<FConsumerCapture> ConsumerCaptures;
	bool bConnectivityEndpointsComplete = true;

	{
		TArray<AFGBuildable*> Manufacturers;
		BuildableSubsystem->GetTypedBuildable(AFGBuildableManufacturer::StaticClass(), Manufacturers);
		OutSnapshot.Truncation.BuildingsAvailable = Manufacturers.Num();

		int32 Scanned = 0;
		for (AFGBuildable* Buildable : Manufacturers)
		{
			if (Scanned >= Params.MaxBuildingsToScan)
			{
				OutSnapshot.Truncation.bAnyCapHit = true;
				bConnectivityEndpointsComplete = false;
				break;
			}

			AFGBuildableManufacturer* Manufacturer = Cast<AFGBuildableManufacturer>(Buildable);
			if (!IsValid(Manufacturer))
			{
				continue;
			}
			TSubclassOf<UFGRecipe> Recipe = Manufacturer->GetCurrentRecipe();
			if (!Recipe)
			{
				continue;
			}
			const float DurationSeconds = UFGRecipe::GetManufacturingDuration(Recipe);
			if (DurationSeconds <= 0.0f)
			{
				continue;
			}

			Scanned++;
			const FMachineState State = ReadMachineState(Manufacturer);

			// Somersloop boost multiplies OUTPUT only; inputs draw at clock rate.
			const float InputMultiplier = Manufacturer->GetCurrentPotential() * Manufacturer->GetManufacturingSpeed();
			const float OutputMultiplier = InputMultiplier * Manufacturer->GetCurrentProductionBoost();
			const float CraftsPerMinuteIn = 60.0f * InputMultiplier / DurationSeconds;
			const float CraftsPerMinuteOut = 60.0f * OutputMultiplier / DurationSeconds;

			const FString MachineName = Manufacturer->mDisplayName.ToString();
			const FString RecipeName = UFGRecipe::GetRecipeName(Recipe).ToString();
			FConsumerCapture ConsumerCapture;
			ConsumerCapture.Consumer = Manufacturer;
			ConsumerCapture.bProducing = State.bProducing;

			FCPRecipeEdge& Edge = EdgeMap.FindOrAdd(RecipeName);
			const bool bNewEdge = Edge.RecipeName.IsEmpty();
			if (bNewEdge)
			{
				Edge.RecipeName = RecipeName;
				Edge.MachineName = MachineName;
			}
			Edge.MachineCount++;

			// ALL products — byproducts included (a machine stalls when ANY output backs up).
			for (const FItemAmount& ProductAmount : UFGRecipe::GetProducts(Recipe))
			{
				if (!ProductAmount.ItemClass || ProductAmount.Amount <= 0)
				{
					continue;
				}
				const float DisplayAmount = ToDisplay(ProductAmount.ItemClass, static_cast<float>(ProductAmount.Amount));
				const FString ItemName = UFGItemDescriptor::GetItemName(ProductAmount.ItemClass).ToString();

				FProductAggregate& Aggregate = ProductAggregates.FindOrAdd(ItemName);
				if (Aggregate.Row.ConfiguredBuildings == 0)
				{
					Aggregate.Row.Item = MakeItemRef(ProductAmount.ItemClass);
					Aggregate.Row.MachineName = MachineName;
				}
				Aggregate.Row.ConfiguredCapacityPerMinute += DisplayAmount * CraftsPerMinuteOut;
				Aggregate.ProductivityTotal += State.ProductivityPercent;
				AccumulateState(Aggregate.Row, State);

				FProducerCapture ProducerCapture;
				ProducerCapture.Producer = Manufacturer;
				ProducerCapture.ItemClass = ProductAmount.ItemClass;
				ProducerCapture.ItemName = ItemName;
				ProducerCapture.CapacityPerMinute = DisplayAmount * CraftsPerMinuteOut;
				ProducerCapture.OutputInventory = Manufacturer->GetOutputInventory();
				ProducerCaptures.Add(MoveTemp(ProducerCapture));

				if (bNewEdge)
				{
					FCPItemRate Rate;
					Rate.Item = MakeItemRef(ProductAmount.ItemClass);
					Rate.AmountPerCraft = DisplayAmount;
					Edge.Products.Add(MoveTemp(Rate));
				}
			}
			// Edge line rates aggregate across all machines on the recipe.
			for (FCPItemRate& Rate : Edge.Products)
			{
				Rate.RatePerMinute += Rate.AmountPerCraft * CraftsPerMinuteOut;
			}

			for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(Manufacturer, Recipe))
			{
				if (!Ingredient.ItemClass || Ingredient.Amount <= 0)
				{
					continue;
				}
				const float DisplayAmount = ToDisplay(Ingredient.ItemClass, static_cast<float>(Ingredient.Amount));
				const FString ItemName = UFGItemDescriptor::GetItemName(Ingredient.ItemClass).ToString();

				FCPDemandRow& Demand = DemandAggregates.FindOrAdd(ItemName);
				if (Demand.ConsumingBuildings == 0)
				{
					Demand.Item = MakeItemRef(Ingredient.ItemClass);
				}
				IngredientClasses.FindOrAdd(ItemName, Ingredient.ItemClass);
				FConsumerCapture::FIngredient CapturedIngredient;
				CapturedIngredient.ItemClass = Ingredient.ItemClass;
				CapturedIngredient.Item = MakeItemRef(Ingredient.ItemClass);
				CapturedIngredient.DemandPerMinute = DisplayAmount * CraftsPerMinuteIn;
				ConsumerCapture.Ingredients.Add(MoveTemp(CapturedIngredient));
				Demand.ConfiguredDemandPerMinute += DisplayAmount * CraftsPerMinuteIn;
				AccumulateDemandState(Demand, State);

				if (bNewEdge)
				{
					FCPItemRate Rate;
					Rate.Item = MakeItemRef(Ingredient.ItemClass);
					Rate.AmountPerCraft = DisplayAmount;
					Edge.Ingredients.Add(MoveTemp(Rate));
				}
			}
			for (FCPItemRate& Rate : Edge.Ingredients)
			{
				Rate.RatePerMinute += Rate.AmountPerCraft * CraftsPerMinuteIn;
			}
			ConsumerCaptures.Add(MoveTemp(ConsumerCapture));
		}
		OutSnapshot.Truncation.BuildingsScanned = Scanned;
	}

	// ---- extractors: raw-resource supply -----------------------------------------------------
	// NOTE (AGENTS gotcha): resource wells (pressurizer + satellites) are a distinct family —
	// verify coverage against a save that has one before trusting well resources here.
	{
		TArray<AFGBuildable*> Extractors;
		BuildableSubsystem->GetTypedBuildable(AFGBuildableResourceExtractor::StaticClass(), Extractors);
		int32 Scanned = 0;
		for (AFGBuildable* Buildable : Extractors)
		{
			if (Scanned >= Params.MaxBuildingsToScan)
			{
				OutSnapshot.Truncation.bAnyCapHit = true;
				bConnectivityEndpointsComplete = false;
				break;
			}
			AFGBuildableResourceExtractor* Extractor = Cast<AFGBuildableResourceExtractor>(Buildable);
			if (!IsValid(Extractor))
			{
				continue;
			}
			IFGExtractableResourceInterface* Resource = Extractor->GetExtractableResource().GetInterface();
			const TSubclassOf<UFGResourceDescriptor> ResourceClass = Resource ? Resource->GetResourceClass() : nullptr;
			if (!ResourceClass)
			{
				continue;
			}
			Scanned++;

			const FString ItemName = UFGItemDescriptor::GetItemName(ResourceClass).ToString();
			const FMachineState State = ReadMachineState(Extractor);

			FProductAggregate& Aggregate = ProductAggregates.FindOrAdd(ItemName);
			if (Aggregate.Row.ConfiguredBuildings == 0)
			{
				Aggregate.Row.Item = MakeItemRef(ResourceClass);
				Aggregate.Row.MachineName = Extractor->mDisplayName.ToString();
			}
			Aggregate.Row.bIsExtraction = true;
			// GetExtractionPerMinute is already display units for fluids (m3/min), unlike
			// recipe FItemAmount (liters) -- see the same rule in CPFlowCapture.
			Aggregate.Row.ConfiguredCapacityPerMinute += Extractor->GetExtractionPerMinute();
			Aggregate.ProductivityTotal += State.ProductivityPercent;
			AccumulateState(Aggregate.Row, State);

			FProducerCapture ProducerCapture;
			ProducerCapture.Producer = Extractor;
			ProducerCapture.ItemClass = ResourceClass;
			ProducerCapture.ItemName = ItemName;
			ProducerCapture.CapacityPerMinute = Extractor->GetExtractionPerMinute();
			ProducerCapture.OutputInventory = Extractor->GetOutputInventory();
			ProducerCaptures.Add(MoveTemp(ProducerCapture));
		}
	}

	// ---- belt/pipe connectivity + item flow networks -----------------------------------------
	// Build the directed reverse graph and weak transport components in one scan. Reverse
	// reachability proves that supply can flow to a consumer; weak components provide the island
	// boundary inside which supply and configured demand may honestly be compared.
	{
		const TArray<AFGBuildable*>& AllBuildables = BuildableSubsystem->GetAllBuildablesRef();
		OutSnapshot.Truncation.ConnectivityBuildingsAvailable = AllBuildables.Num();

		TMap<const AActor*, TSet<const AActor*>> ReverseEdges;
		TMap<const AActor*, TSet<const AActor*>> UndirectedEdges;
		TSet<const AActor*> ScannedTransportActors;
		int32 Scanned = 0;
		for (AFGBuildable* Buildable : AllBuildables)
		{
			if (Scanned >= Params.MaxConnectivityBuildingsToScan)
			{
				OutSnapshot.Truncation.bConnectivityCapHit = true;
				OutSnapshot.Truncation.bAnyCapHit = true;
				break;
			}
			Scanned++;
			if (!IsValid(Buildable) || IsConnectivityEndpoint(Buildable))
			{
				continue;
			}
			ScannedTransportActors.Add(Buildable);

			TInlineComponentArray<UFGFactoryConnectionComponent*> FactoryConnections;
			Buildable->GetComponents(FactoryConnections);
			for (UFGFactoryConnectionComponent* Connection : FactoryConnections)
			{
				if (!Connection || !Connection->IsConnected() || !IsFactoryOutput(Connection->GetDirection()))
				{
					continue;
				}
				UFGFactoryConnectionComponent* Peer = Connection->GetConnection();
				const AActor* PeerActor = Peer ? Peer->GetOwner() : nullptr;
				if (PeerActor && !IsConnectivityEndpoint(PeerActor))
				{
					ReverseEdges.FindOrAdd(PeerActor).Add(Buildable);
					UndirectedEdges.FindOrAdd(PeerActor).Add(Buildable);
					UndirectedEdges.FindOrAdd(Buildable).Add(PeerActor);
				}
			}

			TInlineComponentArray<UFGPipeConnectionComponentBase*> PipeConnections;
			Buildable->GetComponents(PipeConnections);
			for (UFGPipeConnectionComponentBase* Connection : PipeConnections)
			{
				if (!Connection || !Connection->IsConnected() || !IsPipeOutput(Connection->GetPipeConnectionType()))
				{
					continue;
				}
				UFGPipeConnectionComponentBase* Peer = Connection->GetConnection();
				const AActor* PeerActor = Peer ? Peer->GetOwner() : nullptr;
				if (PeerActor && !IsConnectivityEndpoint(PeerActor))
				{
					ReverseEdges.FindOrAdd(PeerActor).Add(Buildable);
					UndirectedEdges.FindOrAdd(PeerActor).Add(Buildable);
					UndirectedEdges.FindOrAdd(Buildable).Add(PeerActor);
				}
			}
		}
		OutSnapshot.Truncation.ConnectivityBuildingsScanned = Scanned;

		TMap<const AActor*, int32> ComponentIds;
		int32 NextNetworkId = 0;
		for (const AActor* Start : ScannedTransportActors)
		{
			if (ComponentIds.Contains(Start))
			{
				continue;
			}
			const int32 ComponentId = NextNetworkId++;
			TArray<const AActor*> ComponentPending { Start };
			ComponentIds.Add(Start, ComponentId);
			for (int32 Index = 0; Index < ComponentPending.Num(); ++Index)
			{
				if (const TSet<const AActor*>* Neighbors = UndirectedEdges.Find(ComponentPending[Index]))
				{
					for (const AActor* Neighbor : *Neighbors)
					{
						if (ScannedTransportActors.Contains(Neighbor) && !ComponentIds.Contains(Neighbor))
						{
							ComponentIds.Add(Neighbor, ComponentId);
							ComponentPending.Add(Neighbor);
						}
					}
				}
			}
		}

		const auto MakeNetworkKey = [](const FString& ItemName, int32 NetworkId)
		{
			return FString::Printf(TEXT("%s\x1f%d"), *ItemName, NetworkId);
		};
		const auto MakeDirectKey = [](const AActor* Producer, const AActor* Consumer, const FString& ItemName)
		{
			return FString::Printf(TEXT("%llu|%llu|%s"),
				static_cast<uint64>(reinterpret_cast<UPTRINT>(Producer)),
				static_cast<uint64>(reinterpret_cast<UPTRINT>(Consumer)), *ItemName);
		};
		TMap<FString, FCPFlowNetwork> FlowAggregates;
		TMap<FString, int32> DirectNetworkIds;

		struct FReachabilityState
		{
			const AActor* Actor = nullptr;
			FString NetworkKey;
		};
		TMap<const AActor*, TSet<FString>> ReachableNetworks;
		TArray<FReachabilityState> Pending;
		const auto Seed = [&ReachableNetworks, &Pending, &ScannedTransportActors](const AActor* Actor, const FString& NetworkKey)
		{
			if (!Actor || !ScannedTransportActors.Contains(Actor))
			{
				return;
			}
			TSet<FString>& Networks = ReachableNetworks.FindOrAdd(Actor);
			if (!Networks.Contains(NetworkKey))
			{
				Networks.Add(NetworkKey);
				Pending.Add({ Actor, NetworkKey });
			}
		};

		for (const FConsumerCapture& Capture : ConsumerCaptures)
		{
			if (!IsValid(Capture.Consumer))
			{
				continue;
			}
			TSet<FString> AddedConsumerNetworks;
			const auto AddConsumerNetwork = [&](const FConsumerCapture::FIngredient& Ingredient, const AActor* PeerActor)
			{
				if (!PeerActor)
				{
					return;
				}
				int32 NetworkId = INDEX_NONE;
				if (const int32* ComponentId = ComponentIds.Find(PeerActor))
				{
					NetworkId = *ComponentId;
				}
				else if (IsConnectivityEndpoint(PeerActor))
				{
					const FString DirectKey = MakeDirectKey(PeerActor, Capture.Consumer, Ingredient.Item.Name);
					int32& DirectId = DirectNetworkIds.FindOrAdd(DirectKey);
					if (DirectId == 0)
					{
						DirectId = ++NextNetworkId; // zero is reserved as the uninitialized map value
					}
					NetworkId = DirectId;
				}
				if (NetworkId == INDEX_NONE)
				{
					return;
				}
				const FString NetworkKey = MakeNetworkKey(Ingredient.Item.Name, NetworkId);
				FCPFlowNetwork& Network = FlowAggregates.FindOrAdd(NetworkKey);
				if (Network.Item.Name.IsEmpty())
				{
					Network.Item = Ingredient.Item;
					Network.NetworkId = NetworkId;
				}
				if (!AddedConsumerNetworks.Contains(NetworkKey))
				{
					AddedConsumerNetworks.Add(NetworkKey);
					Network.ConfiguredDemandPerMinute += Ingredient.DemandPerMinute;
					Network.ConsumerBuildings++;
					Network.InactiveConsumerBuildings += Capture.bProducing ? 0 : 1;
				}
				Seed(PeerActor, NetworkKey);
			};

			TInlineComponentArray<UFGFactoryConnectionComponent*> FactoryInputs;
			Capture.Consumer->GetComponents(FactoryInputs);
			for (UFGFactoryConnectionComponent* Connection : FactoryInputs)
			{
				if (!Connection || !Connection->IsConnected() || !IsFactoryInput(Connection->GetDirection()))
				{
					continue;
				}
				for (const FConsumerCapture::FIngredient& Ingredient : Capture.Ingredients)
				{
					if (FactoryPortCarries(Connection, Ingredient.ItemClass))
					{
						UFGFactoryConnectionComponent* Peer = Connection->GetConnection();
						AddConsumerNetwork(Ingredient, Peer ? Peer->GetOwner() : nullptr);
					}
				}
			}

			TInlineComponentArray<UFGPipeConnectionComponentBase*> PipeInputs;
			Capture.Consumer->GetComponents(PipeInputs);
			for (UFGPipeConnectionComponentBase* Connection : PipeInputs)
			{
				if (!Connection || !Connection->IsConnected() || !IsPipeInput(Connection->GetPipeConnectionType()))
				{
					continue;
				}
				for (const FConsumerCapture::FIngredient& Ingredient : Capture.Ingredients)
				{
					if (PipePortCarries(Connection, Capture.Consumer->GetInputInventory(), Ingredient.ItemClass))
					{
						UFGPipeConnectionComponentBase* Peer = Connection->GetConnection();
						AddConsumerNetwork(Ingredient, Peer ? Peer->GetOwner() : nullptr);
					}
				}
			}
		}

		int32 PendingIndex = 0;
		while (PendingIndex < Pending.Num())
		{
			if (OutSnapshot.Truncation.ConnectivityStatesPropagated >= Params.MaxConnectivityStatesToPropagate)
			{
				OutSnapshot.Truncation.bConnectivityCapHit = true;
				OutSnapshot.Truncation.bAnyCapHit = true;
				break;
			}
			const FReachabilityState State = Pending[PendingIndex++];
			OutSnapshot.Truncation.ConnectivityStatesPropagated++;
			if (const TSet<const AActor*>* UpstreamActors = ReverseEdges.Find(State.Actor))
			{
				for (const AActor* Upstream : *UpstreamActors)
				{
					Seed(Upstream, State.NetworkKey);
				}
			}
		}

		for (const FProducerCapture& Capture : ProducerCaptures)
		{
			if (!IsValid(Capture.Producer))
			{
				continue;
			}
			TSet<FString> MatchedNetworks;
			const auto MatchPeer = [&](const AActor* PeerActor)
			{
				if (!PeerActor)
				{
					return;
				}
				if (const TSet<FString>* Networks = ReachableNetworks.Find(PeerActor))
				{
					for (const FString& NetworkKey : *Networks)
					{
						const FCPFlowNetwork* Network = FlowAggregates.Find(NetworkKey);
						if (Network && Network->Item.Name.Equals(Capture.ItemName, ESearchCase::IgnoreCase))
						{
							MatchedNetworks.Add(NetworkKey);
						}
					}
				}
				const FString DirectKey = MakeDirectKey(Capture.Producer, PeerActor, Capture.ItemName);
				if (const int32* DirectId = DirectNetworkIds.Find(DirectKey))
				{
					MatchedNetworks.Add(MakeNetworkKey(Capture.ItemName, *DirectId));
				}
			};
			TInlineComponentArray<UFGFactoryConnectionComponent*> FactoryOutputs;
			Capture.Producer->GetComponents(FactoryOutputs);
			for (UFGFactoryConnectionComponent* Connection : FactoryOutputs)
			{
				if (!Connection || !Connection->IsConnected() || !IsFactoryOutput(Connection->GetDirection()) ||
					!FactoryPortCarries(Connection, Capture.ItemClass))
				{
					continue;
				}
				UFGFactoryConnectionComponent* Peer = Connection->GetConnection();
				MatchPeer(Peer ? Peer->GetOwner() : nullptr);
			}

			TInlineComponentArray<UFGPipeConnectionComponentBase*> PipeOutputs;
			Capture.Producer->GetComponents(PipeOutputs);
			for (UFGPipeConnectionComponentBase* Connection : PipeOutputs)
			{
				if (!Connection || !Connection->IsConnected() || !IsPipeOutput(Connection->GetPipeConnectionType()) ||
					!PipePortCarries(Connection, Capture.OutputInventory, Capture.ItemClass))
				{
					continue;
				}
				UFGPipeConnectionComponentBase* Peer = Connection->GetConnection();
				MatchPeer(Peer ? Peer->GetOwner() : nullptr);
			}

			if (MatchedNetworks.Num() > 0)
			{
				if (FProductAggregate* Aggregate = ProductAggregates.Find(Capture.ItemName))
				{
					Aggregate->Row.ConnectedCapacityPerMinute += Capture.CapacityPerMinute;
					Aggregate->Row.ConnectedBuildings++;
				}
				for (const FString& NetworkKey : MatchedNetworks)
				{
					if (FCPFlowNetwork* Network = FlowAggregates.Find(NetworkKey))
					{
						Network->ConnectedSupplyPerMinute += Capture.CapacityPerMinute;
						Network->ProducerBuildings++;
					}
				}
			}
		}

		if (!bConnectivityEndpointsComplete)
		{
			OutSnapshot.Truncation.bConnectivityCapHit = true;
		}
		const bool bComplete = !OutSnapshot.Truncation.bConnectivityCapHit;
		for (auto& Entry : ProductAggregates)
		{
			Entry.Value.Row.bConnectivityComplete = bComplete;
		}
		for (auto& Entry : FlowAggregates)
		{
			Entry.Value.bConnectivityComplete = bComplete;
			OutSnapshot.FlowNetworks.Add(MoveTemp(Entry.Value));
		}
		OutSnapshot.FlowNetworks.Sort([](const FCPFlowNetwork& A, const FCPFlowNetwork& B)
		{
			const int32 NameOrder = A.Item.Name.Compare(B.Item.Name, ESearchCase::IgnoreCase);
			return NameOrder == 0 ? A.NetworkId < B.NetworkId : NameOrder < 0;
		});
	}

	// ---- finalize product/demand rows --------------------------------------------------------
	for (auto& Entry : ProductAggregates)
	{
		FCPProductRow Row = Entry.Value.Row;
		if (Row.ConfiguredBuildings > 0)
		{
			Row.AverageProductivityPercent = Entry.Value.ProductivityTotal / static_cast<float>(Row.ConfiguredBuildings);
		}
		OutSnapshot.Products.Add(MoveTemp(Row));
	}
	for (auto& Entry : DemandAggregates)
	{
		OutSnapshot.Demands.Add(MoveTemp(Entry.Value));
	}
	EdgeMap.ValueSort([](const FCPRecipeEdge& A, const FCPRecipeEdge& B) { return A.MachineCount > B.MachineCount; });
	for (auto& Entry : EdgeMap)
	{
		if (OutSnapshot.Edges.Num() >= Params.MaxRecipeEdges)
		{
			OutSnapshot.Truncation.bAnyCapHit = true;
			break;
		}
		OutSnapshot.Edges.Add(MoveTemp(Entry.Value));
	}

	// ---- owned totals: storage / depot / pockets ---------------------------------------------
	TMap<FString, FCPOwnedRow> OwnedMap;
	const auto AddOwned = [&OwnedMap](TSubclassOf<UFGItemDescriptor> ItemClass, int64 Count, int64 FCPOwnedRow::* Bucket)
	{
		if (!ItemClass || Count <= 0)
		{
			return;
		}
		const FString Name = UFGItemDescriptor::GetItemName(ItemClass).ToString();
		FCPOwnedRow& Row = OwnedMap.FindOrAdd(Name);
		if (!Row.Item.IsValid())
		{
			Row.Item = MakeItemRef(ItemClass);
		}
		Row.*Bucket += Count;
	};

	{
		TArray<AFGBuildable*> Storages;
		BuildableSubsystem->GetTypedBuildable(AFGBuildableStorage::StaticClass(), Storages);
		int32 Scanned = 0;
		for (AFGBuildable* Buildable : Storages)
		{
			if (Scanned >= Params.MaxStorageContainersToScan)
			{
				OutSnapshot.Truncation.bAnyCapHit = true;
				break;
			}
			AFGBuildableStorage* Storage = Cast<AFGBuildableStorage>(Buildable);
			// Central storage containers upload into the subsystem; counting local buffers too
			// would double-count.
			if (!IsValid(Storage) || Storage->IsA(AFGCentralStorageContainer::StaticClass()))
			{
				continue;
			}
			UFGInventoryComponent* Inventory = Storage->GetStorageInventory();
			if (!Inventory)
			{
				continue;
			}
			Scanned++;
			TArray<FInventoryStack> Stacks;
			Inventory->GetInventoryStacks(Stacks);
			for (const FInventoryStack& Stack : Stacks)
			{
				if (Stack.HasItems())
				{
					AddOwned(Stack.Item.GetItemClass(), Stack.NumItems, &FCPOwnedRow::InStorage);
				}
			}
		}
		OutSnapshot.Truncation.StorageContainersScanned = Scanned;
	}

	// Depot: server-side shared subsystem — available in every game mode. (Player-retrievable
	// only; consumers must never treat it as machine-reachable supply.)
	if (AFGCentralStorageSubsystem* CentralStorage = AFGCentralStorageSubsystem::Get(World))
	{
		TArray<FItemAmount> DepotItems;
		CentralStorage->GetAllItemsFromCentralStorage(DepotItems);
		for (const FItemAmount& Item : DepotItems)
		{
			AddOwned(Item.ItemClass, Item.Amount, &FCPOwnedRow::InDepot);
		}
	}

	// Pockets: only with a player context; absent otherwise (never zero).
	if (Params.PlayerContext)
	{
		if (AFGCharacterPlayer* Player = Cast<AFGCharacterPlayer>(Params.PlayerContext->GetControlledCharacter()))
		{
			if (UFGInventoryComponent* Inventory = Player->GetInventory())
			{
				OutSnapshot.bHasPlayerContext = true;
				TArray<FInventoryStack> Stacks;
				Inventory->GetInventoryStacks(Stacks);
				for (const FInventoryStack& Stack : Stacks)
				{
					if (Stack.HasItems())
					{
						AddOwned(Stack.Item.GetItemClass(), Stack.NumItems, &FCPOwnedRow::InPockets);
					}
				}
			}
		}
	}

	for (auto& Entry : OwnedMap)
	{
		OutSnapshot.Owned.Add(MoveTemp(Entry.Value));
	}

	// ---- objectives + research gaps ----------------------------------------------------------
	struct FSeedItem
	{
		TSubclassOf<UFGItemDescriptor> ItemClass;
		FString ObjectiveName;
	};
	TArray<FSeedItem> Seeds;

	AFGSchematicManager* SchematicManager = AFGSchematicManager::Get(World);
	AFGGamePhaseManager* PhaseManager = AFGGamePhaseManager::Get(World);

	int32 HighestAvailableTier = SchematicManager ? SchematicManager->GetMaxAllowedTechTier() : 0;
	if (PhaseManager)
	{
		// The phase asset's own tier ceiling is authoritative: BOTH schematic-manager tier
		// getters ignore the Space Elevator gate in practice (prototype ground truth).
		if (const UFGGamePhase* CurrentPhase = PhaseManager->GetCurrentGamePhase())
		{
			HighestAvailableTier = FMath::Min(HighestAvailableTier, CurrentPhase->mLastTierOfPhase);
		}

		if (UFGGamePhase* TargetPhase = PhaseManager->GetTargetGamePhase())
		{
			FCPObjective Objective;
			Objective.Name = TargetPhase->mDisplayName.ToString();
			Objective.Kind = ECPObjectiveKind::SpaceElevatorPhase;

			TArray<FRemainingPhaseCost> RemainingCosts;
			PhaseManager->GetRemainingPhaseCosts(RemainingCosts);
			for (const FRemainingPhaseCost& Cost : RemainingCosts)
			{
				if (!Cost.ItemClass || Objective.Items.Num() >= Params.MaxObjectiveItems)
				{
					continue;
				}
				FCPObjectiveItem Item;
				Item.Item = MakeItemRef(Cost.ItemClass);
				Item.Required = Cost.TotalCost;
				Item.Remaining = Cost.RemainingCost;
				Item.Delivered = Cost.TotalCost - Cost.RemainingCost;
				if (Item.Remaining > 0)
				{
					Seeds.Add({ Cost.ItemClass, Objective.Name });
				}
				Objective.Items.Add(MoveTemp(Item));
			}
			if (Objective.Items.Num() > 0)
			{
				OutSnapshot.Objectives.Add(MoveTemp(Objective));
			}
		}
	}

	if (SchematicManager)
	{
		if (TSubclassOf<UFGSchematic> ActiveMilestone = SchematicManager->GetActiveSchematic())
		{
			OutSnapshot.bMilestoneSelected = true;
			FCPObjective Objective;
			Objective.Name = UFGSchematic::GetSchematicDisplayName(ActiveMilestone).ToString();
			Objective.Kind = ECPObjectiveKind::Milestone;

			const TArray<FItemAmount> BaseCost = UFGSchematic::GetCost(ActiveMilestone);
			const TArray<FItemAmount> RemainingCost = SchematicManager->GetRemainingCostFor(ActiveMilestone);
			for (const FItemAmount& Cost : BaseCost)
			{
				if (!Cost.ItemClass || Objective.Items.Num() >= Params.MaxObjectiveItems)
				{
					continue;
				}
				const int32 Remaining = FItemAmount::GetAmountFromItemAmounts(RemainingCost, Cost.ItemClass);
				FCPObjectiveItem Item;
				Item.Item = MakeItemRef(Cost.ItemClass);
				Item.Required = Cost.Amount;
				Item.Remaining = Remaining;
				Item.Delivered = Cost.Amount - Remaining;
				if (Item.Remaining > 0)
				{
					Seeds.Add({ Cost.ItemClass, Objective.Name });
				}
				Objective.Items.Add(MoveTemp(Item));
			}
			if (Objective.Items.Num() > 0)
			{
				OutSnapshot.Objectives.Add(MoveTemp(Objective));
			}
		}

		// Phase-locked milestone count (context for consumers; the list itself is not M0 data).
		TArray<ESchematicType> MilestoneTypes;
		MilestoneTypes.Add(ESchematicType::EST_Milestone);
		MilestoneTypes.Add(ESchematicType::EST_Tutorial);
		TArray<TSubclassOf<UFGSchematic>> Available;
		SchematicManager->GetAvailableNonPurchasedSchematicsOfTypes(MilestoneTypes, Available);
		for (const TSubclassOf<UFGSchematic>& Schematic : Available)
		{
			if (UFGSchematic::GetTechTier(Schematic) > HighestAvailableTier)
			{
				OutSnapshot.PhaseLockedMilestoneCount++;
			}
			else
			{
				OutSnapshot.SelectableMilestoneCount++;
			}
		}
	}
	OutSnapshot.HighestUnlockedTier = HighestAvailableTier;

	// Research gaps: walk objective items through AVAILABLE recipes; an item with no available
	// recipe (and not a raw resource) is a knowledge gap, paired with purchasable research.
	if (Seeds.Num() > 0 && SchematicManager)
	{
		if (AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World))
		{
			TArray<ESchematicType> ResearchTypes;
			ResearchTypes.Add(ESchematicType::EST_Milestone);
			ResearchTypes.Add(ESchematicType::EST_Tutorial);
			ResearchTypes.Add(ESchematicType::EST_MAM);
			ResearchTypes.Add(ESchematicType::EST_ResourceSink); // AWESOME Shop
			ResearchTypes.Add(ESchematicType::EST_Alternate);    // hard-drive alternates
			TArray<TSubclassOf<UFGSchematic>> Candidates;
			SchematicManager->GetAvailableNonPurchasedSchematicsOfTypes(ResearchTypes, Candidates);

			const auto SourceOf = [](TSubclassOf<UFGSchematic> Schematic)
			{
				switch (UFGSchematic::GetType(Schematic))
				{
				case ESchematicType::EST_Milestone:    return ECPResearchSource::Milestone;
				case ESchematicType::EST_MAM:          return ECPResearchSource::MAM;
				case ESchematicType::EST_ResourceSink: return ECPResearchSource::Shop;
				case ESchematicType::EST_Alternate:    return ECPResearchSource::HardDrive;
				case ESchematicType::EST_Tutorial:     return ECPResearchSource::Tutorial;
				default:                               return ECPResearchSource::Unknown;
				}
			};

			const auto FindUnlock = [&Candidates, &SourceOf, HighestAvailableTier](TSubclassOf<UFGItemDescriptor> Item, FString& OutName, int32& OutTier, ECPResearchSource& OutSource, TSubclassOf<UFGSchematic>& OutSchematic)
			{
				for (const TSubclassOf<UFGSchematic>& Candidate : Candidates)
				{
					if (UFGSchematic::GetTechTier(Candidate) > HighestAvailableTier)
					{
						continue;
					}
					for (UFGUnlock* Unlock : UFGSchematic::GetUnlocks(Candidate))
					{
						const UFGUnlockRecipe* RecipeUnlock = Cast<UFGUnlockRecipe>(Unlock);
						if (!RecipeUnlock)
						{
							continue;
						}
						for (const TSubclassOf<UFGRecipe>& UnlockedRecipe : RecipeUnlock->GetRecipesToUnlock())
						{
							for (const FItemAmount& ProductAmount : UFGRecipe::GetProducts(UnlockedRecipe))
							{
								if (ProductAmount.ItemClass == Item)
								{
									OutName = UFGSchematic::GetSchematicDisplayName(Candidate).ToString();
									OutTier = UFGSchematic::GetTechTier(Candidate);
									OutSource = SourceOf(Candidate);
									OutSchematic = Candidate;
									return true;
								}
							}
						}
					}
				}
				return false;
			};

			struct FWalkEntry
			{
				TSubclassOf<UFGItemDescriptor> Item;
				FString ObjectiveName;
				FString Parent;
				int32 Depth = 0;
			};
			TArray<FWalkEntry> Queue;
			TSet<UClass*> Visited;
			for (const FSeedItem& Seed : Seeds)
			{
				Queue.Add({ Seed.ItemClass, Seed.ObjectiveName, FString(), 0 });
			}

			for (int32 Index = 0; Index < Queue.Num(); ++Index)
			{
				if (Visited.Num() >= Params.ResearchWalkMaxItems || OutSnapshot.ResearchGaps.Num() >= Params.MaxResearchGaps)
				{
					break;
				}
				const FWalkEntry Entry = Queue[Index];
				if (!Entry.Item || Visited.Contains(*Entry.Item))
				{
					continue;
				}
				Visited.Add(*Entry.Item);
				if (Entry.Item->IsChildOf(UFGResourceDescriptor::StaticClass()))
				{
					continue;
				}

				bool bUnusedAlternates = false;
				const TSubclassOf<UFGRecipe> DefaultRecipe = PickDefaultRecipeRanked(RecipeManager, Entry.Item, bUnusedAlternates);
				if (DefaultRecipe)
				{
					if (Entry.Depth < Params.ResearchWalkMaxDepth)
					{
						for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(World, DefaultRecipe))
						{
							if (Ingredient.ItemClass)
							{
								Queue.Add({ Ingredient.ItemClass, Entry.ObjectiveName, UFGItemDescriptor::GetItemName(Entry.Item).ToString(), Entry.Depth + 1 });
							}
						}
					}
					continue;
				}

				// No unlocked recipe — this is a gap. Keep walking through the LOCKED recipe all
				// the same: one research unlock routinely gates a part AND an ingredient that
				// part consumes, and that second line is work the plan must show up front rather
				// than spring on the player after the unlock.
				if (Entry.Depth < Params.ResearchWalkMaxDepth)
				{
					bool bLockedAlternates = false;
					bool bIsLocked = false;
					const TSubclassOf<UFGRecipe> LockedRecipe =
						PickRecipeAllowingLocked(RecipeManager, Entry.Item, bLockedAlternates, bIsLocked);
					if (LockedRecipe && bIsLocked)
					{
						for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(World, LockedRecipe))
						{
							if (Ingredient.ItemClass)
							{
								Queue.Add({ Ingredient.ItemClass, Entry.ObjectiveName,
									UFGItemDescriptor::GetItemName(Entry.Item).ToString(), Entry.Depth + 1 });
							}
						}
					}
				}

				FCPResearchGap Gap;
				Gap.NeededItem = MakeItemRef(Entry.Item);
				Gap.ForObjective = Entry.ObjectiveName;
				Gap.ViaItem = Entry.Parent;
				// Spent nuclear fuel has NO recipe by design — it is obtained by burning its
				// fuel in a Nuclear Power Plant, and research will never change that. Say so
				// instead of "no purchasable research yet" (live-caught on Plutonium Waste).
				{
					TArray<UClass*> FuelClasses;
					GetDerivedClasses(UFGItemDescriptorNuclearFuel::StaticClass(), FuelClasses, true);
					for (UClass* FuelClass : FuelClasses)
					{
						if (UFGItemDescriptorNuclearFuel::GetSpentFuelClass(FuelClass) == Entry.Item)
						{
							Gap.ByproductOfFuel = UFGItemDescriptor::GetItemName(FuelClass).ToString();
							break;
						}
					}
				}
				TSubclassOf<UFGSchematic> UnlockSchematic;
				if (Gap.ByproductOfFuel.IsEmpty() &&
					FindUnlock(Entry.Item, Gap.UnlockSchematicName, Gap.UnlockSchematicTier, Gap.UnlockSource, UnlockSchematic))
				{
					// M2.7b: capture the schematic's full item costs so analysis can plan the
					// payment like an objective. ONE schematic level — prerequisites of the
					// schematic itself are not followed. Payment progress only exists while
					// selected, so Remaining == Required here; when it IS the selected
					// milestone, the live milestone objective already carries the real ledger.
					Gap.bUnlockIsSelectedMilestone = SchematicManager &&
						SchematicManager->GetActiveSchematic() == UnlockSchematic;
					if (!Gap.bUnlockIsSelectedMilestone)
					{
						for (const FItemAmount& Cost : UFGSchematic::GetCost(UnlockSchematic))
						{
							if (!Cost.ItemClass)
							{
								continue;
							}
							FCPObjectiveItem CostItem;
							CostItem.Item = MakeItemRef(Cost.ItemClass);
							CostItem.Required = Cost.Amount;
							CostItem.Remaining = Cost.Amount;
							Gap.UnlockCosts.Add(MoveTemp(CostItem));
						}
					}
				}
				OutSnapshot.ResearchGaps.Add(MoveTemp(Gap));
			}
		}
	}

	// ---- catalog slice for the prospective chain (M2) ------------------------------------------
	// Capture the DEFAULT unlocked recipe for every no-producer frontier item (objective items
	// and configured-edge ingredients with no product row), walking down through ingredients
	// until each branch reaches existing production or a raw resource. Analysis expands this
	// plain-data slice into the planned subtree — it never touches the catalog itself.
	if (AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(World))
	{
		const auto MachineDisplayName = [](TSubclassOf<UFGRecipe> Recipe) -> FString
		{
			for (const TSubclassOf<UObject>& Producer : UFGRecipe::GetProducedIn(Recipe))
			{
				if (Producer && Producer->IsChildOf(AFGBuildableManufacturer::StaticClass()))
				{
					if (const AFGBuildable* CDO = Cast<AFGBuildable>(Producer->GetDefaultObject()))
					{
						return CDO->mDisplayName.ToString();
					}
				}
			}
			return FString();
		};

		// Default-recipe policy: unlocked only; prefer the non-alternate. Alternates present =
		// a choice exists — flagged, never silently chosen.
		const auto PickDefaultRecipe = [RecipeManager](TSubclassOf<UFGItemDescriptor> Item, bool& bOutAlternates) -> TSubclassOf<UFGRecipe>
		{
			return PickDefaultRecipeRanked(RecipeManager, Item, bOutAlternates);
		};

		struct FCatalogSeed
		{
			TSubclassOf<UFGItemDescriptor> ItemClass;
			int32 Depth = 0;
		};
		TArray<FCatalogSeed> Queue;
		TSet<FString> Visited;
		TSet<FString> RawNames;

		const auto EnqueueFrontier = [&](TSubclassOf<UFGItemDescriptor> ItemClass, int32 Depth)
		{
			if (!ItemClass)
			{
				return;
			}
			const FString Name = UFGItemDescriptor::GetItemName(ItemClass).ToString();
			if (ProductAggregates.Contains(Name)) // graft point: real factory covers it
			{
				return;
			}
			if (ItemClass->IsChildOf(UFGResourceDescriptor::StaticClass()))
			{
				RawNames.Add(Name);
				return;
			}
			Queue.Add({ ItemClass, Depth });
		};

		for (const FSeedItem& Seed : Seeds)
		{
			EnqueueFrontier(Seed.ItemClass, 0);
		}
		for (const auto& Entry : IngredientClasses)
		{
			EnqueueFrontier(Entry.Value, 0);
		}

		for (int32 Index = 0; Index < Queue.Num(); ++Index)
		{
			if (OutSnapshot.Catalog.Num() >= Params.MaxCatalogRecipes)
			{
				OutSnapshot.Truncation.bAnyCapHit = true;
				break;
			}
			const FCatalogSeed Entry = Queue[Index];
			const FString Name = UFGItemDescriptor::GetItemName(Entry.ItemClass).ToString();
			if (Visited.Contains(Name))
			{
				continue;
			}
			Visited.Add(Name);

			// Locked recipes ARE captured (flagged), because "what will this need once it
			// unlocks" is a question the plan has to answer BEFORE the unlock. Dropping them
			// here is what made an unlocked part look immediately buildable when the same
			// research also gated one of its ingredients.
			bool bAlternates = false;
			bool bLocked = false;
			const TSubclassOf<UFGRecipe> Recipe = PickRecipeAllowingLocked(RecipeManager, Entry.ItemClass, bAlternates, bLocked);
			if (!Recipe)
			{
				continue; // genuinely no recipe in this installation — the gap walk covers it
			}

			const float Duration = UFGRecipe::GetManufacturingDuration(Recipe);
			const float CraftsPerMinute = Duration > SMALL_NUMBER ? 60.0f / Duration : 0.0f;

			FCPCatalogRecipe Captured;
			Captured.Product = MakeItemRef(Entry.ItemClass);
			Captured.RecipeName = UFGRecipe::GetRecipeName(Recipe).ToString();
			Captured.MachineName = MachineDisplayName(Recipe);
			Captured.bAlternatesExist = bAlternates;
			Captured.bRecipeLocked = bLocked;
			for (const FItemAmount& ProductAmount : UFGRecipe::GetProducts(Recipe))
			{
				if (ProductAmount.ItemClass == Entry.ItemClass)
				{
					Captured.ProductPerMinutePerMachine = ToDisplay(ProductAmount.ItemClass, static_cast<float>(ProductAmount.Amount)) * CraftsPerMinute;
				}
			}
			for (const FItemAmount& Ingredient : UFGRecipe::GetIngredients(World, Recipe))
			{
				if (!Ingredient.ItemClass || Ingredient.Amount <= 0)
				{
					continue;
				}
				FCPItemRate Rate;
				Rate.Item = MakeItemRef(Ingredient.ItemClass);
				Rate.AmountPerCraft = ToDisplay(Ingredient.ItemClass, static_cast<float>(Ingredient.Amount));
				Rate.RatePerMinute = Rate.AmountPerCraft * CraftsPerMinute; // one machine, 100% clock
				Captured.Ingredients.Add(MoveTemp(Rate));

				if (Entry.Depth < Params.CatalogWalkMaxDepth)
				{
					EnqueueFrontier(Ingredient.ItemClass, Entry.Depth + 1);
				}
			}
			OutSnapshot.Catalog.Add(MoveTemp(Captured));
		}
		OutSnapshot.RawResourceNames = RawNames.Array();
	}

	return true;
}
