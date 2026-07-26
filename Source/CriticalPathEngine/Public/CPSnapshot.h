// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPEngineTypes.h"

class AFGPlayerController;

/** Collection caps. Every cap that trips is reported in FCPTruncation. */
struct CRITICALPATHENGINE_API FCPSnapshotParams
{
	int32 MaxBuildingsToScan = 2000;
	int32 MaxStorageContainersToScan = 500;
	int32 MaxRecipeEdges = 128;
	int32 MaxObjectiveItems = 32;
	int32 MaxResearchGaps = 6;
	int32 MaxCatalogRecipes = 64;
	int32 CatalogWalkMaxDepth = 8;

	int32 ResearchWalkMaxDepth = 4;
	int32 ResearchWalkMaxItems = 64;
	int32 MaxConnectivityBuildingsToScan = 50000;
	int32 MaxConnectivityStatesToPropagate = 250000;

	/** Optional player context: pockets + (later) anchored queries. Null = server/no-player
	 *  mode; player-scoped ledger components are ABSENT, never zero (they simply don't appear).
	 *  The depot is server-side and collected regardless. */
	AFGPlayerController* PlayerContext = nullptr;
};

/**
 * Game-thread extraction: reads live UObjects, produces the plain-data FCPFactorySnapshot.
 * Analysis (CPAnalysis.h) then runs on the snapshot alone — UObject-free by construction.
 *
 * M0: single synchronous pass under hard caps. Time-slicing/dirty-tracking arrive with the
 * performance milestone; the seam is already right (extraction is the only game-thread stage).
 */
class CRITICALPATHENGINE_API FCPSnapshotCollector
{
public:
	/** Must be called on the game thread. Returns false only when no world is available. */
	static bool Collect(UObject* WorldContext, const FCPSnapshotParams& Params, FCPFactorySnapshot& OutSnapshot, FString& OutError);
};
