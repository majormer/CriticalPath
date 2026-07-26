// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPFlowTypes.h"

/** Pure Phase C interpretation over a captured graph and its dual solves. */
class CRITICALPATHENGINE_API FCPFlowAnalysis
{
public:
	/** Build one four-rate balance per item-specific weakly connected supply domain. */
	static void BuildItemBalances(
		const FCPFlowGraph& Graph,
		const FCPFlowSolveResult& CurrentSolve,
		const FCPFlowSolveResult& DesignSolve,
		TArray<FCPItemBalance>& OutBalances);

	/** Aggregate disjoint item domains into public Current/Design sufficiency summaries. */
	static void BuildSolverSufficiency(
		const TArray<FCPItemBalance>& Balances,
		TArray<FCPItemSufficiency>& OutSufficiency);

	/** Replace Path blocker/limiter rate decisions with disjoint solver-domain evidence while
	 *  retaining snapshot machine faults and recipe dependency paths. Pure analysis. */
	static void ApplySolverPathInterpretation(
		const FCPFactorySnapshot& Snapshot,
		const TArray<FCPItemBalance>& Balances,
		FCPAnalysisResult& InOutResult);

	/** Replace prospective existing-line graft headroom with design-basis sustainable capacity
	 *  from disjoint item domains. Unknown domains keep the graft result explicitly unknown. */
	static void ApplySolverPlanInterpretation(
		const TArray<FCPItemBalance>& Balances,
		FCPAnalysisResult& InOutResult);

	/** Evaluate one exact action for the report's primary actionable limiter. At most one
	 *  additional design solve is performed per report: another average line machine for an
	 *  installed-capacity constraint, or the next standard tier for a proven belt/pipe edge. */
	static void ApplyPrimaryMarginalValue(
		const FCPFlowGraph& Graph,
		const FCPFlowSolveResult& BaselineDesignSolve,
		FCPAnalysisResult& InOutResult);
};
