// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPWorldAnalysis.h"

#include "CPAnalysis.h"
#include "CPFlowAnalysis.h"
#include "CPFlowCapture.h"
#include "CPFlowSolver.h"

bool FCPWorldAnalysis::Analyze(
	UObject* WorldContext,
	const FCPSnapshotParams& SnapshotParams,
	FCPAnalysisResult& OutResult,
	FString& OutError,
	FCPWorldAnalysisArtifacts* OutArtifacts)
{
	check(IsInGameThread());
	OutError.Reset();

	const double StartSeconds = FPlatformTime::Seconds();
	FCPFactorySnapshot Snapshot;
	if (!FCPSnapshotCollector::Collect(WorldContext, SnapshotParams, Snapshot, OutError))
	{
		return false;
	}
	const double CollectMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

	OutResult = FCPAnalysis::Analyze(Snapshot);

	// PARTIAL SUCCESS IS THE CONTRACT. Snapshot capture is required — without it there is no
	// report at all. Flow enrichment is optional: if the graph cannot be captured, the caller
	// still gets objectives, the owned ledger, machine-state facts and research gaps, with
	// FlowEvidence saying what is missing. Returning false here would throw away everything
	// useful because one optional layer failed.
	const double FlowStartSeconds = FPlatformTime::Seconds();
	FCPFlowGraph FlowGraph;
	FString FlowError;
	const bool bFlowCaptured = FCPFlowCapture::Capture(WorldContext, FCPFlowCaptureParams(), FlowGraph, FlowError);
	// Timed apart from the solve: this half touches live actors and is stuck on the game thread,
	// so it is the half that can stutter. The solve is pure data and the panel runs it elsewhere.
	const double FlowCaptureMilliseconds = (FPlatformTime::Seconds() - FlowStartSeconds) * 1000.0;
	if (!bFlowCaptured)
	{
		OutResult.FlowEvidence = ECPFlowEvidenceState::CaptureFailed;
		UE_LOG(LogCriticalPathEngine, Warning,
			TEXT("WorldAnalysis: flow capture failed (%s) - returning base analysis only"), *FlowError);
		if (OutArtifacts)
		{
			OutArtifacts->Snapshot = MoveTemp(Snapshot);
			OutArtifacts->CollectMilliseconds = CollectMilliseconds;
			OutArtifacts->FlowCaptureMilliseconds = FlowCaptureMilliseconds;
			OutArtifacts->TotalMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
			OutArtifacts->GameThreadMilliseconds = OutArtifacts->TotalMilliseconds;
		}
		return true;
	}

	const double SolveStartSeconds = FPlatformTime::Seconds();
	FCPFlowSolveResult CurrentSolve;
	FCPFlowSolver::Solve(FlowGraph, FCPFlowSolveParams(), CurrentSolve);

	FCPFlowSolveParams DesignParams;
	DesignParams.bUseDesignRates = true;
	FCPFlowSolveResult DesignSolve;
	FCPFlowSolver::Solve(FlowGraph, DesignParams, DesignSolve);

	// Enrichment still runs when a solve did not settle: the derived layer marks those values
	// unknown itself (bKnown / ECPFlowUnknownReason), which is more useful than dropping them.
	FCPFlowAnalysis::BuildItemBalances(FlowGraph, CurrentSolve, DesignSolve, OutResult.ItemBalances);
	FCPFlowAnalysis::BuildSolverSufficiency(OutResult.ItemBalances, OutResult.Sufficiency);
	FCPFlowAnalysis::ApplySolverPathInterpretation(Snapshot, OutResult.ItemBalances, OutResult);
	FCPFlowAnalysis::ApplySolverPlanInterpretation(OutResult.ItemBalances, OutResult);
	FCPFlowAnalysis::ApplyPrimaryMarginalValue(FlowGraph, DesignSolve, OutResult);
	OutResult.FlowEvidence = (CurrentSolve.bConverged && DesignSolve.bConverged)
		? ECPFlowEvidenceState::Available
		: ECPFlowEvidenceState::NotConverged;

	const double FlowMilliseconds = (FPlatformTime::Seconds() - FlowStartSeconds) * 1000.0;
	const double SolveMilliseconds = (FPlatformTime::Seconds() - SolveStartSeconds) * 1000.0;
	if (OutArtifacts)
	{
		OutArtifacts->Snapshot = MoveTemp(Snapshot);
		OutArtifacts->FlowGraph = MoveTemp(FlowGraph);
		OutArtifacts->CurrentSolve = MoveTemp(CurrentSolve);
		OutArtifacts->DesignSolve = MoveTemp(DesignSolve);
		OutArtifacts->CollectMilliseconds = CollectMilliseconds;
		OutArtifacts->FlowMilliseconds = FlowMilliseconds;
		OutArtifacts->FlowCaptureMilliseconds = FlowCaptureMilliseconds;
		OutArtifacts->SolveMilliseconds = SolveMilliseconds;
		OutArtifacts->TotalMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
		// This entry point runs the solve inline, so subtract it to report what the PANEL's
		// game thread would actually pay — the panel hands the solve to the thread pool.
		OutArtifacts->GameThreadMilliseconds = OutArtifacts->TotalMilliseconds - SolveMilliseconds;
	}

	return true;
}
