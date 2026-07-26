// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPFlowTypes.h"

struct CRITICALPATHENGINE_API FCPFlowCaptureParams
{
	/** Hard cap on captured nodes; exceeding it truncates the graph (honesty flag). */
	int32 MaxNodes = 20000;
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
