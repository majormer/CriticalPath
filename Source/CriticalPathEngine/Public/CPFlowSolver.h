// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPFlowTypes.h"

struct CRITICALPATHENGINE_API FCPFlowSolveParams
{
	int32 MaxIterations = 96;
	/** Convergence threshold on the largest per-edge flow delta (units/min). */
	float Epsilon = 0.01f;
	/** Relative floor per transport domain. One tenth of one percent is well below the
	 *  presentation/observation precision while avoiding hundreds of meaningless merger-share
	 *  rebalances in large buffered manifolds. */
	float RelativeEpsilon = 1.0e-3f;
	/** Damping for cyclic graphs (belt loops): new = old + Damping * (target - old). */
	float Damping = 0.5f;

	/** Iterations a component may go without beating its best residual before the solver starts
	 *  damping it. Oscillation is NOT only a property of cyclic topology: a saturated manifold
	 *  feeding many tied consumers has no unique allocation, so an undamped water-fill can flip
	 *  between equally valid answers indefinitely. Observed on a live save as twelve consecutive
	 *  iterations at exactly 1.0547 on a graph the cycle detector found acyclic. */
	int32 StallIterations = 8;

	/** Step multiplier applied each time a component stalls again, and the floor it stops at.
	 *  Shrinking the step turns a fixed-amplitude limit cycle into a decaying one. */
	float StallDampingFalloff = 0.5f;
	float MinDamping = 0.03f;
	/** Solve on DesignRates ("does the design close?") instead of current-state Rates. */
	bool bUseDesignRates = false;
	/** Diagnostic only: visit every item at every node instead of the per-node candidate set.
	 *  Vastly slower and must produce IDENTICAL results — the equivalence test relies on it. */
	bool bDenseItemIteration = false;
};

/**
 * Steady-state, demand-driven, capacity-constrained flow solver (FlowModel.md §3).
 * Pure and UObject-free: damped Jacobi fixed point alternating a supply pass (producers push,
 * splitters water-fill, mergers admit proportional-fair, storage is transparent at steady state) with an
 * acceptance pass (consumers pull up to demand; backpressure propagates upstream).
 * Non-convergence reports bConverged=false — callers must present UNKNOWN, never a
 * half-converged number.
 */
class CRITICALPATHENGINE_API FCPFlowSolver
{
public:
	static void Solve(const FCPFlowGraph& Graph, const FCPFlowSolveParams& Params, FCPFlowSolveResult& OutResult);

	/** Merge clusters of bContractible nodes joined by unblocked edges into single nodes and
	 *  sum parallel unblocked fluid edges. Pipe-junction manifolds otherwise form dense
	 *  bidirectional cycle webs that slow the fixed point to a crawl. Node indices change;
	 *  call BEFORE solving, never between a solve and reading its results. */
	static void CollapseContractibleClusters(FCPFlowGraph& Graph);
};
