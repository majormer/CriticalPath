// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "Modules/ModuleManager.h"

#include "CPJson.h"
#include "CPModularFeature.h"
#include "CPWorldAnalysis.h"
#include "FGPlayerController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY(LogCriticalPathEngine);

/** Writing the whole flow graph to a player's disk is a DIAGNOSTIC ACTION, so it needs consent,
 *  not merely a condition. Previously any non-converged solve serialised several thousand nodes
 *  and edges into Saved/ silently: the player never asked for it and never found out it happened.
 *
 *  A condition is not a control. Off by default; when enabled the write is announced in the log
 *  so the artifact is discoverable rather than something found by accident later. */
static TAutoConsoleVariable<int32> CVarDumpFlowGraph(
	TEXT("cp.DumpFlowGraph"),
	0,
	TEXT("Critical Path flow-graph diagnostic dump to Saved/CriticalPath/FlowGraphDebug.json.\n")
	TEXT("  0: never (default)\n")
	TEXT("  1: only when a solve fails to converge\n")
	TEXT("  2: every analysis"),
	ECVF_Default);

/** JSON-only feature implementation for link-free consumers (see CPModularFeature.h). */
class FCriticalPathEngineFeatureImpl final : public ICriticalPathEngineFeature
{
public:
	virtual bool GetReportJson(UObject* WorldContext, bool bIncludeSnapshot, FString& OutJson, FString& OutError) override
	{
		check(IsInGameThread());

		FCPSnapshotParams Params;
		if (UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr)
		{
			// Player context when one exists; absent on dedicated servers — the ledger reports
			// player-scoped components as absent, never zero.
			Params.PlayerContext = Cast<AFGPlayerController>(World->GetFirstPlayerController());
		}

		FCPAnalysisResult Result;
		FCPWorldAnalysisArtifacts Artifacts;
		if (!FCPWorldAnalysis::Analyze(WorldContext, Params, Result, OutError, &Artifacts))
		{
			return false;
		}

		// Preserve a replay artifact only when explicitly asked for. Routine refreshes must not
		// churn the Saved directory or leave stale-looking evidence, and a player who never
		// enabled diagnostics must not have a multi-megabyte graph written behind their back.
		const int32 DumpMode = CVarDumpFlowGraph.GetValueOnGameThread();
		const bool bAnySolveFailed = !Artifacts.CurrentSolve.bConverged || !Artifacts.DesignSolve.bConverged;
		if (DumpMode >= 2 || (DumpMode == 1 && bAnySolveFailed))
		{
			FString GraphJson;
			if (FJsonObjectConverter::UStructToJsonObjectString(Artifacts.FlowGraph, GraphJson))
			{
				const FString DumpPath = FPaths::ProjectSavedDir() / TEXT("CriticalPath") / TEXT("FlowGraphDebug.json");
				if (FFileHelper::SaveStringToFile(GraphJson, *DumpPath))
				{
					// Announced, so the artifact is discoverable rather than found by accident.
					UE_LOG(LogCriticalPathEngine, Log,
						TEXT("cp.DumpFlowGraph=%d: wrote %d nodes / %d edges (%.1f MB) to %s"),
						DumpMode, Artifacts.FlowGraph.Nodes.Num(), Artifacts.FlowGraph.Edges.Num(),
						static_cast<double>(GraphJson.Len()) / (1024.0 * 1024.0), *DumpPath);
				}
				else
				{
					UE_LOG(LogCriticalPathEngine, Warning,
						TEXT("cp.DumpFlowGraph=%d: could not write %s"), DumpMode, *DumpPath);
				}
			}
		}

		FString FlowJson;
		FString SolveJson;
		FString DesignJson;
		if (FJsonObjectConverter::UStructToJsonObjectString(Artifacts.CurrentSolve, SolveJson, 0, 0, 0, nullptr, false) &&
			FJsonObjectConverter::UStructToJsonObjectString(Artifacts.DesignSolve, DesignJson, 0, 0, 0, nullptr, false))
		{
			FlowJson = FString::Printf(TEXT("{\"nodes\":%d,\"edges\":%d,\"truncated\":%s,\"solve\":%s,\"designSolve\":%s}"),
				Artifacts.FlowGraph.Nodes.Num(), Artifacts.FlowGraph.Edges.Num(),
				Artifacts.FlowGraph.bTruncated ? TEXT("true") : TEXT("false"), *SolveJson, *DesignJson);
		}

		FString ResultJson;
		if (!FCPJson::ResultToJson(Result, ResultJson))
		{
			OutError = TEXT("Failed to serialize analysis result.");
			return false;
		}
		OutJson = FString::Printf(
			TEXT("{\"collectMs\":%.2f,\"totalMs\":%.2f,\"flowMs\":%.2f,\"flowCaptureMs\":%.2f,")
			TEXT("\"solveMs\":%.2f,\"gameThreadMs\":%.2f,\"result\":%s"),
			Artifacts.CollectMilliseconds, Artifacts.TotalMilliseconds, Artifacts.FlowMilliseconds,
			Artifacts.FlowCaptureMilliseconds, Artifacts.SolveMilliseconds,
			Artifacts.GameThreadMilliseconds, *ResultJson);
		if (!FlowJson.IsEmpty())
		{
			OutJson += TEXT(",\"flowExperimental\":") + FlowJson;
		}
		if (bIncludeSnapshot)
		{
			FString SnapshotJson;
			if (FCPJson::SnapshotToJson(Artifacts.Snapshot, SnapshotJson))
			{
				OutJson += TEXT(",\"snapshot\":") + SnapshotJson;
			}
		}
		OutJson += TEXT("}");
		return true;
	}
};

class FCriticalPathEngineModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		IModularFeatures::Get().RegisterModularFeature(ICriticalPathEngineFeature::GetModularFeatureName(), &Feature);
	}

	virtual void ShutdownModule() override
	{
		IModularFeatures::Get().UnregisterModularFeature(ICriticalPathEngineFeature::GetModularFeatureName(), &Feature);
	}

private:
	FCriticalPathEngineFeatureImpl Feature;
};

IMPLEMENT_MODULE(FCriticalPathEngineModule, CriticalPathEngine);
