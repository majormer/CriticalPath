// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "CPEngineTypes.h"

/** JSON serialization for external tooling surfaces and the export feature. Thin wrappers over
 *  FJsonObjectConverter — the types are UPROPERTY-reflected precisely so this stays trivial. */
class CRITICALPATHENGINE_API FCPJson
{
public:
	static bool SnapshotToJson(const FCPFactorySnapshot& Snapshot, FString& OutJson);
	static bool ResultToJson(const FCPAnalysisResult& Result, FString& OutJson);
};
