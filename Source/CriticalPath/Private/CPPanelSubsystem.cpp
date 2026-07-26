// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPPanelSubsystem.h"

#include "Blueprint/UserWidget.h"
#include "Async/Async.h"
#include "CPAnalysis.h"
#include "CPFlowAnalysis.h"
#include "CPFlowCapture.h"
#include "CPFlowSolver.h"
#include "CPInputSubsystem.h"
#include "CPPanelWidget.h"
#include "CPReportMarkdown.h"
#include "CPSnapshot.h"
#include "CriticalPath.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "FGPlayerController.h"

#define LOCTEXT_NAMESPACE "CriticalPath"

bool UCPPanelSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	// Local players only — mirrors the input subsystem; never on dedicated servers.
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->GetNetMode() != NM_DedicatedServer && World->IsGameWorld();
}

void UCPPanelSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (UCPInputSubsystem* Input = InWorld.GetSubsystem<UCPInputSubsystem>())
	{
		ToggleHandle = Input->OnTogglePanel.AddUObject(this, &UCPPanelSubsystem::HandleTogglePanel);
	}
	else
	{
		UE_LOG(LogCriticalPath, Warning, TEXT("PanelSubsystem: input subsystem missing — F10 will not open the panel"));
	}
}

void UCPPanelSubsystem::Deinitialize()
{
	++RefreshGeneration; // invalidate any pure solve still finishing on the worker pool
	if (UWorld* World = GetWorld())
	{
		if (UCPInputSubsystem* Input = World->GetSubsystem<UCPInputSubsystem>())
		{
			Input->OnTogglePanel.Remove(ToggleHandle);
		}
	}
	Panel = nullptr;
	Super::Deinitialize();
}

bool UCPPanelSubsystem::IsPanelVisible() const
{
	return Panel && Panel->IsInViewport();
}

void UCPPanelSubsystem::HandleTogglePanel()
{
	if (IsPanelVisible())
	{
		HidePanel();
	}
	else
	{
		ShowPanel();
	}
}

void UCPPanelSubsystem::ShowPanel()
{
	UWorld* World = GetWorld();
	APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
	if (!Controller)
	{
		return;
	}

	if (!Panel)
	{
		Panel = CreateWidget<UCPPanelWidget>(Controller, UCPPanelWidget::StaticClass());
		if (!Panel)
		{
			UE_LOG(LogCriticalPath, Warning, TEXT("PanelSubsystem: failed to create panel widget"));
			return;
		}
		Panel->OnCloseRequested.AddUObject(this, &UCPPanelSubsystem::HidePanel);
		Panel->OnRefreshRequested.AddUObject(this, &UCPPanelSubsystem::RefreshReport);
	}

	// Fullscreen add: the widget's own root canvas anchors the frame top-center (the viewport
	// slot's anchor setters proved unreliable). ZOrder above the HUD, below the game's menus.
	// Must precede RefreshReport: the widget tree is built on first add, SetReport needs it.
	Panel->AddToViewport(50);

	RefreshReport();

	// Game-and-UI: the player keeps movement keys but gains a cursor for the scroll list.
	FInputModeGameAndUI InputMode;
	InputMode.SetWidgetToFocus(Panel->TakeWidget());
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	InputMode.SetHideCursorDuringCapture(false);
	Controller->SetInputMode(InputMode);
	Controller->SetShowMouseCursor(true);

	UE_LOG(LogCriticalPath, Display, TEXT("Panel opened"));
}

void UCPPanelSubsystem::HidePanel()
{
	if (Panel)
	{
		Panel->RemoveFromParent();
	}
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* Controller = World->GetFirstPlayerController())
		{
			Controller->SetInputMode(FInputModeGameOnly());
			Controller->SetShowMouseCursor(false);
		}
	}
	UE_LOG(LogCriticalPath, Display, TEXT("Panel closed"));
}

void UCPPanelSubsystem::RefreshReport()
{
	const uint64 ThisRefresh = ++RefreshGeneration;
	UWorld* World = GetWorld();
	if (!World || !Panel)
	{
		return;
	}

	FCPSnapshotParams Params;
	Params.PlayerContext = Cast<AFGPlayerController>(World->GetFirstPlayerController());

	FCPFactorySnapshot Snapshot;
	FString Error;
	if (FCPSnapshotCollector::Collect(World, Params, Snapshot, Error))
	{
		// Remote client: inventories, station back-pointers, machine potentials and fluid boxes
		// are all server-side, so a capture here reads empty/stale values that analysis cannot
		// distinguish from a genuinely broken factory. Refuse BEFORE analysing, solving, caching
		// or exporting — a wrong report on disk is worse than no report (TransportModel.md S6).
		if (!Snapshot.bAuthoritative)
		{
			Panel->SetUnavailable(
				LOCTEXT("ClientUnavailableHeadline", "Production data is not available on this client"),
				LOCTEXT("ClientUnavailableDetail",
					"Machine contents, storage levels and vehicle stations are tracked by the server, "
					"so Critical Path cannot measure this factory from a joined game. Open the panel "
					"on the host to get an accurate report."));
			UE_LOG(LogCriticalPath, Log,
				TEXT("PanelSubsystem: non-authoritative (%s) — refusing to present a report"), *Snapshot.NetMode);
			return;
		}

		CachedResult = FCPAnalysis::Analyze(Snapshot);
		bool bBalancePending = false;

		FCPFlowGraph FlowGraph;
		FString FlowError;
		if (FCPFlowCapture::Capture(World, FCPFlowCaptureParams(), FlowGraph, FlowError))
		{
			bBalancePending = true;
			FCPAnalysisResult SolverResult = CachedResult;
			// Capture is game-thread-only; the graph and solver results are plain data. Solve off
			// thread so opening the panel does not stall a megabase for several seconds.
			const TWeakObjectPtr<UCPPanelSubsystem> WeakThis(this);
			Async(EAsyncExecution::ThreadPool, [WeakThis, ThisRefresh, Graph = MoveTemp(FlowGraph),
				Snapshot = MoveTemp(Snapshot), SolverResult = MoveTemp(SolverResult)]() mutable
			{
				FCPFlowSolveResult CurrentSolve;
				FCPFlowSolver::Solve(Graph, FCPFlowSolveParams(), CurrentSolve);
				FCPFlowSolveParams DesignParams;
				DesignParams.bUseDesignRates = true;
				FCPFlowSolveResult DesignSolve;
				FCPFlowSolver::Solve(Graph, DesignParams, DesignSolve);
				FCPFlowAnalysis::BuildItemBalances(Graph, CurrentSolve, DesignSolve, SolverResult.ItemBalances);
				FCPFlowAnalysis::BuildSolverSufficiency(SolverResult.ItemBalances, SolverResult.Sufficiency);
				FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, SolverResult.ItemBalances, SolverResult);
				FCPFlowAnalysis::ApplySolverPlanInterpretation(SolverResult.ItemBalances, SolverResult);

				AsyncTask(ENamedThreads::GameThread, [WeakThis, ThisRefresh,
					SolverResult = MoveTemp(SolverResult)]() mutable
				{
					if (UCPPanelSubsystem* Self = WeakThis.Get())
					{
						if (Self->RefreshGeneration != ThisRefresh)
						{
							return;
						}
						Self->CachedResult = MoveTemp(SolverResult);
						if (Self->Panel && Self->Panel->IsInViewport())
						{
							Self->Panel->SetReport(Self->CachedResult, LOCTEXT("AgeJustNowFlow", "just now"));
						}
					}
				});
			});
		}
		else
		{
			UE_LOG(LogCriticalPath, Warning, TEXT("PanelSubsystem: flow capture failed (%s)"), *FlowError);
		}
		CachedAtWorldSeconds = World->GetTimeSeconds();
		bHasCachedResult = true;
		Panel->SetReport(CachedResult, LOCTEXT("AgeJustNow", "just now"), bBalancePending);

		// Export the fresh report to Saved/CriticalPath/ — a shareable artifact of what the
		// panel showed (bug reports, planning outside the game). Overwritten each refresh.
		const FString ExportPath = FPaths::ProjectSavedDir() / TEXT("CriticalPath") / TEXT("CriticalPath_Report.md");
		const FString Markdown = FCPReportMarkdown::Build(CachedResult, FDateTime::Now().ToString(TEXT("%Y-%m-%d %H:%M:%S")));
		if (!FFileHelper::SaveStringToFile(Markdown, *ExportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
		{
			UE_LOG(LogCriticalPath, Warning, TEXT("PanelSubsystem: report export failed (%s)"), *ExportPath);
		}
		return;
	}

	UE_LOG(LogCriticalPath, Warning, TEXT("PanelSubsystem: collection failed (%s)%s"), *Error,
		bHasCachedResult ? TEXT(" — showing cached result") : TEXT(""));

	if (bHasCachedResult)
	{
		const int32 AgeSeconds = FMath::Max(0, FMath::RoundToInt(World->GetTimeSeconds() - CachedAtWorldSeconds));
		Panel->SetReport(CachedResult, FText::Format(LOCTEXT("AgeStaleFmt", "STALE — {0}s old (refresh failed)"), FText::AsNumber(AgeSeconds)));
	}
	else
	{
		Panel->SetReport(FCPAnalysisResult(), FText::Format(LOCTEXT("AgeErrorFmt", "no data — {0}"), FText::FromString(Error)));
	}
}

#undef LOCTEXT_NAMESPACE
