// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPPanelWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/CircularThrobber.h"
#include "Components/EditableTextBox.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "CriticalPath.h"
#include "CPStyle.h"
#include "FGActorRepresentation.h"
#include "FGActorRepresentationManager.h"
#include "FGIconDatabaseSubsystem.h"
#include "GameFramework/Pawn.h"
#include "Representation/FGPingActorRepresentation.h"
#include "Resources/FGItemDescriptor.h"

#define LOCTEXT_NAMESPACE "CriticalPath"

namespace
{
UTextBlock* MakeText(UWidgetTree* Tree, const FText& Text, int32 Size, const FLinearColor& Color, bool bBold = false)
{
	UTextBlock* Block = Tree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
	Block->SetText(Text);
	Block->SetFont(CPStyle::Font(Size, bBold));
	Block->SetColorAndOpacity(FSlateColor(Color));
	return Block;
}

/** What a part's state should mean to a PLAYER, which is not the same as what it means to the
 *  analysis. The panel previously called everything that was not finished "blocked" and painted
 *  it red -- so a fresh save and a save with a burning factory looked identical, and the alarm
 *  was on for the whole playthrough. The distinction that actually matters:
 *
 *    Fault   a line EXISTS and is not working. Starved, jammed, unpowered, unrouted. The player
 *            can walk over and fix it right now. This is the only state worth red.
 *    Planned nothing is wrong; there is simply work left. The recipe is not unlocked yet, or the
 *            line has not been built yet. This is the normal condition of an unfinished game.
 *    None    delivered, ready, or otherwise fine.
 *
 *  Kept as one function because the header count, the band colour and the verdict sentence must
 *  never disagree -- they did while each re-derived this inline. */
enum class EPartConcern : uint8 { None, Planned, Fault };

EPartConcern ClassifyPart(const FCPPartReport& Part)
{
	if (Part.Status == ECPNodeStatus::Delivered || Part.Status == ECPNodeStatus::ReadyToDeliver)
	{
		return EPartConcern::None;
	}
	// Research outranks any blocker. A locked recipe REPORTS as "nothing produces it" because
	// nothing can -- treating that derived symptom as an independent fault double-counted the
	// part and told the player to go build a line they cannot build yet.
	if (Part.Status == ECPNodeStatus::RecipeLocked)
	{
		return EPartConcern::Planned;
	}
	switch (Part.Blocker.Reason)
	{
	case ECPBlockerReason::NoPower:
	case ECPBlockerReason::InputsStarved:
	case ECPBlockerReason::OutputsFull:
	case ECPBlockerReason::ByproductBackedUp:
	case ECPBlockerReason::ProducerNotConnected:
	case ECPBlockerReason::SupplyBelowDemand:
	case ECPBlockerReason::ExtractionBelowDemand:
		return EPartConcern::Fault;
	case ECPBlockerReason::NoProducer:
		// "Build a line" is a to-do, not a malfunction.
		return EPartConcern::Planned;
	case ECPBlockerReason::ConnectivityUnknown:
		// An admission that the route could not be measured. Reporting a fault we did not prove
		// would be exactly the guessing this mod refuses to do.
		return EPartConcern::Planned;
	default:
		break;
	}
	return Part.Status == ECPNodeStatus::Blocked ? EPartConcern::Planned : EPartConcern::None;
}

/** Row colour for a part, which is CPStyle::StatusColor plus the fault/planned split.
 *  StatusColor keys on ECPNodeStatus alone and cannot see WHY a part is Blocked, so it painted
 *  every unbuilt line red. Red is reserved for something the player can go fix. */
FLinearColor PartRowColor(const FCPPartReport& Part)
{
	if (Part.Status == ECPNodeStatus::Blocked && ClassifyPart(Part) != EPartConcern::Fault)
	{
		return CPStyle::StatusBlue;
	}
	return CPStyle::StatusColor(Part.Status);
}

FText StatusDetail(const FCPPartReport& Part)
{
	switch (Part.Status)
	{
	case ECPNodeStatus::Delivered:
		return LOCTEXT("StatusDelivered", "Delivered");
	case ECPNodeStatus::ReadyToDeliver:
		return LOCTEXT("StatusReadyToDeliver", "Ready to deliver");
	case ECPNodeStatus::TimeLimited:
		return Part.EtaMinutes >= 0.0f
			? FText::Format(LOCTEXT("StatusEtaFmt", "~{0} min remaining"), FText::AsNumber(FMath::RoundToInt(Part.EtaMinutes)))
			: LOCTEXT("StatusTimeLimited", "Time-limited");
	case ECPNodeStatus::UnderSupplied: return LOCTEXT("StatusUnderSupplied", "Under-supplied");
	case ECPNodeStatus::Blocked:
		// The engine's one "Blocked" splits into two things a player does differently. A line that
		// exists and has stalled is a job for right now; a line that was never built is just the
		// next thing on the list. Calling both "Blocked" made the row contradict the header, which
		// counts them as "need a fix" and "still to build".
		switch (Part.Blocker.Reason)
		{
		case ECPBlockerReason::ConnectivityUnknown:
			return LOCTEXT("StatusRouteUnknown", "Route not measured");
		case ECPBlockerReason::NoProducer:
			return LOCTEXT("StatusNotBuilt", "Not built yet");
		default:
			return ClassifyPart(Part) == EPartConcern::Fault
				? LOCTEXT("StatusNeedsFix", "Needs a fix")
				: LOCTEXT("StatusNotBuiltDefault", "Not built yet");
		}
	case ECPNodeStatus::RecipeLocked: return LOCTEXT("StatusRecipeLocked", "Recipe not researched");
	case ECPNodeStatus::Saturated: return LOCTEXT("StatusSaturated", "Saturated (surplus)");
	case ECPNodeStatus::StandbyReserve: return LOCTEXT("StatusStandbyReserve", "Reserve line (standby)");
	default: return FText::GetEmpty();
	}
}

FText ResearchSourceTag(ECPResearchSource Source)
{
	switch (Source)
	{
	case ECPResearchSource::Milestone: return LOCTEXT("SrcMilestone", "milestone");
	case ECPResearchSource::MAM:       return LOCTEXT("SrcMAM", "MAM");
	case ECPResearchSource::Shop:      return LOCTEXT("SrcShop", "AWESOME Shop");
	case ECPResearchSource::HardDrive: return LOCTEXT("SrcHardDrive", "hard drive");
	case ECPResearchSource::Tutorial:  return LOCTEXT("SrcTutorial", "HUB");
	default:                           return FText::GetEmpty();
	}
}

/** Left-edge status stripe: colour carries state at a glance, the row text names it — no
 *  legend-dependent glyphs (they also broke column alignment). */
UWidget* MakeStatusStripe(UWidgetTree* Tree, const FLinearColor& Color)
{
	USizeBox* Size = Tree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Size->SetWidthOverride(4.0f);
	UBorder* Fill = Tree->ConstructWidget<UBorder>(UBorder::StaticClass());
	Fill->SetBrushColor(Color);
	Size->AddChild(Fill);
	return Size;
}

FText BlockerReasonText(const FCPBlocker& Blocker)
{
	switch (Blocker.Reason)
	{
	case ECPBlockerReason::NoProducer:
		return LOCTEXT("BlockNoProducer", "Nothing produces it — build a line");
	case ECPBlockerReason::NoPower:
		return Blocker.TotalBuildings > 0
			? FText::Format(LOCTEXT("BlockNoPowerCountFmt", "No power — {0} of {1} machine(s) dark"),
				FText::AsNumber(Blocker.AffectedBuildings), FText::AsNumber(Blocker.TotalBuildings))
			: LOCTEXT("BlockNoPower", "Producers have no power");
	case ECPBlockerReason::SupplyBelowDemand:
		return FText::Format(Blocker.bSolverDerived
			? LOCTEXT("BlockSolvedSupplyFmt", "Only {0}/min is reaching this line; it needs {1}/min")
			: LOCTEXT("BlockSupplyFmt", "Supply {0}/min vs demand {1}/min — add capacity"),
			FText::AsNumber(FMath::RoundToInt(Blocker.SupplyPerMinute)), FText::AsNumber(FMath::RoundToInt(Blocker.DemandPerMinute)));
	case ECPBlockerReason::ExtractionBelowDemand:
		return FText::Format(Blocker.bSolverDerived
			? LOCTEXT("BlockSolvedExtractFmt", "Only {0}/min is reaching this line; it needs {1}/min — add or upgrade extraction")
			: LOCTEXT("BlockExtractFmt", "Extraction {0}/min vs demand {1}/min — add or upgrade extractors"),
			FText::AsNumber(FMath::RoundToInt(Blocker.SupplyPerMinute)), FText::AsNumber(FMath::RoundToInt(Blocker.DemandPerMinute)));
	case ECPBlockerReason::LogisticsSuspected:
		return LOCTEXT("BlockLegacyLogistics", "This reading is out of date — press REFRESH");
	case ECPBlockerReason::ProducerNotConnected:
		return Blocker.bSolverDerived
			? LOCTEXT("BlockSolvedNotConnected", "Production exists, but none reaches the machines that need it")
			: LOCTEXT("BlockNotConnected", "It is being made, but no belt or pipe connects it to what needs it");
	case ECPBlockerReason::ConnectivityUnknown:
		if (Blocker.bSolverDerived)
		{
			switch (Blocker.UnknownReason)
			{
			case ECPFlowUnknownReason::TransportRateUnknown:
				return LOCTEXT("BlockTransportRateUnknown", "Vehicle route connected — delivery rate unknown because throughput is not yet measured");
			case ECPFlowUnknownReason::PathAttributionMissing:
				return LOCTEXT("BlockPathAttributionMissing", "The feeding line could not be identified — inspect the connected producers");
			case ECPFlowUnknownReason::IncompleteOrNonConverged:
			default:
				return LOCTEXT("BlockSolvedConnectivityUnknown", "Could not measure what is arriving — check the belts and pipes feeding this");
			}
		}
		return LOCTEXT("BlockConnectivityUnknown", "Starved — the delivery path could not be fully checked; inspect its connections");
	case ECPBlockerReason::ByproductBackedUp:
		return FText::Format(LOCTEXT("BlockByproductFmt", "Outputs full — {0} has no connected destination and may be backing up (check its disposal)"),
			FText::FromString(Blocker.Byproduct.Name));
	case ECPBlockerReason::InputsStarved:
		return Blocker.TotalBuildings > 0
			? FText::Format(LOCTEXT("BlockInputsStarvedFmt", "No inputs arriving — {0} of {1} machine(s) starved (check connections and supply)"),
				FText::AsNumber(Blocker.AffectedBuildings), FText::AsNumber(Blocker.TotalBuildings))
			: LOCTEXT("BlockInputsStarved", "No inputs arriving (check connections and supply)");
	case ECPBlockerReason::OutputsFull:
		return Blocker.TotalBuildings > 0
			? FText::Format(LOCTEXT("BlockOutputsFullFmt", "Outputs full — {0} of {1} machine(s) stalled"),
				FText::AsNumber(Blocker.AffectedBuildings), FText::AsNumber(Blocker.TotalBuildings))
			: LOCTEXT("BlockOutputsFull", "Outputs full");
	default:
		return FText::GetEmpty();
	}
}

FText MachineSummary(const TArray<FCPBalanceLocation>& Locations, bool bProducers)
{
	TMap<FString, int32> Counts;
	for (const FCPBalanceLocation& Location : Locations)
	{
		if (Location.bProducer == bProducers)
		{
			Counts.FindOrAdd(Location.Label.IsEmpty() ? TEXT("Building") : Location.Label)++;
		}
	}
	TArray<FString> Labels;
	Counts.GetKeys(Labels);
	Labels.Sort();
	TArray<FString> Parts;
	for (const FString& Label : Labels)
	{
		Parts.Add(FString::Printf(TEXT("%d x %s"), Counts[Label], *Label));
	}
	return FText::FromString(FString::Join(Parts, TEXT(" + ")));
}

FText BalanceRateText(float RatePerMinute, bool bFluid)
{
	FNumberFormattingOptions Format;
	Format.SetMaximumFractionalDigits(bFluid ? 2 : 0);
	return FText::Format(LOCTEXT("BalanceRateWithUnitFmt", "{0} {1}"),
		FText::AsNumber(RatePerMinute, &Format),
		bFluid ? LOCTEXT("BalanceFluidRateUnit", "m³/min") : LOCTEXT("BalanceSolidRateUnit", "items/min"));
}

FText BalanceDiagnosis(const FCPItemBalance& Balance, FLinearColor& OutColor)
{
	const float Demand = Balance.Current.DemandPerMinute;
	const float Installed = Balance.Current.InstalledPerMinute;
	const float CurrentUse = Balance.EstimatedCurrentUsePerMinute;
	const bool bCapacityGap = Installed + KINDA_SMALL_NUMBER < Demand;
	const bool bProducerIssue = Balance.MissingInputProducerBuildings > 0 ||
		Balance.NoPowerProducerBuildings > 0 || Balance.PausedProducerBuildings > 0 ||
		Balance.OutputBlockedProducerBuildings > 0;
	if (!Balance.Current.bKnown && Balance.MachineStateConsumerBuildings == 0 && !bProducerIssue)
	{
		OutColor = CPStyle::StatusAmber;
		return LOCTEXT("BalancePartialEvidence", "MACHINE STATUS ONLY · delivery rates are not available right now");
	}

	if (Demand <= KINDA_SMALL_NUMBER)
	{
		OutColor = CPStyle::StatusNeutral;
		return LOCTEXT("BalanceNoDemand", "NOTHING IS USING THIS · it is only filling storage right now");
	}
	if (bCapacityGap && bProducerIssue)
	{
		OutColor = CPStyle::StatusRed;
		return FText::Format(LOCTEXT("BalanceCapacityMachineGapsFmt", "NOT ENOUGH MACHINES · {0} short, and some machines are stopped as well"),
			BalanceRateText(Demand - Installed, Balance.bFluid));
	}
	if (bCapacityGap)
	{
		OutColor = CPStyle::StatusRed;
		return FText::Format(LOCTEXT("BalanceCapacityGapFmt", "NOT ENOUGH MACHINES · build at least {0} more production"),
			BalanceRateText(Demand - Installed, Balance.bFluid));
	}
	if (bProducerIssue && Balance.JammedProducerBuildings > 0)
	{
		// Lead with the jam: it is the actionable one, and it is not a supply problem at all.
		OutColor = CPStyle::StatusRed;
		return FText::Format(LOCTEXT("BalanceJammedFmt",
			"BELT JAMMED · {0} machine(s) blocked by {1} on the feeding belt — remove it; this is not a supply shortage"),
			FText::AsNumber(Balance.JammedProducerBuildings),
			FText::FromString(Balance.JammedByItemName));
	}
	if (bProducerIssue)
	{
		OutColor = CPStyle::StatusRed;
		return FText::Format(LOCTEXT("BalanceProducerIssueFmt", "PRODUCER ISSUES · {0} missing input · {1} output blocked · {2} no power"),
			FText::AsNumber(Balance.MissingInputProducerBuildings),
			FText::AsNumber(Balance.OutputBlockedProducerBuildings),
			FText::AsNumber(Balance.NoPowerProducerBuildings));
	}
	if (Balance.MachineStateConsumerBuildings > 0 && CurrentUse + KINDA_SMALL_NUMBER < Demand)
	{
		OutColor = CPStyle::StatusAmber;
		return FText::Format(LOCTEXT("BalanceObservedUseGapFmt", "MACHINES RUNNING SLOW · using about {0} of {1}; {2} machine(s) are short of ingredients"),
			BalanceRateText(CurrentUse, Balance.bFluid), BalanceRateText(Demand, Balance.bFluid),
			FText::AsNumber(Balance.MissingInputConsumerBuildings));
	}
	if (Balance.MachineStateConsumerBuildings == 0 &&
		Balance.Current.DeliveredPerMinute + KINDA_SMALL_NUMBER < Demand)
	{
		OutColor = CPStyle::StatusAmber;
		return FText::Format(LOCTEXT("BalanceModeledDeliveryGapFmt", "NOT ENOUGH ARRIVING · {0} is getting through; {1} is needed"),
			BalanceRateText(Balance.Current.DeliveredPerMinute, Balance.bFluid), BalanceRateText(Demand, Balance.bFluid));
	}
	OutColor = CPStyle::StatusGreen;
	if (Balance.MachineStateConsumerBuildings == 0)
	{
		return FText::Format(LOCTEXT("BalanceModeledHealthyFmt", "SUPPLY MEETS NEED · {0} is reaching consumers; they need {1}"),
			BalanceRateText(Balance.Current.DeliveredPerMinute, Balance.bFluid), BalanceRateText(Demand, Balance.bFluid));
	}
	return FText::Format(LOCTEXT("BalanceObservedUseHealthyFmt", "SERVING CURRENT USE · consumers are using about {0} against {1} demand"),
		BalanceRateText(CurrentUse, Balance.bFluid), BalanceRateText(Demand, Balance.bFluid));
}

/** Needs attention. Output-blocked is deliberately NOT here: a backed-up machine is usually a
 *  line running ahead of its demand, which is normal, and colouring it like a fault trains the
 *  player to ignore the colour. It still sorts above healthy rows -- see BalanceLocationRank. */
bool BalanceLocationHasProblem(const FCPBalanceLocation& Location)
{
	return Location.bNoPower || Location.bPaused || Location.bMissingInput;
}

/** 0 = needs attention, 1 = benign but notable (saturated), 2 = healthy. Sort order only. */
int32 BalanceLocationRank(const FCPBalanceLocation& Location)
{
	if (BalanceLocationHasProblem(Location)) { return 0; }
	return Location.bOutputBlocked ? 1 : 2;
}

bool BalanceLocationMatchesAugmentFilter(const FCPBalanceLocation& Location, int32 Filter)
{
	return Filter == 0 || (Filter == 1 && Location.PowerShardCount > 0) ||
		(Filter == 2 && Location.SomersloopCount > 0);
}

FText BalanceLocationStatus(const FCPBalanceLocation& Location, bool bFluid)
{
	TArray<FString> Parts;
	if (!Location.bMachineStateKnown)
	{
		Parts.Add(LOCTEXT("EndpointStateUnavailable", "state unavailable").ToString());
	}
	else
	{
		if (Location.bProducing) { Parts.Add(LOCTEXT("EndpointStateProducing", "producing").ToString()); }
		if (!Location.JammedByItemName.IsEmpty())
		{
			// A jam and a shortage look identical on the machine, but the fix is opposite:
			// clear the belt vs build more supply. Name the culprit so the action is obvious.
			Parts.Add(FText::Format(LOCTEXT("EndpointStateJammedFmt", "blocked by {0} on the belt"),
				FText::FromString(Location.JammedByItemName)).ToString());
		}
		else if (Location.bMissingInput)
		{
			Parts.Add(Location.StarvedOfItemName.IsEmpty()
				? LOCTEXT("EndpointStateMissingInput", "missing input").ToString()
				: FText::Format(LOCTEXT("EndpointStateStarvedFmt", "waiting on {0}"),
					FText::FromString(Location.StarvedOfItemName)).ToString());
		}
		// "Output blocked" sounds like a fault; it usually means this line is ahead of demand.
		if (Location.bOutputBlocked) { Parts.Add(LOCTEXT("EndpointStateOutputBlocked", "output full (nothing taking it)").ToString()); }
		if (Location.bNoPower) { Parts.Add(LOCTEXT("EndpointStateNoPower", "no power").ToString()); }
		if (Location.bPaused) { Parts.Add(LOCTEXT("EndpointStatePaused", "paused").ToString()); }
		// Nothing observably wrong and not producing: between cycles, or waiting on demand. Say
		// that, rather than leaving the player wondering what "idle" is concealing.
		if (Parts.Num() == 0) { Parts.Add(LOCTEXT("EndpointStateIdle", "idle - no fault reported").ToString()); }
		Parts.Add(FText::Format(LOCTEXT("EndpointProductivityFmt", "{0}% productivity"),
			FText::AsNumber(FMath::RoundToInt(Location.ProductivityPercent))).ToString());
		Parts.Add(BalanceRateText(Location.EstimatedRatePerMinute, bFluid).ToString());
		if (Location.BufferedAmount > KINDA_SMALL_NUMBER)
		{
			FNumberFormattingOptions BufferFormat;
			BufferFormat.SetMaximumFractionalDigits(bFluid ? 2 : 0);
			Parts.Add(FText::Format(LOCTEXT("EndpointBufferFmt", "buffer {0} {1}"),
				FText::AsNumber(Location.BufferedAmount, &BufferFormat),
				bFluid ? LOCTEXT("EndpointBufferM3", "m³") : LOCTEXT("EndpointBufferItems", "items")).ToString());
		}
	}
	return FText::FromString(FString::Join(Parts, TEXT("  ·  ")));
}
}

void UCPBalanceLocator::Locate()
{
	if (!Owner)
	{
		return;
	}
	Owner->CreateLocationPing(Location, IconItem, Label);
}

void UCPPanelWidget::CreateLocationPing(const FVector& Location, const FCPItemRef& IconItem, const FText& Label)
{
	AFGActorRepresentationManager* RepresentationManager = AFGActorRepresentationManager::Get(GetWorld());
	if (!RepresentationManager)
	{
		UE_LOG(LogCriticalPath, Warning, TEXT("Could not create location ping: representation manager unavailable"));
		return;
	}

	UTexture2D* Icon = nullptr;
	if (!IconItem.DescriptorClassPath.IsEmpty())
	{
		if (UClass* DescriptorClass = FindObject<UClass>(nullptr, *IconItem.DescriptorClassPath);
			DescriptorClass && DescriptorClass->IsChildOf(UFGItemDescriptor::StaticClass()))
		{
			Icon = UFGItemDescriptor::GetSmallIcon(DescriptorClass);
		}
	}
	if (!Icon)
	{
		Icon = LoadObject<UTexture2D>(nullptr,
			TEXT("/Game/FactoryGame/Interface/UI/Assets/AttentionPing/TXUI_AttentionPing_Exclamation.TXUI_AttentionPing_Exclamation"));
	}

	constexpr float PingLifetimeSeconds = 30.0f * 60.0f;
	AFGIconDatabaseSubsystem* IconDatabase = AFGIconDatabaseSubsystem::Get(GetWorld());
	const int32 IconId = IconDatabase ? IconDatabase->GetIconIDForTexture(Icon) : INDEX_NONE;
	if (IconId == INDEX_NONE)
	{
		UE_LOG(LogCriticalPath, Warning, TEXT("Could not resolve locate icon for %s through the icon database"),
			*Label.ToString());
	}

	UCPNamedPingRepresentation* Representation = Cast<UCPNamedPingRepresentation>(
		RepresentationManager->CreateNewRepresentationNoActor(
		Location, Icon, CPStyle::Accent, PingLifetimeSeconds, true, true,
		ERepresentationType::RT_MapMarker, UCPNamedPingRepresentation::StaticClass()));
	if (!Representation)
	{
		UE_LOG(LogCriticalPath, Warning, TEXT("Could not create location ping for %s"), *Label.ToString());
		return;
	}
	FMapMarker Marker;
	Marker.MarkerGUID = FGuid::NewGuid();
	Marker.Location = Location;
	Marker.Name = Label.ToString();
	Marker.MapMarkerType = ERepresentationType::RT_MapMarker;
	Marker.IconID = IconId;
	Marker.Color = CPStyle::Accent;
	Marker.Scale = 1.0f;
	Marker.CompassViewDistance = ECompassViewDistance::CVD_Always;
	Representation->Init(Marker);
	RepresentationManager->AddRepresentation(Representation);

	ActiveLocationPings.RemoveAll([](const TObjectPtr<UFGActorRepresentation>& Existing)
	{
		return !IsValid(Existing);
	});
	ActiveLocationPings.Add(Representation);
}

void UCPPanelWidget::ClearLocationPings()
{
	if (AFGActorRepresentationManager* RepresentationManager = AFGActorRepresentationManager::Get(GetWorld()))
	{
		for (UFGActorRepresentation* Representation : ActiveLocationPings)
		{
			if (IsValid(Representation))
			{
				RepresentationManager->RemoveRepresentation(Representation);
			}
		}
	}
	ActiveLocationPings.Reset();
}

TSharedRef<SWidget> UCPPanelWidget::RebuildWidget()
{
	SetIsFocusable(true);
	// Visible (hit-testable) root + the Native mouse overrides = fully modal: every mouse event
	// over the whole screen is consumed while the panel is up.
	SetVisibility(ESlateVisibility::Visible);

	// Layout lives INSIDE the widget: the viewport slot's anchor/alignment setters proved
	// unreliable here (panel half off-screen), so the root is a fullscreen canvas — added to
	// the viewport stretched — and the frame is anchored top-center within it.
	UCanvasPanel* Root = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("PanelRoot"));
	WidgetTree->RootWidget = Root;

	USizeBox* Frame = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PanelFrame"));
	Frame->SetWidthOverride(CPStyle::PanelMaxWidth);
	Frame->SetMaxDesiredHeight(CPStyle::PanelMaxHeight); // scroll list scrolls past this
	if (UCanvasPanelSlot* FrameSlot = Root->AddChildToCanvas(Frame))
	{
		FrameSlot->SetAnchors(FAnchors(0.5f, 0.06f, 0.5f, 0.06f));
		FrameSlot->SetAlignment(FVector2D(0.5f, 0.0f));
		FrameSlot->SetAutoSize(true);
		FrameSlot->SetPosition(FVector2D::ZeroVector);
	}

	UBorder* Backdrop = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("PanelBackdrop"));
	Backdrop->SetBrushColor(CPStyle::Backdrop);
	Backdrop->SetPadding(FMargin(12.0f));
	Frame->AddChild(Backdrop);

	UVerticalBox* Stack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PanelStack"));
	Backdrop->SetContent(Stack);

	// Header band
	{
		UBorder* Band = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("HeaderBand"));
		Band->SetBrushColor(CPStyle::Surface);
		Band->SetPadding(FMargin(12.0f, 8.0f));
		UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Band->SetContent(Box);

		UTextBlock* Title = MakeText(WidgetTree, LOCTEXT("PanelTitle", "CRITICAL PATH"), 20, CPStyle::TextPrimary, true);
		if (UHorizontalBoxSlot* TitleSlot = Box->AddChildToHorizontalBox(Title))
		{
			TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TitleSlot->SetVerticalAlignment(VAlign_Center);
		}

		UButton* RefreshButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("RefreshButton"));
		RefreshButton->SetBackgroundColor(CPStyle::RowNested);
		RefreshButton->SetContent(MakeText(WidgetTree, LOCTEXT("RefreshReport", "REFRESH"), 9, CPStyle::Accent, true));
		RefreshButton->OnClicked.AddDynamic(this, &UCPPanelWidget::RequestRefresh);
		if (UHorizontalBoxSlot* RefreshSlot = Box->AddChildToHorizontalBox(RefreshButton))
		{
			RefreshSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			RefreshSlot->SetVerticalAlignment(VAlign_Center);
		}

		UButton* ClearPingsButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("ClearPingsButton"));
		ClearPingsButton->SetBackgroundColor(CPStyle::RowNested);
		ClearPingsButton->SetContent(MakeText(WidgetTree, LOCTEXT("ClearPings", "CLEAR PINGS"), 9, CPStyle::Accent, true));
		ClearPingsButton->OnClicked.AddDynamic(this, &UCPPanelWidget::ClearLocationPings);
		if (UHorizontalBoxSlot* ClearPingsSlot = Box->AddChildToHorizontalBox(ClearPingsButton))
		{
			ClearPingsSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			ClearPingsSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UVerticalBoxSlot* BandSlot = Stack->AddChildToVerticalBox(Band))
		{
			BandSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			BandSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	// Summary band (+ data age)
	{
		UBorder* Band = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("SummaryBand"));
		Band->SetBrushColor(CPStyle::Surface);
		Band->SetPadding(FMargin(12.0f, 6.0f));
		UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		Band->SetContent(Box);

		SummaryText = MakeText(WidgetTree, FText::GetEmpty(), 11, CPStyle::TextSecondary);
		if (UHorizontalBoxSlot* SummarySlot = Box->AddChildToHorizontalBox(SummaryText))
		{
			SummarySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			SummarySlot->SetVerticalAlignment(VAlign_Center);
		}
		DataAgeTextBlock = MakeText(WidgetTree, FText::GetEmpty(), 10, CPStyle::TextSecondary);
		if (UHorizontalBoxSlot* AgeSlot = Box->AddChildToHorizontalBox(DataAgeTextBlock))
		{
			AgeSlot->SetVerticalAlignment(VAlign_Center);
		}

		if (UVerticalBoxSlot* BandSlot = Stack->AddChildToVerticalBox(Band))
		{
			BandSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			BandSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	// One navigation surface for the three distinct jobs. Keeping the views mutually exclusive
	// preserves the existing panel footprint while giving Balance and Plan room to be legible.
	{
		UHorizontalBox* Tabs = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("TabBar"));
		auto AddTab = [&](const FText& Label, const FName Name, void (UCPPanelWidget::*Handler)())
		{
			UButton* Button = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
			Button->SetBackgroundColor(CPStyle::Surface);
			Button->SetContent(MakeText(WidgetTree, Label, 12, CPStyle::Accent, true));
			if (Handler == &UCPPanelWidget::ShowPathTab)
			{
				Button->OnClicked.AddDynamic(this, &UCPPanelWidget::ShowPathTab);
			}
			else if (Handler == &UCPPanelWidget::ShowBalanceTab)
			{
				Button->OnClicked.AddDynamic(this, &UCPPanelWidget::ShowBalanceTab);
			}
			else
			{
				Button->OnClicked.AddDynamic(this, &UCPPanelWidget::ShowPlanTab);
			}
			if (UHorizontalBoxSlot* Slot = Tabs->AddChildToHorizontalBox(Button))
			{
				Slot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				Slot->SetPadding(FMargin(2.0f, 0.0f));
			}
		};
		AddTab(LOCTEXT("TabPath", "PATH"), TEXT("PathTab"), &UCPPanelWidget::ShowPathTab);
		AddTab(LOCTEXT("TabBalance", "BALANCE"), TEXT("BalanceTab"), &UCPPanelWidget::ShowBalanceTab);
		AddTab(LOCTEXT("TabPlan", "PLAN"), TEXT("PlanTab"), &UCPPanelWidget::ShowPlanTab);
		if (UVerticalBoxSlot* TabsSlot = Stack->AddChildToVerticalBox(Tabs))
		{
			TabsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			TabsSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	// Balance controls stay fixed below the tabs while only the network rows scroll.
	{
		UBorder* ToolsBand = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("BalanceToolsBand"));
		ToolsBand->SetBrushColor(CPStyle::Surface);
		ToolsBand->SetPadding(FMargin(8.0f, 5.0f));
		BalanceTools = ToolsBand;
		UHorizontalBox* Tools = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		ToolsBand->SetContent(Tools);

		UEditableTextBox* Filter = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("BalanceFilter"));
		Filter->SetHintText(LOCTEXT("BalanceFilterHint", "Filter items or buildings…"));
		Filter->SetText(FText::FromString(BalanceFilter));
		Filter->WidgetStyle.SetFont(CPStyle::Font(10, false));
		Filter->WidgetStyle.SetForegroundColor(FSlateColor(CPStyle::TextPrimary));
		Filter->WidgetStyle.SetFocusedForegroundColor(FSlateColor(CPStyle::TextPrimary));
		Filter->WidgetStyle.SetBackgroundColor(FSlateColor(CPStyle::RowNested));
		Filter->WidgetStyle.SetPadding(FMargin(8.0f, 5.0f));
		Filter->OnTextChanged.AddDynamic(this, &UCPPanelWidget::OnBalanceFilterChanged);
		if (UHorizontalBoxSlot* FilterSlot = Tools->AddChildToHorizontalBox(Filter))
		{
			FilterSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			FilterSlot->SetVerticalAlignment(VAlign_Center);
		}

		UButton* AugmentButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BalanceAugmentFilterButton"));
		AugmentButton->SetBackgroundColor(CPStyle::RowNested);
		const FText AugmentFilterLabel = BalanceAugmentFilter == 1
			? LOCTEXT("BalanceAugmentShards", "POWER SHARDS")
			: (BalanceAugmentFilter == 2
				? LOCTEXT("BalanceAugmentSloops", "SOMERSLOOPS")
				: LOCTEXT("BalanceAugmentAll", "ALL AUGMENTS"));
		BalanceAugmentFilterText = MakeText(WidgetTree, AugmentFilterLabel,
			9, CPStyle::Accent, true);
		AugmentButton->SetContent(BalanceAugmentFilterText);
		AugmentButton->OnClicked.AddDynamic(this, &UCPPanelWidget::ToggleBalanceAugmentFilter);
		if (UHorizontalBoxSlot* AugmentSlot = Tools->AddChildToHorizontalBox(AugmentButton))
		{
			AugmentSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			AugmentSlot->SetVerticalAlignment(VAlign_Fill);
		}

		UButton* SortButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("BalanceSortButton"));
		SortButton->SetBackgroundColor(CPStyle::RowNested);
		BalanceSortText = MakeText(WidgetTree,
			bBalanceNeedsFirst ? LOCTEXT("BalanceSortNeedFirst", "NEED FIRST") : LOCTEXT("BalanceSortAlphabetical", "A–Z"),
			9, CPStyle::Accent, true);
		SortButton->SetContent(BalanceSortText);
		SortButton->OnClicked.AddDynamic(this, &UCPPanelWidget::ToggleBalanceSort);
		if (UHorizontalBoxSlot* SortSlot = Tools->AddChildToHorizontalBox(SortButton))
		{
			SortSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			SortSlot->SetVerticalAlignment(VAlign_Fill);
		}
		if (UVerticalBoxSlot* ToolsSlot = Stack->AddChildToVerticalBox(ToolsBand))
		{
			ToolsSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
			ToolsSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	// Scrollable body: Elevator and Milestone sections side by side, deep-chain research gaps
	// full-width below them.
	{
		PathScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("PathScroll"));
		UVerticalBox* Body = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PathBody"));
		PathScroll->AddChild(Body);

		UHorizontalBox* Columns = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(), TEXT("SectionColumns"));
		ElevatorColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ElevatorColumn"));
		MilestoneColumn = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("MilestoneColumn"));
		if (UHorizontalBoxSlot* LeftSlot = Columns->AddChildToHorizontalBox(ElevatorColumn))
		{
			LeftSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			LeftSlot->SetPadding(FMargin(0.0f, 0.0f, 4.0f, 0.0f));
			LeftSlot->SetVerticalAlignment(VAlign_Top);
		}
		if (UHorizontalBoxSlot* RightSlot = Columns->AddChildToHorizontalBox(MilestoneColumn))
		{
			RightSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			RightSlot->SetPadding(FMargin(4.0f, 0.0f, 0.0f, 0.0f));
			RightSlot->SetVerticalAlignment(VAlign_Top);
		}
		Body->AddChildToVerticalBox(Columns);

		GapList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("GapList"));
		if (UVerticalBoxSlot* GapSlot = Body->AddChildToVerticalBox(GapList))
		{
			GapSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
			GapSlot->SetHorizontalAlignment(HAlign_Fill);
		}

		if (UVerticalBoxSlot* ScrollSlot = Stack->AddChildToVerticalBox(PathScroll))
		{
			ScrollSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			ScrollSlot->SetHorizontalAlignment(HAlign_Fill);
			ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}

	// Objective-scoped production balance. Rows are populated from the solver-backed domains.
	{
		BalanceScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("BalanceScroll"));
		BalanceList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("BalanceList"));
		BalanceScroll->AddChild(BalanceList);
		if (UVerticalBoxSlot* ScrollSlot = Stack->AddChildToVerticalBox(BalanceScroll))
		{
			ScrollSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			ScrollSlot->SetHorizontalAlignment(HAlign_Fill);
			ScrollSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}

	// Full-height Plan view; the compact verdict remains visible below every tab.
	{
		PlanScroll = WidgetTree->ConstructWidget<UScrollBox>(UScrollBox::StaticClass(), TEXT("PlanScroll"));
		PlanList = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("PlanList"));
		PlanScroll->AddChild(PlanList);
		if (UVerticalBoxSlot* PlanSlot = Stack->AddChildToVerticalBox(PlanScroll))
		{
			PlanSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 8.0f));
			PlanSlot->SetHorizontalAlignment(HAlign_Fill);
			PlanSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
	}

	// Compact report verdict: persistent across Path, Balance, and Plan.
	{
		VerdictBand = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("VerdictBand"));
		VerdictBand->SetBrushColor(CPStyle::VerdictBlockedBand);
		VerdictBand->SetPadding(FMargin(12.0f, 8.0f));

		VerdictText = MakeText(WidgetTree, FText::GetEmpty(), 14, CPStyle::TextPrimary, true);
		// Explicit wrap (AutoWrap under-reports desired height): panel width minus backdrop (24)
		// and band (24) padding, with a little slack.
		VerdictText->SetWrapTextAt(CPStyle::PanelMaxWidth - 56.0f);
		VerdictBand->SetContent(VerdictText);

		if (UVerticalBoxSlot* BandSlot = Stack->AddChildToVerticalBox(VerdictBand))
		{
			BandSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	SetActiveTab(0);

	return Super::RebuildWidget();
}

void UCPPanelWidget::RequestRefresh()
{
	// Feedback first: capture happens on the game thread and the solve lands a moment later, so
	// without this the button can look inert on a large factory.
	if (DataAgeTextBlock)
	{
		DataAgeTextBlock->SetText(LOCTEXT("AgeRefreshing", "re-measuring..."));
	}
	OnRefreshRequested.Broadcast();
}

void UCPPanelWidget::SetUnavailable(const FText& Headline, const FText& Detail)
{
	if (!ElevatorColumn || !MilestoneColumn || !GapList || !BalanceList)
	{
		return;
	}
	CachedReport = FCPAnalysisResult();
	bCachedBalancePending = false;
	ElevatorColumn->ClearChildren();
	MilestoneColumn->ClearChildren();
	GapList->ClearChildren();
	BalanceList->ClearChildren();
	// The plan is a view of a report we no longer have. Leaving it behind would show a joined
	// client the build order from whatever singleplayer save was open last — a stale answer
	// presented with no indication that it describes a different world entirely.
	if (PlanList)
	{
		PlanList->ClearChildren();
	}
	ChainToggles.Reset();
	BalanceLocators.Reset();
	Spinners.Reset();

	// No report means no verdict. The band is constructed red and keeps its last colour, so
	// leaving it visible renders an empty alarm-coloured bar under a message that explicitly
	// says nothing is wrong with the factory.
	if (SummaryText)
	{
		SummaryText->SetText(FText::GetEmpty());
	}
	if (VerdictBand)
	{
		VerdictBand->SetVisibility(ESlateVisibility::Collapsed);
	}

	// Amber, not red: nothing is wrong with the factory, the data simply cannot be read here.
	if (UTextBlock* HeadlineText = MakeText(WidgetTree, Headline, 13, CPStyle::StatusAmber, true))
	{
		GapList->AddChildToVerticalBox(HeadlineText);
	}
	if (UTextBlock* DetailText = MakeText(WidgetTree, Detail, 11, CPStyle::TextSecondary))
	{
		DetailText->SetAutoWrapText(true);
		if (UVerticalBoxSlot* DetailSlot = GapList->AddChildToVerticalBox(DetailText))
		{
			DetailSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 0.0f));
		}
	}
	if (DataAgeTextBlock)
	{
		DataAgeTextBlock->SetText(LOCTEXT("AgeUnavailable", "unavailable here"));
	}
}

void UCPPanelWidget::SetReport(const FCPAnalysisResult& Result, const FText& DataAgeText, bool bBalancePending)
{
	if (!ElevatorColumn || !MilestoneColumn || !GapList || !BalanceList)
	{
		return;
	}
	CachedReport = Result;
	bCachedBalancePending = bBalancePending;
	ElevatorColumn->ClearChildren();
	MilestoneColumn->ClearChildren();
	GapList->ClearChildren();
	BalanceList->ClearChildren();
	ChainToggles.Reset();
	BalanceLocators.Reset();
	Spinners.Reset();

	// A recipe-locked part's unlocking research goes ON its row — a separate gap row for the
	// same item read as a redundant copy of the list. Only deep-chain gaps (items that are not
	// objective parts themselves) render as their own rows below the columns.
	TMap<FString, FString> ResearchByItem;
	for (const FCPResearchGap& Gap : Result.ResearchGaps)
	{
		if (!Gap.UnlockSchematicName.IsEmpty())
		{
			const FText SourceTag = ResearchSourceTag(Gap.UnlockSource);
			ResearchByItem.FindOrAdd(Gap.NeededItem.Name, SourceTag.IsEmpty()
				? Gap.UnlockSchematicName
				: FString::Printf(TEXT("%s (%s)"), *Gap.UnlockSchematicName, *SourceTag.ToString()));
		}
	}
	TSet<FString> PartItemNames;

	// Research-gated and factory-blocked are counted apart on purpose. Lumping them into one
	// "blocked" number made the panel report a fault for the ordinary act of not having finished
	// the tech tree yet -- true for most of a playthrough, and therefore meaningless.
	int32 PartsTotal = 0, PartsDone = 0, PartsReady = 0, PartsStillToBuild = 0, PartsFactoryBlocked = 0;
	bool bAnyMilestone = false;
	auto RenderObjective = [&](const FCPObjectiveReport& Objective)
	{
		for (const FCPPartReport& Part : Objective.Parts)
		{
			PartsTotal++;
			PartItemNames.Add(Part.Item.Name);
			// Chain nodes count as "shown" too — an item explained inside a rendered planned
			// chain must not repeat in the Research-needed-upstream section (live-caught:
			// Plutonium Waste appeared in both, with conflicting explanations).
			for (const FCPPlannedNode& Node : Part.PlannedChain)
			{
				if (Node.Kind == ECPPlanNodeKind::ResearchLocked)
				{
					PartItemNames.Add(Node.Item.Name);
				}
			}
			if (Part.Status == ECPNodeStatus::Delivered)
			{
				PartsDone++;
			}
			if (Part.Status == ECPNodeStatus::ReadyToDeliver)
			{
				PartsReady++;
			}
			switch (ClassifyPart(Part))
			{
			case EPartConcern::Fault:   PartsFactoryBlocked++; break;
			case EPartConcern::Planned: PartsStillToBuild++;   break;
			default: break;
			}
			const FString* Research = ResearchByItem.Find(Part.Item.Name);
			// The limiter is for SUPPLIED-BUT-SLOW chains. A hard-stopped line (no power, no
			// producer, not connected, byproduct-clogged) makes rate ratios meaningless noise —
			// live-caught: an unpowered, unfed Manufacturer showed 'Limiter: Wire' as if the
			// line were merely slow. Show it only when the blocker is absent or is itself a
			// rate deficit, and only when it names a DIFFERENT link than the blocker.
			const bool bBlockerIsRateShaped = Part.Blocker.Reason == ECPBlockerReason::None ||
				Part.Blocker.Reason == ECPBlockerReason::SupplyBelowDemand ||
				Part.Blocker.Reason == ECPBlockerReason::ExtractionBelowDemand;
			const bool bLimiterStatusAllowsAdvice = Part.Status == ECPNodeStatus::UnderSupplied ||
				Part.Status == ECPNodeStatus::TimeLimited;
			const bool bShowLimiter = Part.Limiter.IsSet() &&
				Part.Limiter.InstalledPerMinute > KINDA_SMALL_NUMBER &&
				bLimiterStatusAllowsAdvice && bBlockerIsRateShaped &&
				!Part.Limiter.Item.Name.Equals(Part.Blocker.Item.Name, ESearchCase::IgnoreCase);
			// M2.7b: a recipe-locked part expands into its unlocking schematic's cost plan.
			const FCPResearchGapReport* ResearchPlan = Part.Status == ECPNodeStatus::RecipeLocked
				? Result.ResearchPlans.FindByPredicate([&Part](const FCPResearchGapReport& Plan)
					{ return Plan.Gap.NeededItem.Name.Equals(Part.Item.Name, ESearchCase::IgnoreCase); })
				: nullptr;
			// No plan because the unlocking research IS the selected milestone: its live
			// ledger is the Milestone column — say so instead of showing nothing.
			const FCPResearchGap* SelectedMilestoneGap = nullptr;
			if (Part.Status == ECPNodeStatus::RecipeLocked && !ResearchPlan)
			{
				SelectedMilestoneGap = Result.ResearchGaps.FindByPredicate([&Part](const FCPResearchGap& Gap)
					{ return Gap.bUnlockIsSelectedMilestone && Gap.NeededItem.Name.Equals(Part.Item.Name, ESearchCase::IgnoreCase); });
			}
			const bool bHasChain = Part.Blocker.Reason != ECPBlockerReason::None || bShowLimiter ||
				Part.Issues.Num() > 0 ||
				(ResearchPlan && ResearchPlan->CostParts.Num() > 0) || SelectedMilestoneGap != nullptr;
			UTextBlock* Chevron = nullptr;
			UBorder* Row = AddPartRow(Part, Research ? *Research : FString(), bHasChain, Chevron);
			if (bHasChain)
			{
				// Every concurrent at-machine issue, worst first — one reason is half the truth.
				UVerticalBox* ChainBox = AddChainRows(Part.Blocker, Part.Issues.Num() > 0);
				for (const FCPBlocker& Issue : Part.Issues)
				{
					AddReasonRow(Issue, ChainBox);
				}
				if (bShowLimiter)
				{
					AddLimiterRow(Part.Limiter, ChainBox);
				}
				if (ResearchPlan)
				{
					AddResearchPlanRows(*ResearchPlan, ChainBox);
				}
				if (SelectedMilestoneGap)
				{
					UBorder* NoteBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
					NoteBorder->SetBrushColor(CPStyle::RowNested);
					NoteBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
					UHorizontalBox* NoteBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
					NoteBorder->SetContent(NoteBox);
					if (UHorizontalBoxSlot* StripeSlot = NoteBox->AddChildToHorizontalBox(MakeStatusStripe(WidgetTree, CPStyle::StatusViolet)))
					{
						StripeSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
						StripeSlot->SetVerticalAlignment(VAlign_Fill);
					}
					UTextBlock* Note = MakeText(WidgetTree,
						FText::Format(LOCTEXT("ResearchIsSelectedFmt", "Unlocked by {0} — your SELECTED milestone: its payment progress is the Milestone column →"),
							FText::FromString(SelectedMilestoneGap->UnlockSchematicName)),
						10, CPStyle::TextSecondary);
					Note->SetWrapTextAt(CPStyle::ColumnNoticeWrap);
					if (UHorizontalBoxSlot* NoteSlot = NoteBox->AddChildToHorizontalBox(Note))
					{
						NoteSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
						NoteSlot->SetVerticalAlignment(VAlign_Center);
						NoteSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));
					}
					if (UVerticalBoxSlot* RowSlot = ChainBox->AddChildToVerticalBox(NoteBorder))
					{
						RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
						RowSlot->SetHorizontalAlignment(HAlign_Fill);
					}
				}
				AddPlannedRows(Part.PlannedChain, ChainBox);
				if (Row && ChainBox)
				{
					UCPChainToggle* Toggle = NewObject<UCPChainToggle>(this);
					Toggle->Target = ChainBox;
					Toggle->Chevron = Chevron;
					ChainToggles.Add(Toggle);
					Row->OnMouseButtonDownEvent.BindDynamic(Toggle, &UCPChainToggle::OnRowMouseDown);

					// Collapsed by default: the part rows are the distilled view, detail on click.
					ChainBox->SetVisibility(ESlateVisibility::Collapsed);
					if (Chevron)
					{
						Chevron->SetRenderTransformAngle(-90.0f);
					}
				}
			}
		}
	};

	// Left column: the Space Elevator phase. Right column: the HUB milestone — always present
	// so its empty states can speak.
	CurrentList = ElevatorColumn;
	bool bAnyElevator = false;
	for (const FCPObjectiveReport& Objective : Result.Objectives)
	{
		if (Objective.Kind == ECPObjectiveKind::SpaceElevatorPhase)
		{
			bAnyElevator = true;
			AddSectionHeader(FText::Format(LOCTEXT("SectionElevatorFmt", "Elevator: {0}"), FText::FromString(Objective.ObjectiveName)));
			RenderObjective(Objective);
		}
	}
	if (!bAnyElevator)
	{
		AddSectionHeader(LOCTEXT("SectionElevatorNone", "Elevator"));
		AddNoticeRow(LOCTEXT("ElevatorNone", "No outstanding Space Elevator phase."));
	}

	CurrentList = MilestoneColumn;
	for (const FCPObjectiveReport& Objective : Result.Objectives)
	{
		if (Objective.Kind == ECPObjectiveKind::Milestone)
		{
			bAnyMilestone = true;
			AddSectionHeader(FText::Format(LOCTEXT("SectionMilestoneFmt", "Milestone: {0}"), FText::FromString(Objective.ObjectiveName)));
			RenderObjective(Objective);
		}
	}
	if (!bAnyMilestone)
	{
		AddSectionHeader(LOCTEXT("SectionMilestoneNone", "Milestone"));
		if (Result.bMilestoneSelected)
		{
			// Selected but nothing outstanding: it's fully paid, only the redeem step remains.
			AddNoticeRow(LOCTEXT("MilestonePaid", "Current milestone is fully paid — collect it and select the next one at the HUB."));
		}
		else if (Result.SelectableMilestoneCount > 0)
		{
			AddNoticeRow(FText::Format(LOCTEXT("MilestoneNoneSelectedFmt", "No milestone selected — {0} available. Choose one at the HUB."),
				FText::AsNumber(Result.SelectableMilestoneCount)));
		}
		else
		{
			AddNoticeRow(LOCTEXT("MilestoneNoneAvailable", "No milestones available in this elevator phase."));
		}
	}

	// Deep-chain research gaps only (part-level gaps are already on their rows).
	CurrentList = GapList;
	bool bAnyDeepGap = false;
	for (const FCPResearchGap& Gap : Result.ResearchGaps)
	{
		if (!PartItemNames.Contains(Gap.NeededItem.Name))
		{
			if (!bAnyDeepGap)
			{
				bAnyDeepGap = true;
				AddSectionHeader(LOCTEXT("SectionResearch", "Research needed upstream"));
			}
			AddResearchGapRow(Gap);
		}
	}
	CurrentList = nullptr;
	AddBalanceRows(Result, bBalancePending);

	// The Plan lives in the band, one home only.
	PlanList->ClearChildren();
	AddPlanRows(Result, PlanList);
	AddPlanExclusionRows(Result, ResearchByItem, PlanList);
	// Only non-zero counts are shown. "0 ready to deliver · 0 blocked" is developer habit: it makes
	// the reader parse four numbers to learn three of them are nothing.
	{
		TArray<FText> Segments;
		// Plural form, not a bare suffix: an early-game phase can legitimately need exactly one
		// part, and "1 parts" is the kind of detail that makes a tool look unfinished.
		Segments.Add(FText::Format(
			LOCTEXT("SummaryPartsFmt", "{0} {0}|plural(one=part,other=parts)"), PartsTotal));
		if (PartsDone > 0)
		{
			Segments.Add(FText::Format(LOCTEXT("SummaryDoneFmt", "{0} delivered"), FText::AsNumber(PartsDone)));
		}
		if (PartsReady > 0)
		{
			Segments.Add(FText::Format(LOCTEXT("SummaryReadyFmt", "{0} ready to deliver"), FText::AsNumber(PartsReady)));
		}
		if (PartsFactoryBlocked > 0)
		{
			Segments.Add(FText::Format(LOCTEXT("SummaryBlockedFmt", "{0} need a fix"), FText::AsNumber(PartsFactoryBlocked)));
		}
		if (PartsStillToBuild > 0)
		{
			Segments.Add(FText::Format(LOCTEXT("SummaryToBuildFmt", "{0} still to build"), FText::AsNumber(PartsStillToBuild)));
		}
		SummaryText->SetText(FText::Join(FText::FromString(TEXT("  ·  ")), Segments));
	}

	// Truncation/staleness honesty (lifecycle states)
	FText Age = DataAgeText;
	if (Result.Truncation.bAnyCapHit)
	{
		Age = FText::Format(LOCTEXT("TruncFmt", "{0}  ·  only {1} of {2} checked — too many to scan"), Age,
			FText::AsNumber(Result.Truncation.BuildingsScanned), FText::AsNumber(Result.Truncation.BuildingsAvailable));
	}
	DataAgeTextBlock->SetText(Age);

	// Restored here because SetUnavailable collapses it; a panel that recovers must not stay mute.
	if (VerdictBand)
	{
		VerdictBand->SetVisibility(ESlateVisibility::Visible);
	}
	VerdictText->SetText(MakeVerdict(Result));
	// Red only for a fault the player can go fix. Waiting on research is blue: it is progress,
	// not a problem, and it is the state the panel will be in for most of the game.
	const FLinearColor BandColor = PartsFactoryBlocked > 0 ? CPStyle::VerdictBlockedBand
		: PartsStillToBuild > 0 ? CPStyle::VerdictNextBand
		: CPStyle::VerdictReadyBand;
	const FLinearColor VerdictColor = PartsFactoryBlocked > 0 ? CPStyle::StatusRed
		: PartsStillToBuild > 0 ? CPStyle::StatusBlue
		: CPStyle::StatusGreen;
	VerdictBand->SetBrushColor(BandColor);
	VerdictText->SetColorAndOpacity(FSlateColor(VerdictColor));
}

void UCPPanelWidget::SetActiveTab(int32 TabIndex)
{
	if (PathScroll)
	{
		PathScroll->SetVisibility(TabIndex == 0 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (BalanceScroll)
	{
		BalanceScroll->SetVisibility(TabIndex == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (BalanceTools)
	{
		BalanceTools->SetVisibility(TabIndex == 1 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	if (PlanScroll)
	{
		PlanScroll->SetVisibility(TabIndex == 2 ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UCPPanelWidget::ShowPathTab()
{
	SetActiveTab(0);
}

void UCPPanelWidget::ShowBalanceTab()
{
	SetActiveTab(1);
}

void UCPPanelWidget::ShowPlanTab()
{
	SetActiveTab(2);
}

void UCPPanelWidget::OnBalanceFilterChanged(const FText& Text)
{
	BalanceFilter = Text.ToString().TrimStartAndEnd();
	ApplyBalanceFilter();
}

void UCPPanelWidget::ToggleBalanceSort()
{
	bBalanceNeedsFirst = !bBalanceNeedsFirst;
	if (BalanceSortText)
	{
		BalanceSortText->SetText(bBalanceNeedsFirst
			? LOCTEXT("BalanceSortNeedFirst", "NEED FIRST")
			: LOCTEXT("BalanceSortAlphabetical", "A–Z"));
	}
	RebuildBalanceRows();
}

void UCPPanelWidget::ToggleBalanceAugmentFilter()
{
	BalanceAugmentFilter = (BalanceAugmentFilter + 1) % 3;
	if (BalanceAugmentFilterText)
	{
		BalanceAugmentFilterText->SetText(BalanceAugmentFilter == 1
			? LOCTEXT("BalanceAugmentShards", "POWER SHARDS")
			: (BalanceAugmentFilter == 2
				? LOCTEXT("BalanceAugmentSloops", "SOMERSLOOPS")
				: LOCTEXT("BalanceAugmentAll", "ALL AUGMENTS")));
	}
	RebuildBalanceRows();
}

void UCPPanelWidget::RebuildBalanceRows()
{
	if (BalanceList)
	{
		BalanceList->ClearChildren();
		BalanceLocators.Reset();
		AddBalanceRows(CachedReport, bCachedBalancePending);
	}
}

void UCPPanelWidget::ApplyBalanceFilter()
{
	int32 VisibleRows = 0;
	for (int32 Index = 0; Index < BalanceFilterRows.Num(); ++Index)
	{
		const bool bVisible = BalanceFilter.IsEmpty() ||
			(BalanceFilterKeys.IsValidIndex(Index) && BalanceFilterKeys[Index].Contains(BalanceFilter, ESearchCase::IgnoreCase));
		if (BalanceFilterRows[Index])
		{
			BalanceFilterRows[Index]->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
		VisibleRows += bVisible ? 1 : 0;
	}
	if (BalanceNoMatchesRow)
	{
		BalanceNoMatchesRow->SetVisibility(!BalanceFilter.IsEmpty() && VisibleRows == 0
			? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
}

void UCPPanelWidget::AddBalanceRows(const FCPAnalysisResult& Result, bool bBalancePending)
{
	BalanceFilterRows.Reset();
	BalanceFilterKeys.Reset();
	BalanceNoMatchesRow = nullptr;
	TSet<FString> RelevantItems;
	auto AddPartItems = [&RelevantItems](const FCPPartReport& Part)
	{
		RelevantItems.Add(Part.Item.Name);
		for (const FCPItemRef& Item : Part.Blocker.Path)
		{
			RelevantItems.Add(Item.Name);
		}
		for (const FCPItemRef& Item : Part.Limiter.Path)
		{
			RelevantItems.Add(Item.Name);
		}
		for (const FCPPlannedNode& Node : Part.PlannedChain)
		{
			RelevantItems.Add(Node.Item.Name);
		}
	};
	for (const FCPObjectiveReport& Objective : Result.Objectives)
	{
		for (const FCPPartReport& Part : Objective.Parts)
		{
			AddPartItems(Part);
		}
	}
	for (const FCPResearchGapReport& Plan : Result.ResearchPlans)
	{
		for (const FCPPartReport& Part : Plan.CostParts)
		{
			AddPartItems(Part);
		}
	}
	// Sufficiency is already restricted to items visited by the objective/research dependency
	// walk, so it fills in healthy intermediate ingredients that have no blocker row.
	for (const FCPItemSufficiency& Item : Result.Sufficiency)
	{
		RelevantItems.Add(Item.Item.Name);
	}

	UBorder* IntroBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	IntroBorder->SetBrushColor(CPStyle::Surface);
	IntroBorder->SetPadding(FMargin(10.0f, 7.0f));
	UTextBlock* Intro = MakeText(WidgetTree,
		LOCTEXT("BalanceIntro", "Every line feeding your objective: what it makes, what arrives, and how long your stock lasts."),
		10, CPStyle::TextSecondary);
	Intro->SetAutoWrapText(true);
	IntroBorder->SetContent(Intro);
	if (UVerticalBoxSlot* IntroSlot = BalanceList->AddChildToVerticalBox(IntroBorder))
	{
		IntroSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 6.0f));
	}

	TArray<const FCPItemBalance*> Candidates;
	TMap<FString, int32> DomainTotals;
	for (const FCPItemBalance& Candidate : Result.ItemBalances)
	{
		if (!RelevantItems.Contains(Candidate.Item.Name) ||
			(Candidate.ProducerBuildings == 0 && Candidate.ConsumerBuildings == 0))
		{
			continue;
		}
		if (BalanceAugmentFilter != 0 && !Candidate.Locations.ContainsByPredicate([this](const FCPBalanceLocation& Location)
		{
			return BalanceLocationMatchesAugmentFilter(Location, BalanceAugmentFilter);
		}))
		{
			continue;
		}
		Candidates.Add(&Candidate);
		DomainTotals.FindOrAdd(Candidate.Item.Name)++;
	}
	Candidates.Sort([this](const FCPItemBalance& A, const FCPItemBalance& B)
	{
		if (bBalanceNeedsFirst)
		{
			auto AttentionRank = [](const FCPItemBalance& Balance)
			{
				if (!Balance.Current.bKnown) return 0;
				const bool bMachineIssue = Balance.MissingInputProducerBuildings > 0 ||
					Balance.NoPowerProducerBuildings > 0 || Balance.PausedProducerBuildings > 0 ||
					Balance.OutputBlockedProducerBuildings > 0 || Balance.MissingInputConsumerBuildings > 0;
				const float EvidenceRate = Balance.MachineStateConsumerBuildings > 0
					? Balance.EstimatedCurrentUsePerMinute : Balance.Current.DeliveredPerMinute;
				if (bMachineIssue || (Balance.Current.DemandPerMinute > KINDA_SMALL_NUMBER &&
					EvidenceRate + KINDA_SMALL_NUMBER < Balance.Current.DemandPerMinute)) return 1;
				if (Balance.Current.DemandPerMinute > KINDA_SMALL_NUMBER) return 2;
				return 3;
			};
			const int32 RankA = AttentionRank(A);
			const int32 RankB = AttentionRank(B);
			if (RankA != RankB) return RankA < RankB;
			const float EvidenceA = A.MachineStateConsumerBuildings > 0
				? A.EstimatedCurrentUsePerMinute : A.Current.DeliveredPerMinute;
			const float EvidenceB = B.MachineStateConsumerBuildings > 0
				? B.EstimatedCurrentUsePerMinute : B.Current.DeliveredPerMinute;
			const float GapA = A.Current.DemandPerMinute - EvidenceA;
			const float GapB = B.Current.DemandPerMinute - EvidenceB;
			if (!FMath::IsNearlyEqual(GapA, GapB)) return GapA > GapB;
		}
		const int32 NameOrder = A.Item.Name.Compare(B.Item.Name, ESearchCase::IgnoreCase);
		return NameOrder != 0 ? NameOrder < 0 : A.DomainId < B.DomainId;
	});
	TMap<FString, int32> DomainOrdinal;
	int32 RowsAdded = 0;
	for (const FCPItemBalance* BalancePtr : Candidates)
	{
		const FCPItemBalance& Balance = *BalancePtr;
		RowsAdded++;
		const int32 NetworkNumber = ++DomainOrdinal.FindOrAdd(Balance.Item.Name);

		UBorder* Row = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Row->SetBrushColor(CPStyle::Row);
		Row->SetPadding(FMargin(10.0f, 7.0f));
		UVerticalBox* RowStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		Row->SetContent(RowStack);
		UVerticalBox* EndpointDetails = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

		UHorizontalBox* TitleBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		AddItemIcon(TitleBox, Balance.Item, 22.0f);
		int32 LinePowerShards = 0;
		int32 LineSomersloops = 0;
		FCPItemRef PowerShardItem;
		FCPItemRef SomersloopItem;
		for (const FCPBalanceLocation& Location : Balance.Locations)
		{
			LinePowerShards += Location.PowerShardCount;
			LineSomersloops += Location.SomersloopCount;
			if (!PowerShardItem.IsValid() && Location.PowerShardItem.IsValid()) { PowerShardItem = Location.PowerShardItem; }
			if (!SomersloopItem.IsValid() && Location.SomersloopItem.IsValid()) { SomersloopItem = Location.SomersloopItem; }
		}
		const FText TitleText = DomainTotals.FindRef(Balance.Item.Name) > 1
			? FText::Format(LOCTEXT("BalanceTitleMultiFmt", "{0}  ·  line {1}"),
				FText::FromString(Balance.Item.Name), FText::AsNumber(NetworkNumber))
			: FText::FromString(Balance.Item.Name);
		UTextBlock* Title = MakeText(WidgetTree, TitleText, 12, CPStyle::TextPrimary, true);
		if (UHorizontalBoxSlot* TitleSlot = TitleBox->AddChildToHorizontalBox(Title))
		{
			TitleSlot->SetVerticalAlignment(VAlign_Center);
			TitleSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		}
		if (LinePowerShards > 0)
		{
			AddItemIcon(TitleBox, PowerShardItem, 17.0f);
			TitleBox->AddChildToHorizontalBox(MakeText(WidgetTree, FText::AsNumber(LinePowerShards),
				9, CPStyle::StatusBlue, true))->SetVerticalAlignment(VAlign_Center);
		}
		if (LineSomersloops > 0)
		{
			AddItemIcon(TitleBox, SomersloopItem, 17.0f);
			TitleBox->AddChildToHorizontalBox(MakeText(WidgetTree, FText::AsNumber(LineSomersloops),
				9, CPStyle::StatusViolet, true))->SetVerticalAlignment(VAlign_Center);
		}
		UTextBlock* EndpointChevron = MakeText(WidgetTree, LOCTEXT("BalanceExpandChevron", "▼"), 10, CPStyle::TextSecondary, true);
		TitleBox->AddChildToHorizontalBox(EndpointChevron)->SetVerticalAlignment(VAlign_Center);
		UBorder* TitleClick = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		TitleClick->SetBrushColor(CPStyle::Row);
		TitleClick->SetContent(TitleBox);
		UCPChainToggle* EndpointToggle = NewObject<UCPChainToggle>(this);
		EndpointToggle->Target = EndpointDetails;
		EndpointToggle->Chevron = EndpointChevron;
		ChainToggles.Add(EndpointToggle);
		TitleClick->OnMouseButtonDownEvent.BindDynamic(EndpointToggle, &UCPChainToggle::OnRowMouseDown);
		RowStack->AddChildToVerticalBox(TitleClick)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));

		// Identify the physical line in player terms, then provide one consumer-side locate action.
		UHorizontalBox* IdentityBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		const FText ProducerSummary = Balance.ProducerBuildings > 0
			? MachineSummary(Balance.Locations, true) : LOCTEXT("BalanceNoProducer", "no producer");
		const FText ConsumerSummary = Balance.ConsumerBuildings > 0
			? MachineSummary(Balance.Locations, false) : LOCTEXT("BalanceNoConsumer", "no connected consumer");
		const FCPBalanceLocation* Anchor = nullptr;
		const APawn* Pawn = GetOwningPlayerPawn();
		float BestDistanceSq = TNumericLimits<float>::Max();
		const bool bPreferConsumer = Balance.ConsumerBuildings > 0;
		for (const FCPBalanceLocation& Location : Balance.Locations)
		{
			if (Location.bProducer == bPreferConsumer)
			{
				continue;
			}
			const float DistanceSq = Pawn ? FVector::DistSquared(Pawn->GetActorLocation(), Location.Location) : 0.0f;
			if (!Anchor || DistanceSq < BestDistanceSq)
			{
				Anchor = &Location;
				BestDistanceSq = DistanceSq;
			}
		}
		FText DistanceText = FText::GetEmpty();
		if (Anchor && Pawn)
		{
			DistanceText = FText::Format(LOCTEXT("BalanceDistanceFmt", "  ·  nearest endpoint {0} m away"),
				FText::AsNumber(FMath::RoundToInt(FMath::Sqrt(BestDistanceSq) / 100.0f)));
		}
		UTextBlock* Identity = MakeText(WidgetTree,
			FText::Format(LOCTEXT("BalanceIdentityFmt", "made by {0}  ·  used by {1}{2}"), ProducerSummary, ConsumerSummary, DistanceText),
			9, CPStyle::TextSecondary);
		if (UHorizontalBoxSlot* IdentitySlot = IdentityBox->AddChildToHorizontalBox(Identity))
		{
			IdentitySlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			IdentitySlot->SetVerticalAlignment(VAlign_Center);
		}
		if (Anchor)
		{
			UCPBalanceLocator* Locator = NewObject<UCPBalanceLocator>(this);
			Locator->Owner = this;
			Locator->Location = Anchor->Location;
			Locator->IconItem = Anchor->BuildingItem.DescriptorClassPath.IsEmpty()
				? Balance.Item : Anchor->BuildingItem;
			Locator->Label = FText::Format(LOCTEXT("BalanceAnchorPingFmt", "{0} - {1}"),
				FText::FromString(Anchor->Label), FText::FromString(Balance.Item.Name));
			BalanceLocators.Add(Locator);
			UButton* LocateButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
			LocateButton->SetBackgroundColor(CPStyle::Surface);
			LocateButton->SetContent(MakeText(WidgetTree,
				bPreferConsumer ? LOCTEXT("LocateConsumer", "LOCATE CONSUMER") : LOCTEXT("LocateProducer", "LOCATE PRODUCER"),
				9, CPStyle::Accent, true));
			LocateButton->OnClicked.AddDynamic(Locator, &UCPBalanceLocator::Locate);
			if (UHorizontalBoxSlot* LocateSlot = IdentityBox->AddChildToHorizontalBox(LocateButton))
			{
				LocateSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
				LocateSlot->SetVerticalAlignment(VAlign_Center);
			}
		}
		RowStack->AddChildToVerticalBox(IdentityBox)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));

		auto AddEndpointSection = [&](bool bProducers, const FText& Heading)
		{
			TArray<const FCPBalanceLocation*> Endpoints;
			for (const FCPBalanceLocation& Location : Balance.Locations)
			{
				if (Location.bProducer == bProducers &&
					BalanceLocationMatchesAugmentFilter(Location, BalanceAugmentFilter))
				{
					Endpoints.Add(&Location);
				}
			}
			Endpoints.Sort([](const FCPBalanceLocation& A, const FCPBalanceLocation& B)
			{
				const int32 RankA = BalanceLocationRank(A);
				const int32 RankB = BalanceLocationRank(B);
				return RankA != RankB ? RankA < RankB : A.ActorName < B.ActorName;
			});
			if (Endpoints.Num() == 0) { return; }
			EndpointDetails->AddChildToVerticalBox(MakeText(WidgetTree, Heading, 9, CPStyle::Accent, true))
				->SetPadding(FMargin(10.0f, 4.0f, 0.0f, 2.0f));
			for (int32 EndpointIndex = 0; EndpointIndex < Endpoints.Num(); ++EndpointIndex)
			{
				const FCPBalanceLocation& Location = *Endpoints[EndpointIndex];
				UHorizontalBox* EndpointLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				if (Location.PowerShardCount > 0)
				{
					AddItemIcon(EndpointLine, Location.PowerShardItem, 16.0f);
					EndpointLine->AddChildToHorizontalBox(MakeText(WidgetTree, FText::AsNumber(Location.PowerShardCount),
						8, CPStyle::StatusBlue, true))->SetVerticalAlignment(VAlign_Center);
				}
				if (Location.SomersloopCount > 0)
				{
					AddItemIcon(EndpointLine, Location.SomersloopItem, 16.0f);
					EndpointLine->AddChildToHorizontalBox(MakeText(WidgetTree, FText::AsNumber(Location.SomersloopCount),
						8, CPStyle::StatusViolet, true))->SetVerticalAlignment(VAlign_Center);
				}
				const FText EndpointText = FText::Format(LOCTEXT("BalanceEndpointFmt", "{0}. {1}  —  {2}"),
					FText::AsNumber(EndpointIndex + 1), FText::FromString(Location.Label),
					BalanceLocationStatus(Location, Balance.bFluid));
				UTextBlock* EndpointLabel = MakeText(WidgetTree, EndpointText, 9,
					BalanceLocationHasProblem(Location) ? CPStyle::StatusRed
						: (Location.bOutputBlocked ? CPStyle::StatusNeutral : CPStyle::TextSecondary));
				EndpointLabel->SetAutoWrapText(true);
				if (UHorizontalBoxSlot* LabelSlot = EndpointLine->AddChildToHorizontalBox(EndpointLabel))
				{
					LabelSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					LabelSlot->SetVerticalAlignment(VAlign_Center);
				}
				UCPBalanceLocator* EndpointLocator = NewObject<UCPBalanceLocator>(this);
				EndpointLocator->Owner = this;
				EndpointLocator->Location = Location.Location;
				EndpointLocator->IconItem = Location.BuildingItem.DescriptorClassPath.IsEmpty()
					? Balance.Item : Location.BuildingItem;
				EndpointLocator->Label = FText::Format(LOCTEXT("BalanceEndpointPingFmt", "{0} - {1} - {2}"),
					FText::FromString(Location.Label), FText::FromString(Balance.Item.Name),
					FText::AsNumber(EndpointIndex + 1));
				BalanceLocators.Add(EndpointLocator);
				UButton* EndpointLocate = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass());
				EndpointLocate->SetBackgroundColor(CPStyle::Surface);
				EndpointLocate->SetContent(MakeText(WidgetTree, LOCTEXT("LocateEndpoint", "LOCATE"), 8, CPStyle::Accent, true));
				EndpointLocate->OnClicked.AddDynamic(EndpointLocator, &UCPBalanceLocator::Locate);
				if (UHorizontalBoxSlot* LocateSlot = EndpointLine->AddChildToHorizontalBox(EndpointLocate))
				{
					LocateSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
					LocateSlot->SetVerticalAlignment(VAlign_Center);
				}
				EndpointDetails->AddChildToVerticalBox(EndpointLine)->SetPadding(FMargin(16.0f, 2.0f, 0.0f, 2.0f));
			}
		};
		AddEndpointSection(true, LOCTEXT("BalanceProducersHeading", "PRODUCERS"));
		AddEndpointSection(false, LOCTEXT("BalanceConsumersHeading", "CONSUMERS"));
		EndpointDetails->SetVisibility(ESlateVisibility::Collapsed);
		EndpointChevron->SetRenderTransformAngle(-90.0f);
		RowStack->AddChildToVerticalBox(EndpointDetails)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));

		if (!Balance.Current.bKnown)
		{
			RowStack->AddChildToVerticalBox(MakeText(WidgetTree,
				LOCTEXT("BalanceUnknown", "Delivery rates are not available right now. Machine status and built capacity are shown below."),
				10, CPStyle::StatusAmber));
		}
		{
			FLinearColor DiagnosisColor;
			UBorder* DiagnosisStrip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			DiagnosisStrip->SetBrushColor(CPStyle::RowNested);
			DiagnosisStrip->SetPadding(FMargin(7.0f, 4.0f));
			DiagnosisStrip->SetContent(MakeText(WidgetTree, BalanceDiagnosis(Balance, DiagnosisColor), 9, DiagnosisColor, true));
			RowStack->AddChildToVerticalBox(DiagnosisStrip)->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 5.0f));

			const float Maximum = FMath::Max(1.0f, FMath::Max(
				FMath::Max(Balance.Current.DemandPerMinute, Balance.Current.InstalledPerMinute),
				FMath::Max(Balance.EstimatedCurrentOutputPerMinute, Balance.EstimatedCurrentUsePerMinute)));
			auto AddBar = [&](const FText& Label, float Value, const FLinearColor& Color)
			{
				UHorizontalBox* BarLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				USizeBox* LabelWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				LabelWidth->SetWidthOverride(155.0f);
				LabelWidth->AddChild(MakeText(WidgetTree, Label, 9, CPStyle::TextSecondary));
				BarLine->AddChildToHorizontalBox(LabelWidth)->SetVerticalAlignment(VAlign_Center);
				UProgressBar* Bar = WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass());
				Bar->SetPercent(FMath::Clamp(Value / Maximum, 0.0f, 1.0f));
				Bar->SetFillColorAndOpacity(Color);
				// The default track is near-white: without this the meters read as pale-on-pale.
				Bar->WidgetStyle.BackgroundImage.TintColor = FSlateColor(CPStyle::MeterTrack);
				if (UHorizontalBoxSlot* BarSlot = BarLine->AddChildToHorizontalBox(Bar))
				{
					BarSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					BarSlot->SetVerticalAlignment(VAlign_Center);
					BarSlot->SetPadding(FMargin(4.0f, 0.0f, 8.0f, 0.0f));
				}
				USizeBox* ValueWidth = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
				ValueWidth->SetWidthOverride(130.0f);
				ValueWidth->AddChild(MakeText(WidgetTree,
					BalanceRateText(Value, Balance.bFluid),
					9, CPStyle::TextPrimary, true));
				BarLine->AddChildToHorizontalBox(ValueWidth)->SetVerticalAlignment(VAlign_Center);
				RowStack->AddChildToVerticalBox(BarLine)->SetPadding(FMargin(0.0f, 1.0f));
			};
			AddBar(LOCTEXT("BalanceDemand", "CONSUMER NEED"), Balance.Current.DemandPerMinute, CPStyle::MeterReference);
			AddBar(LOCTEXT("BalanceInstalled", "BUILT CAPACITY"), Balance.Current.InstalledPerMinute, CPStyle::MeterCapacity);
			if (Balance.MachineStateProducerBuildings > 0)
			{
				AddBar(LOCTEXT("BalanceEstimatedOutput", "EST. CURRENT OUTPUT"), Balance.EstimatedCurrentOutputPerMinute, CPStyle::MeterOutput);
			}
			if (Balance.MachineStateConsumerBuildings > 0)
			{
				AddBar(LOCTEXT("BalanceEstimatedUse", "EST. CURRENT USE"), Balance.EstimatedCurrentUsePerMinute, CPStyle::MeterUse);
			}
			if (Balance.Current.bKnown)
			{
				RowStack->AddChildToVerticalBox(MakeText(WidgetTree,
					FText::Format(LOCTEXT("BalanceModelFmt", "SUPPLY LINE  can send {0}  ·  actually arriving {1}"),
						BalanceRateText(Balance.Current.SustainablePerMinute, Balance.bFluid),
						BalanceRateText(Balance.Current.DeliveredPerMinute, Balance.bFluid)),
					9, CPStyle::TextSecondary))->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
			}
			if (Balance.Design.bKnown)
			{
				RowStack->AddChildToVerticalBox(MakeText(WidgetTree,
					FText::Format(LOCTEXT("BalanceDesignFmt", "AT FULL SPEED  needs {0}  ·  ingredients allow {1}  ·  would deliver {2}"),
						BalanceRateText(Balance.Design.DemandPerMinute, Balance.bFluid),
						BalanceRateText(Balance.Design.SustainablePerMinute, Balance.bFluid),
						BalanceRateText(Balance.Design.DeliveredPerMinute, Balance.bFluid)),
					9, CPStyle::TextSecondary))->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
			}

			UBorder* BufferStrip = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			BufferStrip->SetBrushColor(CPStyle::RowNested);
			BufferStrip->SetPadding(FMargin(7.0f, 4.0f));
			const FText BufferUnit = Balance.bFluid ? LOCTEXT("BufferM3", "m³") : LOCTEXT("BufferItems", "items");
			FNumberFormattingOptions RunwayFormat;
			RunwayFormat.SetMaximumFractionalDigits(1);
			const FText BufferText = Balance.BufferRunwayMinutes >= 0.0f
				? FText::Format(LOCTEXT("BufferRunwayFmt", "BUFFER  {0} {1} reachable  ·  about {2} min at the current shortfall"),
					FText::AsNumber(FMath::RoundToInt(Balance.BufferedAmount)), BufferUnit,
					FText::AsNumber(Balance.BufferRunwayMinutes, &RunwayFormat))
				: FText::Format(LOCTEXT("BufferAmountFmt", "BUFFER  {0} {1} reachable"),
					FText::AsNumber(FMath::RoundToInt(Balance.BufferedAmount)), BufferUnit);
			BufferStrip->SetContent(MakeText(WidgetTree, BufferText, 9, CPStyle::StatusViolet, true));
			RowStack->AddChildToVerticalBox(BufferStrip)->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 0.0f));

			if (Balance.bUsesTransport && !Balance.bTransportRateKnown)
			{
				RowStack->AddChildToVerticalBox(MakeText(WidgetTree,
					LOCTEXT("TransportTopologyOnly", "Transport route is known; vehicle throughput is not yet measured."),
					9, CPStyle::StatusAmber))->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 0.0f));
			}
		}

		if (UVerticalBoxSlot* BalanceSlot = BalanceList->AddChildToVerticalBox(Row))
		{
			BalanceSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 4.0f));
			BalanceSlot->SetHorizontalAlignment(HAlign_Fill);
		}
		BalanceFilterRows.Add(Row);
		FString FilterKey = Balance.Item.Name;
		for (const FCPBalanceLocation& Location : Balance.Locations)
		{
			FilterKey += TEXT(" ") + Location.Label;
			if (Location.PowerShardCount > 0) { FilterKey += TEXT(" power shard overclock"); }
			if (Location.SomersloopCount > 0) { FilterKey += TEXT(" somersloop production boost"); }
		}
		BalanceFilterKeys.Add(MoveTemp(FilterKey));
	}

	if (RowsAdded == 0)
	{
		UBorder* Empty = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		Empty->SetBrushColor(CPStyle::Row);
		Empty->SetPadding(FMargin(10.0f, 8.0f));
		if (bBalancePending)
		{
			UHorizontalBox* PendingBox = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			UCircularThrobber* Throbber = WidgetTree->ConstructWidget<UCircularThrobber>(UCircularThrobber::StaticClass());
			Throbber->SetNumberOfPieces(8);
			Throbber->SetPeriod(0.75f);
			USizeBox* ThrobberSize = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
			ThrobberSize->SetWidthOverride(22.0f);
			ThrobberSize->SetHeightOverride(22.0f);
			ThrobberSize->AddChild(Throbber);
			if (UHorizontalBoxSlot* ThrobberSlot = PendingBox->AddChildToHorizontalBox(ThrobberSize))
			{
				ThrobberSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
				ThrobberSlot->SetVerticalAlignment(VAlign_Center);
			}
			UTextBlock* PendingText = MakeText(WidgetTree,
				LOCTEXT("BalancePending", "Checking your factory…"),
				11, CPStyle::Accent, true);
			PendingBox->AddChildToHorizontalBox(PendingText)->SetVerticalAlignment(VAlign_Center);
			Empty->SetContent(PendingBox);
		}
		else
		{
			Empty->SetContent(MakeText(WidgetTree,
				LOCTEXT("BalanceEmpty", "Nothing on your objective path has a production line yet."),
				11, CPStyle::TextSecondary));
		}
		BalanceList->AddChildToVerticalBox(Empty);
	}
	else
	{
		BalanceNoMatchesRow = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		BalanceNoMatchesRow->SetBrushColor(CPStyle::Row);
		BalanceNoMatchesRow->SetPadding(FMargin(10.0f, 8.0f));
		BalanceNoMatchesRow->SetContent(MakeText(WidgetTree,
			LOCTEXT("BalanceNoMatches", "No balance rows match this filter."), 11, CPStyle::TextSecondary));
		BalanceNoMatchesRow->SetVisibility(ESlateVisibility::Collapsed);
		BalanceList->AddChildToVerticalBox(BalanceNoMatchesRow);
		ApplyBalanceFilter();
	}
}

void UCPPanelWidget::AddSectionHeader(const FText& Title)
{
	UBorder* HeaderBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	HeaderBorder->SetBrushColor(CPStyle::Surface);
	HeaderBorder->SetPadding(FMargin(8.0f, 5.0f, 10.0f, 5.0f));
	HeaderBorder->SetContent(MakeText(WidgetTree, Title, 14, CPStyle::Accent, true));
	if (UVerticalBoxSlot* RowSlot = CurrentList->AddChildToVerticalBox(HeaderBorder))
	{
		// Extra top spacing separates sections; first child's is invisible against the band gap.
		RowSlot->SetPadding(FMargin(0.0f, CurrentList->GetChildrenCount() == 0 ? 0.0f : 8.0f, 0.0f, 2.0f));
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void UCPPanelWidget::AddNoticeRow(const FText& Notice)
{
	UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	RowBorder->SetBrushColor(CPStyle::Row);
	RowBorder->SetPadding(FMargin(8.0f, 6.0f, 10.0f, 6.0f));
	UTextBlock* Text = MakeText(WidgetTree, Notice, 11, CPStyle::TextSecondary);
	Text->SetWrapTextAt(CPStyle::ColumnNoticeWrap); // notices live inside a half-width column
	RowBorder->SetContent(Text);
	if (UVerticalBoxSlot* RowSlot = CurrentList->AddChildToVerticalBox(RowBorder))
	{
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void UCPPanelWidget::AddItemIcon(UHorizontalBox* Box, const FCPItemRef& Item, float IconSize)
{
	if (Item.DescriptorClassPath.IsEmpty())
	{
		return;
	}
	// Descriptor classes are already in memory for anything the factory/objectives reference;
	// this resolves without triggering a load of unloaded assets.
	UClass* DescriptorClass = FindObject<UClass>(nullptr, *Item.DescriptorClassPath);
	UTexture2D* Icon = DescriptorClass && DescriptorClass->IsChildOf(UFGItemDescriptor::StaticClass())
		? UFGItemDescriptor::GetSmallIcon(DescriptorClass)
		: nullptr;
	if (!Icon)
	{
		return;
	}

	UImage* Image = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
	RootedIconTextures.AddUnique(Icon);
	Image->SetBrushFromTexture(Icon, false);
	Image->SetDesiredSizeOverride(FVector2D(IconSize, IconSize));
	if (UHorizontalBoxSlot* IconSlot = Box->AddChildToHorizontalBox(Image))
	{
		IconSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		IconSlot->SetVerticalAlignment(VAlign_Center);
	}
}

UBorder* UCPPanelWidget::AddPartRow(const FCPPartReport& Part, const FString& ResearchName, bool bHasChain, UTextBlock*& OutChevron)
{
	UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	RowBorder->SetBrushColor(CPStyle::Row);
	RowBorder->SetPadding(FMargin(8.0f, 6.0f, 10.0f, 6.0f));

	// Inspector tooltip: the full ledger the row's one-liner compresses.
	{
		FTextBuilder Tip;
		Tip.AppendLine(FText::FromString(Part.Item.Name));
		if (Part.Status == ECPNodeStatus::Delivered)
		{
			Tip.AppendLine(FText::Format(LOCTEXT("TipDeliveredFmt", "Delivered: all {0}"), FText::AsNumber(Part.Required)));
		}
		else
		{
			Tip.AppendLine(FText::Format(LOCTEXT("TipNeededFmt", "Still needed: {0}  (banked {1}, to produce {2})"),
				FText::AsNumber(Part.Remaining), FText::AsNumber(Part.Banked), FText::AsNumber(Part.StillToProduce)));
		}
		if (Part.OwnedInStorage + Part.OwnedInDepot + Part.OwnedInPockets > 0)
		{
			Tip.AppendLine(FText::Format(LOCTEXT("TipOwnedFmt", "Owned: {0} in storage  ·  {1} in depot  ·  {2} carried"),
				FText::AsNumber(Part.OwnedInStorage), FText::AsNumber(Part.OwnedInDepot), FText::AsNumber(Part.OwnedInPockets)));
		}
		if (Part.EffectiveRatePerMinute > 0.01f)
		{
			Tip.AppendLine(FText::Format(LOCTEXT("TipRateFmt", "Producing at {0}/min"),
				FText::AsNumber(FMath::RoundToInt(Part.EffectiveRatePerMinute))));
		}
		if (bHasChain)
		{
			Tip.AppendLine(LOCTEXT("TipChainHint", "Click to show/hide the blocker chain"));
		}
		RowBorder->SetToolTipText(Tip.ToText());
	}

	// Left padding 0 so the status stripe sits flush with the row edge.
	RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	RowBorder->SetContent(Box);

	if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(MakeStatusStripe(WidgetTree, PartRowColor(Part))))
	{
		StripeSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		StripeSlot->SetVerticalAlignment(VAlign_Fill);
	}

	// Reserved fixed-size activity/status slot (game-texture icons, DesignSystem.md status-icon
	// table): rotating cogwheel = actively producing; static icons say what to do. Empty (but
	// space-reserving) when there's nothing to say, so alignment is structural.
	{
		USizeBox* ActivitySlotBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		ActivitySlotBox->SetWidthOverride(20.0f);
		ActivitySlotBox->SetHeightOverride(20.0f);

		const bool bMoving = Part.EffectiveRatePerMinute > 0.01f;
		const TCHAR* IconPath = nullptr;
		bool bSpin = false;
		switch (Part.Status)
		{
		case ECPNodeStatus::Delivered:      IconPath = CPStyle::IconDelivered; break;
		case ECPNodeStatus::ReadyToDeliver: IconPath = CPStyle::IconBanked; break;
		// The warning triangle is for a line that has actually stopped. A line you simply have not
		// built yet gets no icon: the blue stripe and "Not built yet" already say it, and an alert
		// glyph on every unstarted part is the same false alarm in pictorial form.
		case ECPNodeStatus::Blocked:
			IconPath = ClassifyPart(Part) == EPartConcern::Fault ? CPStyle::IconAlert : nullptr;
			break;
		case ECPNodeStatus::RecipeLocked:   IconPath = CPStyle::IconResearch; break;
		case ECPNodeStatus::UnderSupplied:  IconPath = bMoving ? CPStyle::IconSlow : CPStyle::IconAlert; break;
		default:
			if (bMoving)
			{
				IconPath = CPStyle::IconProducing;
				bSpin = true;
			}
			break;
		}

		if (IconPath)
		{
			if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, IconPath))
			{
				RootedIconTextures.AddUnique(Tex);
				UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
				Icon->SetBrushFromTexture(Tex, false);
				Icon->SetDesiredSizeOverride(FVector2D(18.0f, 18.0f));
				Icon->SetColorAndOpacity(PartRowColor(Part));
				ActivitySlotBox->AddChild(Icon);
				if (bSpin)
				{
					Icon->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
					Spinners.Add(Icon);
				}
			}
		}
		if (UHorizontalBoxSlot* ActSlot = Box->AddChildToHorizontalBox(ActivitySlotBox))
		{
			ActSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
			ActSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	AddItemIcon(Box, Part.Item, 24.0f);

	// Name over ledger: two lines fit the half-width column without crowding or overlap.
	UVerticalBox* TextStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	if (UHorizontalBoxSlot* TextSlot = Box->AddChildToHorizontalBox(TextStack))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		TextSlot->SetPadding(FMargin(0.0f, 6.0f, 0.0f, 6.0f));
	}
	// Disclosure chevron: ▼ when expanded, rotated -90° (pointing right) when collapsed —
	// rotation instead of a second glyph keeps us inside the font's verified geometric set.
	OutChevron = nullptr;
	if (bHasChain)
	{
		UHorizontalBox* NameLine = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		UTextBlock* NameText = MakeText(WidgetTree, FText::FromString(Part.Item.Name), 13, CPStyle::TextPrimary, true);
		NameLine->AddChildToHorizontalBox(NameText);
		OutChevron = MakeText(WidgetTree, FText::FromString(TEXT("▼")), 10, CPStyle::TextSecondary);
		OutChevron->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		if (UHorizontalBoxSlot* ChevronSlot = NameLine->AddChildToHorizontalBox(OutChevron))
		{
			ChevronSlot->SetPadding(FMargin(8.0f, 0.0f, 0.0f, 0.0f));
			ChevronSlot->SetVerticalAlignment(VAlign_Center);
		}
		TextStack->AddChildToVerticalBox(NameLine);
	}
	else
	{
		TextStack->AddChildToVerticalBox(MakeText(WidgetTree, FText::FromString(Part.Item.Name), 13, CPStyle::TextPrimary, true));
	}

	// A recipe-locked part names its unlocking research inline (replaces the generic detail).
	const FText Detail0 = Part.Status == ECPNodeStatus::RecipeLocked && !ResearchName.IsEmpty()
		? FText::Format(LOCTEXT("StatusResearchFmt", "Research: {0}"), FText::FromString(ResearchName))
		: StatusDetail(Part);

	FText Ledger;
	if (Part.Status == ECPNodeStatus::Delivered)
	{
		Ledger = FText::Format(LOCTEXT("LedgerDeliveredFmt", "{0} delivered"), FText::AsNumber(Part.Required));
	}
	else if (Part.StillToProduce > 0 && Part.Banked > 0)
	{
		Ledger = FText::Format(LOCTEXT("LedgerFullFmt", "{0} needed  ·  {1} banked  ·  {2} to produce  ·  {3}"),
			FText::AsNumber(Part.Remaining), FText::AsNumber(Part.Banked), FText::AsNumber(Part.StillToProduce), Detail0);
	}
	else if (Part.StillToProduce > 0)
	{
		Ledger = FText::Format(LOCTEXT("LedgerNoBankFmt", "{0} needed  ·  {1}"),
			FText::AsNumber(Part.Remaining), Detail0);
	}
	else
	{
		Ledger = FText::Format(LOCTEXT("LedgerBankedFmt", "{0} needed  ·  {0} banked  ·  {1}"),
			FText::AsNumber(Part.Remaining), Detail0);
	}
	UTextBlock* Detail = MakeText(WidgetTree, Ledger, 11, CPStyle::TextSecondary);
	Detail->SetWrapTextAt(CPStyle::ColumnNoticeWrap);
	if (UVerticalBoxSlot* DetailSlot = TextStack->AddChildToVerticalBox(Detail))
	{
		DetailSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	}

	if (UVerticalBoxSlot* RowSlot = CurrentList->AddChildToVerticalBox(RowBorder))
	{
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	return RowBorder;
}

void UCPPanelWidget::AddReasonRow(const FCPBlocker& Reason, UVerticalBox* Target)
{
	UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	RowBorder->SetBrushColor(CPStyle::RowNested);
	RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	RowBorder->SetContent(Box);
	if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(MakeStatusStripe(WidgetTree, CPStyle::StatusRed)))
	{
		StripeSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		StripeSlot->SetVerticalAlignment(VAlign_Fill);
	}
	UTextBlock* ReasonText = MakeText(WidgetTree, BlockerReasonText(Reason), 11, CPStyle::TextPrimary);
	ReasonText->SetWrapTextAt(CPStyle::ColumnNoticeWrap - 20.0f);
	if (UHorizontalBoxSlot* ReasonSlot = Box->AddChildToHorizontalBox(ReasonText))
	{
		ReasonSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		ReasonSlot->SetVerticalAlignment(VAlign_Center);
		ReasonSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));
	}
	if (UVerticalBoxSlot* RowSlot = Target->AddChildToVerticalBox(RowBorder))
	{
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

UVerticalBox* UCPPanelWidget::AddChainRows(const FCPBlocker& Blocker, bool bSuppressAtPartReason)
{
	// Chain rows live in their own container so the part row can toggle them as one unit.
	UVerticalBox* ChainBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());

	// Blocker AT the part itself (path length 1): no chain hops to carry the reason, so render
	// it directly — unless the caller's issue list already covers the at-part conditions.
	if (!bSuppressAtPartReason && Blocker.Reason != ECPBlockerReason::None && Blocker.Path.Num() <= 1)
	{
		AddReasonRow(Blocker, ChainBox);
	}

	// Path[0] is the part itself (already a row); render the chain below it, indented per depth.
	for (int32 Index = 1; Index < Blocker.Path.Num(); ++Index)
	{
		const bool bIsBlockerNode = Index == Blocker.Path.Num() - 1;

		UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RowBorder->SetBrushColor(CPStyle::RowNested);
		RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RowBorder->SetContent(Box);

		if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(
			MakeStatusStripe(WidgetTree, bIsBlockerNode ? CPStyle::StatusRed : CPStyle::StatusAmber)))
		{
			StripeSlot->SetVerticalAlignment(VAlign_Fill);
		}

		USizeBox* Indent = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Indent->SetWidthOverride(CPStyle::IndentPerDepth * Index);
		Box->AddChildToHorizontalBox(Indent);

		AddItemIcon(Box, Blocker.Path[Index], 18.0f);

		UTextBlock* Name = MakeText(WidgetTree, FText::FromString(Blocker.Path[Index].Name), 12, CPStyle::TextPrimary);
		if (UHorizontalBoxSlot* NameSlot = Box->AddChildToHorizontalBox(Name))
		{
			NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			NameSlot->SetVerticalAlignment(VAlign_Center);
			NameSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));
		}

		if (bIsBlockerNode)
		{
			UTextBlock* Reason = MakeText(WidgetTree, BlockerReasonText(Blocker), 10, CPStyle::TextSecondary);
			Reason->SetWrapTextAt(CPStyle::ColumnNoticeWrap * 0.6f);
			if (UHorizontalBoxSlot* ReasonSlot = Box->AddChildToHorizontalBox(Reason))
			{
				ReasonSlot->SetVerticalAlignment(VAlign_Center);
			}
		}

		if (UVerticalBoxSlot* RowSlot = ChainBox->AddChildToVerticalBox(RowBorder))
		{
			RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	if (UVerticalBoxSlot* BoxSlot = CurrentList->AddChildToVerticalBox(ChainBox))
	{
		BoxSlot->SetHorizontalAlignment(HAlign_Fill);
	}
	return ChainBox;
}

void UCPPanelWidget::AddLimiterRow(const FCPLimiter& Limiter, UVerticalBox* Target)
{
	if (!Target)
	{
		return;
	}

	UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	RowBorder->SetBrushColor(CPStyle::RowNested);
	RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	RowBorder->SetContent(Box);

	if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(MakeStatusStripe(WidgetTree, CPStyle::StatusAmber)))
	{
		StripeSlot->SetVerticalAlignment(VAlign_Fill);
	}

	USizeBox* Indent = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
	Indent->SetWidthOverride(CPStyle::IndentPerDepth);
	Box->AddChildToHorizontalBox(Indent);

	// Tachometer: the "producing slowly" token — the limiter is by definition the slow link.
	if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, CPStyle::IconSlow))
	{
		RootedIconTextures.AddUnique(Tex);
		UImage* Icon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		Icon->SetBrushFromTexture(Tex, false);
		Icon->SetDesiredSizeOverride(FVector2D(14.0f, 14.0f));
		Icon->SetColorAndOpacity(CPStyle::StatusAmber);
		if (UHorizontalBoxSlot* IconSlot = Box->AddChildToHorizontalBox(Icon))
		{
			IconSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
			IconSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	AddItemIcon(Box, Limiter.Item, 18.0f);

	// Same name-over-detail hierarchy as every other nested row.
	UVerticalBox* TextStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
	if (UHorizontalBoxSlot* TextSlot = Box->AddChildToHorizontalBox(TextStack))
	{
		TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		TextSlot->SetVerticalAlignment(VAlign_Center);
		TextSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));
	}
	TextStack->AddChildToVerticalBox(MakeText(WidgetTree,
		FText::Format(LOCTEXT("LimiterNameFmt", "Bottleneck: {0}"), FText::FromString(Limiter.Item.Name)),
		12, CPStyle::TextPrimary, true));

	FText Detail = FText::Format(
		Limiter.bSolverDerived
			? LOCTEXT("LimiterSolvedFmt", "Only {0}/min is getting through; the machines on this line need {1}/min ({2}%)")
			: Limiter.bNetworkScoped
			// Honesty: the ratio is the item's WORST island anywhere in the factory — chain-to-
			// island attribution needs M3 transport/routing data.
			? LOCTEXT("LimiterNetFmt", "Across the whole factory only {0}/min is available against {1}/min needed ({2}%) — this may be a different line")
			: LOCTEXT("LimiterAggFmt", "Only {0}/min is available for {1}/min needed ({2}%); some delivery routes could not be checked"),
		FText::AsNumber(FMath::RoundToInt(Limiter.SupplyPerMinute)),
		FText::AsNumber(FMath::RoundToInt(Limiter.DemandPerMinute)),
		FText::AsNumber(FMath::RoundToInt(Limiter.Ratio * 100.0f)));
	if (Limiter.bSolverDerived)
	{
		FText Constraint;
		switch (Limiter.ConstraintKind)
		{
		case ECPFlowConstraintKind::InstalledCapacity:
			Constraint = FText::Format(
				LOCTEXT("LimiterInstalledConstraintFmt", "Machines here can make {0}; the machines using it need {1}"),
				BalanceRateText(Limiter.InstalledPerMinute, Limiter.Item.Form == ECPItemForm::Fluid),
				BalanceRateText(Limiter.DemandPerMinute, Limiter.Item.Form == ECPItemForm::Fluid));
			break;
		case ECPFlowConstraintKind::InputSupport:
			Constraint = FText::Format(
				LOCTEXT("LimiterInputConstraintFmt", "Arriving ingredients only support {0} of the {1} these machines could make"),
				BalanceRateText(Limiter.SustainablePerMinute, Limiter.Item.Form == ECPItemForm::Fluid),
				BalanceRateText(Limiter.InstalledPerMinute, Limiter.Item.Form == ECPItemForm::Fluid));
			break;
		case ECPFlowConstraintKind::DeliveryEdge:
			if (Limiter.EdgeEvidence.IsSet())
			{
				const FText From = Limiter.EdgeEvidence.FromLabel.IsEmpty()
					? LOCTEXT("LimiterEdgeUpstreamFallback", "upstream node")
					: FText::FromString(Limiter.EdgeEvidence.FromLabel);
				const FText To = Limiter.EdgeEvidence.ToLabel.IsEmpty()
					? LOCTEXT("LimiterEdgeDownstreamFallback", "downstream node")
					: FText::FromString(Limiter.EdgeEvidence.ToLabel);
				Constraint = FText::Format(
					Limiter.EdgeEvidence.bFluid
						? LOCTEXT("LimiterPipeConstraintFmt", "Pipe bottleneck: {0} → {1} carries {2} of {3}")
						: LOCTEXT("LimiterBeltConstraintFmt", "Belt bottleneck: {0} → {1} carries {2} of {3}"),
					From, To,
					BalanceRateText(Limiter.EdgeEvidence.TotalFlowPerMinute, Limiter.EdgeEvidence.bFluid),
					BalanceRateText(Limiter.EdgeEvidence.CapacityPerMinute, Limiter.EdgeEvidence.bFluid));
			}
			break;
		case ECPFlowConstraintKind::DeliveryRouting:
			Constraint = FText::Format(
				LOCTEXT("LimiterRoutingConstraintFmt", "The machines can supply {0}, but only {1} reaches consumers; inspect the route between them"),
				BalanceRateText(Limiter.SustainablePerMinute, Limiter.Item.Form == ECPItemForm::Fluid),
				BalanceRateText(Limiter.SupplyPerMinute, Limiter.Item.Form == ECPItemForm::Fluid));
			break;
		case ECPFlowConstraintKind::None:
		default:
			break;
		}
		if (!Constraint.IsEmpty())
		{
			Detail = FText::Format(LOCTEXT("LimiterConstraintJoinFmt", "{0}  ·  {1}"), Detail, Constraint);
		}
	}
	if (Limiter.BufferMinutes >= 0.0f)
	{
		Detail = Limiter.BufferMinutes < 1.0f
			? FText::Format(LOCTEXT("LimiterBufferDryFmt", "{0}  ·  Stored buffer is dry"), Detail)
			: FText::Format(LOCTEXT("LimiterBufferFmt", "{0}  ·  Stored buffer covers ~{1} min"),
				Detail, FText::AsNumber(FMath::RoundToInt(Limiter.BufferMinutes)));
	}
	if (Limiter.bMarginalSolverDerived && Limiter.MarginalRatio >= 0.0f)
	{
		if (Limiter.MarginalAction == ECPMarginalActionKind::AddAverageMachine)
		{
			Detail = FText::Format(LOCTEXT("LimiterExactMachineMarginalFmt", "{0}  ·  Add 1 average {1} → about {2} reaching consumers ({3}%)"),
				Detail,
				Limiter.MachineName.IsEmpty() ? LOCTEXT("LimiterMachineFallback", "machine") : FText::FromString(Limiter.MachineName),
				BalanceRateText(Limiter.MarginalDeliveredPerMinute, Limiter.Item.Form == ECPItemForm::Fluid),
				FText::AsNumber(FMath::RoundToInt(Limiter.MarginalRatio * 100.0f)));
		}
		else if (Limiter.MarginalAction == ECPMarginalActionKind::UpgradeEdge)
		{
			Detail = FText::Format(
				Limiter.EdgeEvidence.bFluid
					? LOCTEXT("LimiterExactPipeMarginalFmt", "{0}  ·  Upgrade this pipe to {1} → about {2} reaching consumers ({3}%)")
					: LOCTEXT("LimiterExactBeltMarginalFmt", "{0}  ·  Upgrade this belt to {1} → about {2} reaching consumers ({3}%)"),
				Detail,
				BalanceRateText(Limiter.MarginalEdgeCapacityPerMinute, Limiter.EdgeEvidence.bFluid),
				BalanceRateText(Limiter.MarginalDeliveredPerMinute, Limiter.Item.Form == ECPItemForm::Fluid),
				FText::AsNumber(FMath::RoundToInt(Limiter.MarginalRatio * 100.0f)));
		}
	}
	else if (Limiter.MarginalRatio >= 0.0f)
	{
		Detail = FText::Format(LOCTEXT("LimiterMarginalFmt", "{0}  ·  one more {1} would reach {2}%"),
			Detail,
			Limiter.MachineName.IsEmpty() ? LOCTEXT("LimiterMachineFallback", "machine") : FText::FromString(Limiter.MachineName),
			FText::AsNumber(FMath::RoundToInt(Limiter.MarginalRatio * 100.0f)));
	}
	if (Limiter.UnknownLinks > 0)
	{
		Detail = FText::Format(LOCTEXT("LimiterUnknownFmt", "{0}  ·  delivery rate unknown on {1} route(s)"),
			Detail, FText::AsNumber(Limiter.UnknownLinks));
	}
	UTextBlock* DetailText = MakeText(WidgetTree, Detail, 10, CPStyle::TextSecondary);
	// Tighter than ColumnNoticeWrap: this row spends ~90px on stripe + indent + two icons.
	DetailText->SetWrapTextAt(CPStyle::ColumnNoticeWrap - 90.0f);
	if (UVerticalBoxSlot* DetailSlot = TextStack->AddChildToVerticalBox(DetailText))
	{
		DetailSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
	}

	if (UVerticalBoxSlot* RowSlot = Target->AddChildToVerticalBox(RowBorder))
	{
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

void UCPPanelWidget::AddPlanRows(const FCPAnalysisResult& Result, UVerticalBox* Target)
{
	if (Result.BuildTiers.Num() == 0 || !Target)
	{
		return;
	}

	int32 StepNumber = 0;
	for (const FCPBuildTier& Tier : Result.BuildTiers)
	{
		// One row per action kind per tier: fixes lead (they gate the tier's builds' feeders).
		struct FAction
		{
			bool bFix;
			const TArray<FCPBuildStep>* Items;
		};
		const FAction Actions[] = { { true, &Tier.Fixes }, { false, &Tier.Items } };
		// Unlocks lead their tier, ahead of its builds. Everything in one tier is mutually
		// independent so the order within it is presentation, not dependency -- but research is
		// quick and it explains why the later steps exist, so reading it first matches how the
		// work is actually done. What MUST hold is that an unlock precedes the parts it makes
		// buildable, and those sit a tier above it.
		const auto EmitUnlocks = [&]()
		{
			for (const FCPUnlockStep& Unlock : Tier.Unlocks)
			{
				StepNumber++;

				UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
				RowBorder->SetBrushColor(CPStyle::Row);
				RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
				UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				RowBorder->SetContent(Box);

				// Violet marks research everywhere in this panel; a build-step accent here would
				// suggest it is another line to construct.
				if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(
					MakeStatusStripe(WidgetTree, CPStyle::StatusViolet)))
				{
					StripeSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
					StripeSlot->SetVerticalAlignment(VAlign_Fill);
				}

				UWrapBox* Content = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass());
				Content->SetInnerSlotPadding(FVector2D(0.0f, 2.0f));
				if (UHorizontalBoxSlot* ContentSlot = Box->AddChildToHorizontalBox(Content))
				{
					ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
					ContentSlot->SetVerticalAlignment(VAlign_Center);
				}

				UTextBlock* Lead = MakeText(WidgetTree,
					FText::Format(LOCTEXT("PlanStepUnlockFmt", "{0})  Unlock:"), FText::AsNumber(StepNumber)),
					12, CPStyle::StatusViolet, true);
				if (UWrapBoxSlot* LeadSlot = Content->AddChildToWrapBox(Lead))
				{
					LeadSlot->SetPadding(FMargin(0.0f, 6.0f, 8.0f, 6.0f));
					LeadSlot->SetVerticalAlignment(VAlign_Center);
				}

				const FText SourceTag = ResearchSourceTag(Unlock.Source);
				UTextBlock* Name = MakeText(WidgetTree,
					SourceTag.IsEmpty()
						? FText::FromString(Unlock.SchematicName)
						: FText::Format(LOCTEXT("PlanUnlockNameFmt", "{0} ({1})"),
							FText::FromString(Unlock.SchematicName), SourceTag),
					12, CPStyle::TextPrimary);
				if (UWrapBoxSlot* NameSlot = Content->AddChildToWrapBox(Name))
				{
					NameSlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
					NameSlot->SetVerticalAlignment(VAlign_Center);
				}

				if (Unlock.Unlocks.Num() > 0)
				{
					TArray<FString> Names;
					for (const FCPItemRef& Item : Unlock.Unlocks)
					{
						Names.AddUnique(Item.Name);
					}
					UTextBlock* Makes = MakeText(WidgetTree,
						FText::Format(LOCTEXT("PlanUnlockMakesFmt", "→ makes {0} buildable"),
							FText::FromString(FString::Join(Names, TEXT(", ")))),
						11, CPStyle::TextSecondary);
					Makes->SetAutoWrapText(true);
					if (UWrapBoxSlot* MakesSlot = Content->AddChildToWrapBox(Makes))
					{
						MakesSlot->SetVerticalAlignment(VAlign_Center);
						MakesSlot->SetFillEmptySpace(true);
					}
				}

				if (UVerticalBoxSlot* RowSlot = Target->AddChildToVerticalBox(RowBorder))
				{
					RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
					RowSlot->SetHorizontalAlignment(HAlign_Fill);
				}
			}
		};

		for (int32 ActionIndex = 0; ActionIndex < UE_ARRAY_COUNT(Actions); ++ActionIndex)
		{
			if (ActionIndex == 1)
			{
				EmitUnlocks();
			}
			const FAction& Action = Actions[ActionIndex];
			if (Action.Items->Num() == 0)
			{
				continue;
			}
			StepNumber++;

			UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			RowBorder->SetBrushColor(CPStyle::Row);
			RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
			UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			RowBorder->SetContent(Box);

			if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(
				MakeStatusStripe(WidgetTree, Action.bFix ? CPStyle::StatusRed : CPStyle::Accent)))
			{
				StripeSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
				StripeSlot->SetVerticalAlignment(VAlign_Fill);
			}

			// The step's content is a WRAP box, not a horizontal one. A tier that builds three
			// parts and names three consumers overruns the panel on a single line, and a
			// horizontal box has no way to answer that — it just draws past the edge. Each
			// icon+name is one atom so a label never separates from its icon mid-flow.
			UWrapBox* Content = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass());
			Content->SetInnerSlotPadding(FVector2D(0.0f, 2.0f));
			if (UHorizontalBoxSlot* ContentSlot = Box->AddChildToHorizontalBox(Content))
			{
				ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				ContentSlot->SetVerticalAlignment(VAlign_Center);
			}

			UTextBlock* Lead = MakeText(WidgetTree,
				FText::Format(Action.bFix
					? LOCTEXT("PlanStepFixFmt", "{0})  Fix:")
					: LOCTEXT("PlanStepBuildFmt", "{0})  Build:"),
					FText::AsNumber(StepNumber)),
				12, Action.bFix ? CPStyle::StatusRed : CPStyle::Accent, true);
			if (UWrapBoxSlot* LeadSlot = Content->AddChildToWrapBox(Lead))
			{
				LeadSlot->SetPadding(FMargin(0.0f, 6.0f, 8.0f, 6.0f));
				LeadSlot->SetVerticalAlignment(VAlign_Center);
			}

			// Consumers are attributed PER ITEM. A single union across the row claimed that every
			// item in the step fed every consumer named after it -- so a terminal objective part
			// that merely shares a tier (it depends on nothing left to build) read as an
			// ingredient of the others. Each item now carries its own "-> for ..." or nothing.
			for (int32 ItemIndex = 0; ItemIndex < Action.Items->Num(); ++ItemIndex)
			{
				const FCPBuildStep& Step = (*Action.Items)[ItemIndex];
				UHorizontalBox* Entry = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
				if (ItemIndex > 0)
				{
					UTextBlock* Plus = MakeText(WidgetTree, FText::FromString(TEXT("+")), 12, CPStyle::TextSecondary);
					if (UHorizontalBoxSlot* PlusSlot = Entry->AddChildToHorizontalBox(Plus))
					{
						PlusSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
						PlusSlot->SetVerticalAlignment(VAlign_Center);
					}
				}
				AddItemIcon(Entry, Step.Item, 18.0f);
				UTextBlock* Name = MakeText(WidgetTree, FText::FromString(Step.Item.Name), 12, CPStyle::TextPrimary);
				if (UHorizontalBoxSlot* NameSlot = Entry->AddChildToHorizontalBox(Name))
				{
					NameSlot->SetVerticalAlignment(VAlign_Center);
				}
				// Consumers inside the same step are internal to it; naming them reads as circular.
				TArray<FString> Consumers;
				for (const FCPItemRef& Consumer : Step.NeededFor)
				{
					const bool bInThisStep = Action.Items->ContainsByPredicate(
						[&Consumer](const FCPBuildStep& Sibling) { return Sibling.Item.Name == Consumer.Name; });
					if (!bInThisStep)
					{
						Consumers.AddUnique(Consumer.Name);
					}
				}
				// Objectives the step satisfies directly. Labelled here rather than in the engine,
				// which stores bare names and has no business knowing what a player calls them.
				for (const FString& ObjectiveName : Step.NeededForObjectives)
				{
					const FCPObjectiveReport* Objective = Result.Objectives.FindByPredicate(
						[&ObjectiveName](const FCPObjectiveReport& Candidate)
						{ return Candidate.ObjectiveName == ObjectiveName; });
					const bool bMilestone = !Objective || Objective->Kind == ECPObjectiveKind::Milestone;
					Consumers.AddUnique(FText::Format(bMilestone
						? LOCTEXT("PlanForMilestoneFmt", "the {0} milestone")
						: LOCTEXT("PlanForPhaseFmt", "the {0} elevator phase"),
						FText::FromString(ObjectiveName)).ToString());
				}
				if (Consumers.Num() > 0)
				{
					UTextBlock* For = MakeText(WidgetTree,
						FText::Format(LOCTEXT("PlanItemForFmt", "→ for {0}"),
							FText::FromString(FString::Join(Consumers, TEXT(", ")))),
						11, CPStyle::TextSecondary);
					if (UHorizontalBoxSlot* ForSlot = Entry->AddChildToHorizontalBox(For))
					{
						ForSlot->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));
						ForSlot->SetVerticalAlignment(VAlign_Center);
					}
				}
				if (UWrapBoxSlot* EntrySlot = Content->AddChildToWrapBox(Entry))
				{
					EntrySlot->SetPadding(FMargin(0.0f, 0.0f, 12.0f, 0.0f));
					EntrySlot->SetVerticalAlignment(VAlign_Center);
				}
			}

			if (UVerticalBoxSlot* RowSlot = Target->AddChildToVerticalBox(RowBorder))
			{
				RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
				RowSlot->SetHorizontalAlignment(HAlign_Fill);
			}
		}

	}
}

void UCPPanelWidget::AddPlanExclusionRows(const FCPAnalysisResult& Result,
	const TMap<FString, FString>& ResearchByItem, UVerticalBox* Target)
{
	if (!Target)
	{
		return;
	}

	// Objective parts the plan CANNOT cover, said plainly and separately. A locked recipe has no
	// build order to give, so it is absent from the numbered steps -- and silence there reads as
	// "handled". Naming it inside the plan's own sentence was worse: it implied the unlock was
	// part of, or the next step after, a sequence it has nothing to do with.
	// Parts the plan now sequences (their unlock became a step) must NOT also appear here — that
	// would restate as "separate" the very thing we just proved belongs in the chain.
	TSet<FString> SequencedByPlan;
	for (const FCPBuildTier& Tier : Result.BuildTiers)
	{
		for (const FCPBuildStep& Step : Tier.Items)
		{
			SequencedByPlan.Add(Step.Item.Name);
		}
	}

	for (const FCPObjectiveReport& Objective : Result.Objectives)
	{
		for (const FCPPartReport& Part : Objective.Parts)
		{
			if (Part.Status != ECPNodeStatus::RecipeLocked || SequencedByPlan.Contains(Part.Item.Name))
			{
				continue;
			}

			UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
			RowBorder->SetBrushColor(CPStyle::Row);
			RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
			UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			RowBorder->SetContent(Box);

			// Violet, matching the research marker the PATH tab already uses for this same part —
			// not the accent colour the numbered build steps wear, because this is not one.
			if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(
				MakeStatusStripe(WidgetTree, CPStyle::StatusViolet)))
			{
				StripeSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
				StripeSlot->SetVerticalAlignment(VAlign_Fill);
			}

			UWrapBox* Content = WidgetTree->ConstructWidget<UWrapBox>(UWrapBox::StaticClass());
			Content->SetInnerSlotPadding(FVector2D(0.0f, 2.0f));
			if (UHorizontalBoxSlot* ContentSlot = Box->AddChildToHorizontalBox(Content))
			{
				ContentSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
				ContentSlot->SetVerticalAlignment(VAlign_Center);
			}

			UTextBlock* Lead = MakeText(WidgetTree,
				LOCTEXT("PlanExcludedLead", "Separate track:"), 12, CPStyle::StatusViolet, true);
			if (UWrapBoxSlot* LeadSlot = Content->AddChildToWrapBox(Lead))
			{
				LeadSlot->SetPadding(FMargin(0.0f, 6.0f, 8.0f, 6.0f));
				LeadSlot->SetVerticalAlignment(VAlign_Center);
			}

			UHorizontalBox* Entry = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
			AddItemIcon(Entry, Part.Item, 18.0f);
			UTextBlock* Name = MakeText(WidgetTree, FText::FromString(Part.Item.Name), 12, CPStyle::TextPrimary);
			if (UHorizontalBoxSlot* NameSlot = Entry->AddChildToHorizontalBox(Name))
			{
				NameSlot->SetVerticalAlignment(VAlign_Center);
			}
			if (UWrapBoxSlot* EntrySlot = Content->AddChildToWrapBox(Entry))
			{
				EntrySlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
				EntrySlot->SetVerticalAlignment(VAlign_Center);
			}

			const FString* Research = ResearchByItem.Find(Part.Item.Name);
			UTextBlock* Why = MakeText(WidgetTree,
				Research
					? FText::Format(LOCTEXT("PlanExcludedWhyFmt",
						"is not in this plan — its recipe unlocks with {0}"), FText::FromString(*Research))
					: LOCTEXT("PlanExcludedWhyUnknown",
						"is not in this plan — its recipe is not unlocked yet"),
				11, CPStyle::TextSecondary);
			Why->SetAutoWrapText(true);
			if (UWrapBoxSlot* WhySlot = Content->AddChildToWrapBox(Why))
			{
				WhySlot->SetPadding(FMargin(6.0f, 0.0f, 0.0f, 0.0f));
				WhySlot->SetVerticalAlignment(VAlign_Center);
				WhySlot->SetFillEmptySpace(true);
			}

			if (UVerticalBoxSlot* RowSlot = Target->AddChildToVerticalBox(RowBorder))
			{
				RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
				RowSlot->SetHorizontalAlignment(HAlign_Fill);
			}
		}
	}
}

void UCPPanelWidget::AddResearchPlanRows(const FCPResearchGapReport& Plan, UVerticalBox* Target)
{
	if (!Target || Plan.CostParts.Num() == 0)
	{
		return;
	}

	// Section lead-in: which research this payment plan belongs to.
	{
		UBorder* HeadBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		HeadBorder->SetBrushColor(CPStyle::RowNested);
		HeadBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		HeadBorder->SetContent(Box);
		if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(MakeStatusStripe(WidgetTree, CPStyle::StatusViolet)))
		{
			StripeSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
			StripeSlot->SetVerticalAlignment(VAlign_Fill);
		}
		UTextBlock* Head = MakeText(WidgetTree,
			FText::Format(LOCTEXT("ResearchPlanHeadFmt", "To research {0}  ({1}):"),
				FText::FromString(Plan.Gap.UnlockSchematicName), ResearchSourceTag(Plan.Gap.UnlockSource)),
			11, CPStyle::StatusViolet, true);
		if (UHorizontalBoxSlot* HeadSlot = Box->AddChildToHorizontalBox(Head))
		{
			HeadSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			HeadSlot->SetVerticalAlignment(VAlign_Center);
			HeadSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));
		}
		if (UVerticalBoxSlot* RowSlot = Target->AddChildToVerticalBox(HeadBorder))
		{
			RowSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 2.0f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}

	for (const FCPPartReport& Cost : Plan.CostParts)
	{
		UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RowBorder->SetBrushColor(CPStyle::RowNested);
		RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RowBorder->SetContent(Box);

		if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(MakeStatusStripe(WidgetTree, CPStyle::StatusColor(Cost.Status))))
		{
			StripeSlot->SetVerticalAlignment(VAlign_Fill);
		}
		USizeBox* Indent = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Indent->SetWidthOverride(CPStyle::IndentPerDepth);
		Box->AddChildToHorizontalBox(Indent);

		AddItemIcon(Box, Cost.Item, 18.0f);

		UVerticalBox* TextStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		if (UHorizontalBoxSlot* TextSlot = Box->AddChildToHorizontalBox(TextStack))
		{
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));
		}
		TextStack->AddChildToVerticalBox(MakeText(WidgetTree, FText::FromString(Cost.Item.Name), 12, CPStyle::TextPrimary));

		UTextBlock* Detail = MakeText(WidgetTree,
			FText::Format(LOCTEXT("ResearchCostLedgerFmt", "{0} needed  ·  {1} banked  ·  {2}"),
				FText::AsNumber(Cost.Remaining), FText::AsNumber(Cost.Banked), StatusDetail(Cost)),
			10, CPStyle::TextSecondary);
		Detail->SetWrapTextAt(CPStyle::ColumnNoticeWrap);
		if (UVerticalBoxSlot* DetailSlot = TextStack->AddChildToVerticalBox(Detail))
		{
			DetailSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
		}

		if (UVerticalBoxSlot* RowSlot = Target->AddChildToVerticalBox(RowBorder))
		{
			RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

void UCPPanelWidget::AddPlannedRows(const TArray<FCPPlannedNode>& Chain, UVerticalBox* Target)
{
	if (Chain.Num() == 0 || !Target)
	{
		return;
	}

	for (const FCPPlannedNode& Node : Chain)
	{
		UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
		RowBorder->SetBrushColor(CPStyle::RowNested);
		RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
		UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
		RowBorder->SetContent(Box);

		// Planned = teal accent, deliberately outside the status palette: this is a proposal,
		// not a state of the real factory. Grafts/raws/locked keep their semantic colours.
		FLinearColor Colour;
		FText Detail;
		switch (Node.Kind)
		{
		case ECPPlanNodeKind::PlannedLine:
			Colour = CPStyle::Accent;
			Detail = Node.MachineName.IsEmpty()
				? LOCTEXT("PlanLine", "No line — build one")
				: FText::Format(LOCTEXT("PlanLineFmt", "No line — build: {0}"), FText::FromString(Node.MachineName));
			if (Node.bAlternatesExist)
			{
				Detail = FText::Format(LOCTEXT("PlanAltFmt", "{0}  ·  Alt recipes exist"), Detail);
			}
			break;
		case ECPPlanNodeKind::ExistingProduction:
			Colour = Node.bSufficiencyKnown
				? (Node.SufficiencyRatio >= 1.0f ? CPStyle::StatusGreen : CPStyle::StatusAmber)
				: CPStyle::StatusNeutral;
			// A stopped line is a graft in name only — never say "already produced" for a
			// machine that isn't running.
			if (Node.bLineStopped)
			{
				Colour = CPStyle::StatusRed;
				Detail = Node.MachineName.IsEmpty()
					? LOCTEXT("PlanGraftStopped", "Line exists but is STOPPED — fix it")
					: FText::Format(LOCTEXT("PlanGraftStoppedFmt", "Line exists but is STOPPED — fix the {0}"), FText::FromString(Node.MachineName));
			}
			else
			{
				Detail = Node.MachineName.IsEmpty()
					? LOCTEXT("PlanGraft", "Already produced")
					: FText::Format(LOCTEXT("PlanGraftFmt", "Already produced — {0}"), FText::FromString(Node.MachineName));
			}
			if (Node.bSufficiencyKnown)
			{
				Detail = FText::Format(LOCTEXT("PlanGraftRatioFmt",
					"{0}  ·  Spare {1}/min, needs {2}/min ({3}%)"), Detail,
					FText::AsNumber(Node.AvailableHeadroomPerMinute), FText::AsNumber(Node.RequiredPerMinute),
					FText::AsNumber(FMath::RoundToInt(Node.SufficiencyRatio * 100.0f)));
			}
			else
			{
				Detail = FText::Format(LOCTEXT("PlanGraftUnknownFmt",
					"{0}  ·  Spare capacity unavailable — one or more routes could not be fully measured"), Detail);
			}
			break;
		case ECPPlanNodeKind::RawResource:
			Colour = CPStyle::StatusNeutral;
			Detail = LOCTEXT("PlanRaw", "Raw resource — extract it");
			break;
		case ECPPlanNodeKind::ResearchLocked:
			if (!Node.ByproductOfFuel.IsEmpty())
			{
				// Not a research problem at all: spent fuel only exists by burning.
				Colour = CPStyle::StatusNeutral;
				Detail = FText::Format(LOCTEXT("PlanSpentFuelFmt", "Spent fuel — burn {0} in a Nuclear Power Plant"),
					FText::FromString(Node.ByproductOfFuel));
			}
			else
			{
				Colour = CPStyle::StatusViolet;
				Detail = LOCTEXT("PlanLocked", "Recipe not researched");
			}
			break;
		default:
			Colour = CPStyle::TextSecondary;
			Detail = LOCTEXT("PlanUnknown", "No known source");
			break;
		}
		if (Node.bTruncated)
		{
			Detail = FText::Format(LOCTEXT("PlanTruncFmt", "{0}  ·  more not shown"), Detail);
		}

		if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(MakeStatusStripe(WidgetTree, Colour)))
		{
			StripeSlot->SetVerticalAlignment(VAlign_Fill);
		}

		// One extra indent level relative to the part row; planned depth nests below that.
		USizeBox* Indent = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass());
		Indent->SetWidthOverride(CPStyle::IndentPerDepth * (Node.Depth + 1));
		Box->AddChildToHorizontalBox(Indent);

		AddItemIcon(Box, Node.Item, 18.0f);

		// Match the objective-row hierarchy: item name first, status/rate directly beneath it.
		// Keeping both texts in one fill-width stack prevents the two half-width columns from
		// drawing long sufficiency verdicts across one another.
		UVerticalBox* TextStack = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass());
		if (UHorizontalBoxSlot* TextSlot = Box->AddChildToHorizontalBox(TextStack))
		{
			TextSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
			TextSlot->SetVerticalAlignment(VAlign_Center);
			TextSlot->SetPadding(FMargin(0.0f, 5.0f, 0.0f, 5.0f));
		}
		TextStack->AddChildToVerticalBox(
			MakeText(WidgetTree, FText::FromString(Node.Item.Name), 12, CPStyle::TextPrimary));

		UTextBlock* DetailText = MakeText(WidgetTree, Detail, 10, CPStyle::TextSecondary);
		const float DetailWrap = FMath::Max(180.0f,
			CPStyle::ColumnNoticeWrap - CPStyle::IndentPerDepth * (Node.Depth + 1) - 28.0f);
		DetailText->SetWrapTextAt(DetailWrap);
		if (UVerticalBoxSlot* DetailSlot = TextStack->AddChildToVerticalBox(DetailText))
		{
			DetailSlot->SetPadding(FMargin(0.0f, 2.0f, 0.0f, 0.0f));
		}

		if (!Node.RecipeName.IsEmpty() && Node.Kind == ECPPlanNodeKind::PlannedLine)
		{
			RowBorder->SetToolTipText(FText::Format(LOCTEXT("PlanTipFmt", "Assumed recipe: {0}"), FText::FromString(Node.RecipeName)));
		}

		if (UVerticalBoxSlot* RowSlot = Target->AddChildToVerticalBox(RowBorder))
		{
			RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
			RowSlot->SetHorizontalAlignment(HAlign_Fill);
		}
	}
}

FEventReply UCPChainToggle::OnRowMouseDown(FGeometry InGeometry, const FPointerEvent& InMouseEvent)
{
	if (Target)
	{
		const bool bNowVisible = Target->GetVisibility() == ESlateVisibility::Collapsed;
		Target->SetVisibility(bNowVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
		if (Chevron)
		{
			// ▼ expanded, rotated to point right when collapsed.
			Chevron->SetRenderTransformAngle(bNowVisible ? 0.0f : -90.0f);
		}
	}
	return FEventReply(true);
}

void UCPPanelWidget::AddResearchGapRow(const FCPResearchGap& Gap)
{
	UBorder* RowBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass());
	RowBorder->SetBrushColor(CPStyle::RowNested);
	RowBorder->SetPadding(FMargin(0.0f, 0.0f, 10.0f, 0.0f));
	UHorizontalBox* Box = WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass());
	RowBorder->SetContent(Box);

	if (UHorizontalBoxSlot* StripeSlot = Box->AddChildToHorizontalBox(MakeStatusStripe(WidgetTree, CPStyle::StatusViolet)))
	{
		StripeSlot->SetPadding(FMargin(0.0f, 0.0f, 8.0f, 0.0f));
		StripeSlot->SetVerticalAlignment(VAlign_Fill);
	}

	if (UTexture2D* ResearchTex = LoadObject<UTexture2D>(nullptr, CPStyle::IconResearch))
	{
		RootedIconTextures.AddUnique(ResearchTex);
		UImage* ResearchIcon = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass());
		ResearchIcon->SetBrushFromTexture(ResearchTex, false);
		ResearchIcon->SetDesiredSizeOverride(FVector2D(14.0f, 14.0f));
		ResearchIcon->SetColorAndOpacity(CPStyle::StatusViolet);
		if (UHorizontalBoxSlot* IconSlot = Box->AddChildToHorizontalBox(ResearchIcon))
		{
			IconSlot->SetPadding(FMargin(0.0f, 0.0f, 6.0f, 0.0f));
			IconSlot->SetVerticalAlignment(VAlign_Center);
		}
	}

	AddItemIcon(Box, Gap.NeededItem, 18.0f);

	UTextBlock* Name = MakeText(WidgetTree, FText::FromString(Gap.NeededItem.Name), 12, CPStyle::TextPrimary);
	if (UHorizontalBoxSlot* NameSlot = Box->AddChildToHorizontalBox(Name))
	{
		NameSlot->SetSize(FSlateChildSize(ESlateSizeRule::Fill));
		NameSlot->SetVerticalAlignment(VAlign_Center);
		NameSlot->SetPadding(FMargin(0.0f, 4.0f, 0.0f, 4.0f));
	}

	const FText SourceTag = ResearchSourceTag(Gap.UnlockSource);
	const FText Detail = !Gap.ByproductOfFuel.IsEmpty()
		? FText::Format(LOCTEXT("GapSpentFuelFmt", "No recipe by design — spent fuel: burn {0} in a Nuclear Power Plant"), FText::FromString(Gap.ByproductOfFuel))
		: Gap.UnlockSchematicName.IsEmpty()
			? LOCTEXT("GapNoUnlock", "Recipe locked — no purchasable research yet")
			: SourceTag.IsEmpty()
				? FText::Format(LOCTEXT("GapUnlockFmt", "Recipe locked — research: {0}"), FText::FromString(Gap.UnlockSchematicName))
				: FText::Format(LOCTEXT("GapUnlockSrcFmt", "Recipe locked — research: {0} ({1})"), FText::FromString(Gap.UnlockSchematicName), SourceTag);
	UTextBlock* DetailText = MakeText(WidgetTree, Detail, 10, CPStyle::TextSecondary);
	if (UHorizontalBoxSlot* DetailSlot = Box->AddChildToHorizontalBox(DetailText))
	{
		DetailSlot->SetVerticalAlignment(VAlign_Center);
	}

	if (UVerticalBoxSlot* RowSlot = CurrentList->AddChildToVerticalBox(RowBorder))
	{
		RowSlot->SetPadding(FMargin(0.0f, 0.0f, 0.0f, 2.0f));
		RowSlot->SetHorizontalAlignment(HAlign_Fill);
	}
}

FText UCPPanelWidget::MakeVerdict(const FCPAnalysisResult& Result) const
{
	// Build-order verdict: when planned lines exist, the whole picture is the aggregated
	// tier sequence — build the bottom tier first (its inputs already exist), each later
	// tier waits on the one before it. Far more actionable than naming one blocked part.
	if (Result.BuildTiers.Num() > 0)
	{
		// Which objective parts this plan actually hands you. Without naming them the bar counts
		// steps but never says what they are FOR, so the reader has to reverse-engineer the goal
		// from the last row.
		TSet<FString> PlannedItemNames;
		for (const FCPBuildTier& Tier : Result.BuildTiers)
		{
			for (const FCPBuildStep& Step : Tier.Items)
			{
				PlannedItemNames.Add(Step.Item.Name);
			}
			for (const FCPBuildStep& Step : Tier.Fixes)
			{
				PlannedItemNames.Add(Step.Item.Name);
			}
		}
		TArray<FString> DeliveredByPlan;
		const FCPPartReport* FirstFactoryBlocked = nullptr;
		for (const FCPObjectiveReport& Objective : Result.Objectives)
		{
			for (const FCPPartReport& Part : Objective.Parts)
			{
				if (ClassifyPart(Part) == EPartConcern::Fault && !FirstFactoryBlocked)
				{
					FirstFactoryBlocked = &Part;
				}
				if (PlannedItemNames.Contains(Part.Item.Name) && Part.Status != ECPNodeStatus::Delivered)
				{
					DeliveredByPlan.AddUnique(Part.Item.Name);
				}
			}
		}

		// The band carries a slim status headline; the numbered sequence renders directly
		// beneath it in the band's own scrolling list.
		int32 TotalSteps = 0;
		for (const FCPBuildTier& Tier : Result.BuildTiers)
		{
			TotalSteps += (Tier.Fixes.Num() > 0 ? 1 : 0) + (Tier.Items.Num() > 0 ? 1 : 0) + Tier.Unlocks.Num();
		}

		// The bar describes THIS PLAN and nothing else. Research-locked parts are excluded from
		// the plan by construction, so mentioning them here — appended after the step count with
		// a separator — read as a further step in the same sequence, when they are neither in it
		// nor related to it. They now get their own row beneath the plan, which is where a thing
		// the plan does not cover belongs.
		// BLOCKED is spent only on a factory fault the player can walk over and fix.
		FText Verdict = DeliveredByPlan.Num() > 0
			? FText::Format(FirstFactoryBlocked
				? LOCTEXT("VerdictPlanBlockedDeliverFmt", "BLOCKED — {0} step(s) to deliver {1}")
				: LOCTEXT("VerdictPlanDeliverFmt", "NEXT STEPS — {0} step(s) to deliver {1}"),
				FText::AsNumber(TotalSteps), FText::FromString(FString::Join(DeliveredByPlan, TEXT(", "))))
			: FText::Format(FirstFactoryBlocked
				? LOCTEXT("VerdictPlanBlockedFmt", "BLOCKED — the plan ({0} step(s)):")
				: LOCTEXT("VerdictPlanHeadFmt", "NEXT STEPS — the plan ({0} step(s)):"),
				FText::AsNumber(TotalSteps));
		if (FirstFactoryBlocked)
		{
			Verdict = FText::Format(LOCTEXT("VerdictPlanFaultFmt", "{0}  ·  {1}: {2}"),
				Verdict, FText::FromString(FirstFactoryBlocked->Blocker.Item.Name),
				BlockerReasonText(FirstFactoryBlocked->Blocker));
		}
		return Verdict;
	}

	// No planned lines: fall back to naming the first part that needs attention, faults first so
	// a real malfunction is never buried under a to-do.
	for (const EPartConcern Wanted : { EPartConcern::Fault, EPartConcern::Planned })
	{
		for (const FCPObjectiveReport& Objective : Result.Objectives)
		{
			for (const FCPPartReport& Part : Objective.Parts)
			{
				if (Part.Blocker.Reason == ECPBlockerReason::None || ClassifyPart(Part) != Wanted)
				{
					continue;
				}
				return FText::Format(Wanted == EPartConcern::Fault
					? LOCTEXT("VerdictBlockedFmt", "BLOCKED — {0}: {1}.")
					: LOCTEXT("VerdictNextFmt", "NEXT — {0}: {1}."),
					FText::FromString(Part.Blocker.Item.Name), BlockerReasonText(Part.Blocker));
			}
		}
	}
	float BestEta = -1.0f;
	FString BestItem;
	bool bAllReady = true;
	for (const FCPObjectiveReport& Objective : Result.Objectives)
	{
		for (const FCPPartReport& Part : Objective.Parts)
		{
			bAllReady &= Part.Status == ECPNodeStatus::ReadyToDeliver || Part.Status == ECPNodeStatus::Delivered;
			if (Part.EtaMinutes >= 0.0f && (BestEta < 0.0f || Part.EtaMinutes > BestEta))
			{
				BestEta = Part.EtaMinutes;
				BestItem = Part.Item.Name;
			}
		}
	}
	if (bAllReady && Result.Objectives.Num() > 0)
	{
		return LOCTEXT("VerdictReady", "READY — every remaining part is banked. Delivery is the only step left.");
	}
	if (BestEta >= 0.0f)
	{
		return FText::Format(LOCTEXT("VerdictEtaFmt", "ON TRACK — ~{0} min of production remain ({1} is the long pole)."),
			FText::AsNumber(FMath::RoundToInt(BestEta)), FText::FromString(BestItem));
	}
	return LOCTEXT("VerdictNone", "No outstanding objectives.");
}

void UCPPanelWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	if (Spinners.Num() == 0)
	{
		return;
	}
	SpinnerAngle = FMath::Fmod(SpinnerAngle + InDeltaTime * 180.0f, 360.0f); // half turn per second
	for (UWidget* Spinner : Spinners)
	{
		if (Spinner)
		{
			Spinner->SetRenderTransformAngle(SpinnerAngle);
		}
	}
}

FReply UCPPanelWidget::NativeOnKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnCloseRequested.Broadcast();
		return FReply::Handled();
	}
	return Super::NativeOnKeyDown(InGeometry, InKeyEvent);
}

// Preview (tunnel) phase: catches Esc even when a child (scroll list) holds focus, so close
// always works — the click-through save-menu soft-lock must stay impossible.
FReply UCPPanelWidget::NativeOnPreviewKeyDown(const FGeometry& InGeometry, const FKeyEvent& InKeyEvent)
{
	if (InKeyEvent.GetKey() == EKeys::Escape)
	{
		OnCloseRequested.Broadcast();
		return FReply::Handled();
	}
	return Super::NativeOnPreviewKeyDown(InGeometry, InKeyEvent);
}

FReply UCPPanelWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
	return FReply::Handled();
}

FReply UCPPanelWidget::NativeOnMouseButtonUp(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseButtonUp(InGeometry, InMouseEvent);
	return FReply::Handled();
}

FReply UCPPanelWidget::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
	return FReply::Handled();
}

FReply UCPPanelWidget::NativeOnMouseWheel(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseWheel(InGeometry, InMouseEvent);
	return FReply::Handled();
}


#undef LOCTEXT_NAMESPACE
