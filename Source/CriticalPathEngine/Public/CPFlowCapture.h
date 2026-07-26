// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPFlowTypes.h"

struct CRITICALPATHENGINE_API FCPFlowCaptureParams
{
	/** Hard cap on captured nodes; exceeding it truncates the graph (honesty flag).
	 *
	 *  Budget this against MANUFACTURER COUNT, not building count: a manufacturer contributes TWO
	 *  nodes (a Consumer for its inputs and a Producer for its outputs), and extractors, storage,
	 *  splitters, mergers, pumps, junctions, sinks and transport ports each add more on top. At the
	 *  old 20000 a 10000-machine factory spent the entire budget on manufacturers alone and
	 *  truncated everything else, which reads downstream as "no line" for lines that plainly exist.
	 *
	 *  This also does not follow FCPSnapshotParams::MaxBuildingsToScan — capture walks the world
	 *  itself, so raising the snapshot cap without raising this one leaves the flow layer truncated.
	 *  Solver cost scales with node count; this needs a timing check on a megabase before shipping.
	 *
	 *  Sized against a finished base rather than an observed one: 8,808 manufacturers on a normal
	 *  endgame save is ~17,600 nodes before storage, splitters, mergers, pumps and transport ports
	 *  are counted, so anything in the tens of thousands is a ceiling a real player will reach. */
	int32 MaxNodes = 1000000;
	/** Max belt hops when walking a run to its far end (defensive; runs collapse). */
	int32 MaxBeltHops = 512;
};

/**
 * Builds the typed flow graph from live buildables (FlowModel.md §2): solid and fluid machine
 * networks, storage, objectives, and scheduled transport. UObject traversal stays here on the
 * game thread; solving stays pure.
 */
class CRITICALPATHENGINE_API FCPFlowCapture
{
public:
	static bool Capture(UObject* WorldContext, const FCPFlowCaptureParams& Params, FCPFlowGraph& OutGraph, FString& OutError);
};
