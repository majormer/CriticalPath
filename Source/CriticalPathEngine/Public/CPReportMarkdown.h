// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPEngineTypes.h"

/**
 * Pure formatter: analysis result -> human-readable Markdown (with a Mermaid graph of the
 * blocker chains). No UObjects, no file IO — consumers decide where the text goes.
 */
class CRITICALPATHENGINE_API FCPReportMarkdown
{
public:
	/** CapturedAtIso: pre-formatted timestamp line (formatter stays clock-free for testability). */
	static FString Build(const FCPAnalysisResult& Result, const FString& CapturedAtIso);
};
