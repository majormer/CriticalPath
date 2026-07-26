// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPEngineTypes.h"

class AFGPlayerController;

/** Collection caps. Every cap that trips is reported in FCPTruncation.
 *
 *  THESE ARE LATENCY GUARDS, NOT MEMORY GUARDS. A cap never saves allocation — the arrays hold
 *  only what the world actually contains — it just stops the walk early and yields a wrong answer
 *  faster. The panel's own widget budget is what bounds UObjects (see CPPanelWidget.cpp), and it
 *  is enforced independently, so these numbers no longer have to protect the render path.
 *
 *  The original values were sized for a mid-game base and turned out to be far under real play: a
 *  normal endgame save measured 8,808 manufacturers, which blew a 2,000 cap by 4x and reported
 *  four objective parts as "nothing produces it" when production plainly existed. Sizing these for
 *  the base a player actually finishes the game with is the point of the mod, so they are now set
 *  well above any observed save rather than close to it.
 *
 *  The cost of raising them is game-thread time on a huge factory, paid once per refresh (the scan
 *  does not run on a timer). That trade is deliberate: a slower correct answer beats a fast wrong
 *  one. If a save is ever found that makes a refresh hitch, the fix is to time-slice the walk, not
 *  to put the ceiling back. */
struct CRITICALPATHENGINE_API FCPSnapshotParams
{
	int32 MaxBuildingsToScan = 200000;
	int32 MaxStorageContainersToScan = 50000;
	/** Recipe-graph breadth, not world size: modded recipe sets (ContentLib and friends) blow the
	 *  original 128/64 easily, and a truncated recipe graph looks like a missing production path. */
	int32 MaxRecipeEdges = 4096;
	int32 MaxObjectiveItems = 32;
	int32 MaxResearchGaps = 6;
	int32 MaxCatalogRecipes = 1024;
	int32 CatalogWalkMaxDepth = 8;

	int32 ResearchWalkMaxDepth = 4;
	int32 ResearchWalkMaxItems = 64;
	/** Counts EVERY buildable, foundations and walls included, so it must clear the total build
	 *  count of a finished base — not the machine count. */
	int32 MaxConnectivityBuildingsToScan = 1000000;
	int32 MaxConnectivityStatesToPropagate = 5000000;

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
