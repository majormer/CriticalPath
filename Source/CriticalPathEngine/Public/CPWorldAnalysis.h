// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPFlowTypes.h"
#include "CPSnapshot.h"

/** Optional capture/solve evidence for diagnostics and consumers that need the underlying graph.
 *  All fields are plain snapshot data and remain safe to use off the game thread after capture. */
struct CRITICALPATHENGINE_API FCPWorldAnalysisArtifacts
{
	FCPFactorySnapshot Snapshot;
	FCPFlowGraph FlowGraph;
	FCPFlowSolveResult CurrentSolve;
	FCPFlowSolveResult DesignSolve;
	double CollectMilliseconds = 0.0;
	double FlowMilliseconds = 0.0;
	double TotalMilliseconds = 0.0;

	/** The split that decides whether the panel is shippable, kept apart from FlowMilliseconds
	 *  because that number lumps them and cannot answer the question:
	 *
	 *    FlowCapture MUST run on the game thread — it walks live actors — so it is a HITCH.
	 *    Solve is pure data and the panel runs it on the thread pool, so it is NOT a hitch.
	 *
	 *  Game-thread cost is therefore Collect + Analyze + FlowCapture. Reporting only a combined
	 *  figure made a 2.98s solve look like 2.98s of stutter, when the stall the player actually
	 *  feels is a fraction of it. */
	double FlowCaptureMilliseconds = 0.0;
	double SolveMilliseconds = 0.0;
	/** Collect + Analyze + FlowCapture: what the player feels when opening or refreshing. */
	double GameThreadMilliseconds = 0.0;
};

/** Public typed entry point for external engine consumers.
 *
 *  Owns the complete live-world pipeline: bounded UObject capture on the game thread, pure base
 *  analysis, dual flow solve, and Phase C enrichment. Consumers should call this instead of
 *  reproducing the engine's orchestration sequence.
 */
class CRITICALPATHENGINE_API FCPWorldAnalysis
{
public:
	/** Must be called on the game thread.
	 *
	 *  Partial success is deliberate. Returns FALSE only when the REQUIRED snapshot capture
	 *  fails (no world / no buildable subsystem), in which case there is no report at all and
	 *  OutError says why. Everything else returns TRUE with a usable report: if flow capture or
	 *  convergence fails, objectives, the owned ledger, machine-state facts and research gaps
	 *  are still populated and OutResult.FlowEvidence records what is missing. Callers that
	 *  need solver-backed numbers must check FlowEvidence rather than the return value. */
	static bool Analyze(
		UObject* WorldContext,
		const FCPSnapshotParams& SnapshotParams,
		FCPAnalysisResult& OutResult,
		FString& OutError,
		FCPWorldAnalysisArtifacts* OutArtifacts = nullptr);
};
