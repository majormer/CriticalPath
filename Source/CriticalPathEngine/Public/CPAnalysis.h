// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPEngineTypes.h"

/**
 * Pure analysis over a snapshot: no UObjects, no world, no side effects. Safe on any thread,
 * unit-testable with synthetic snapshots (see Private/Tests).
 *
 * Base pass: ledger, machine-state classification, banked-aware observed ETA, prospective
 * planning, and categorical root-cause facts. Quantitative routing, sufficiency, blockers,
 * limiters, and runway are applied afterward from disjoint flow domains by FCPFlowAnalysis.
 * If that evidence is unavailable, these values remain unknown rather than falling back to
 * the retired overlapping-island calculation.
 */
class CRITICALPATHENGINE_API FCPAnalysis
{
public:
	static FCPAnalysisResult Analyze(const FCPFactorySnapshot& Snapshot);

	/** Classify one item's production state from its aggregates (exposed for tests/UI legends). */
	static ECPNodeStatus ClassifyItem(const FCPFactorySnapshot& Snapshot, const FString& ItemName);

	/** Root-cause walk: descend from ItemName through configured edges to the deepest broken
	 *  link. Returns false when the chain is healthy (no blocker). */
	static bool FindBlocker(const FCPFactorySnapshot& Snapshot, const FString& ItemName, FCPBlocker& OutBlocker);
};
