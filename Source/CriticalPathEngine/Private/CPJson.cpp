// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPJson.h"

#include "JsonObjectConverter.h"

bool FCPJson::SnapshotToJson(const FCPFactorySnapshot& Snapshot, FString& OutJson)
{
	return FJsonObjectConverter::UStructToJsonObjectString(Snapshot, OutJson);
}

bool FCPJson::ResultToJson(const FCPAnalysisResult& Result, FString& OutJson)
{
	return FJsonObjectConverter::UStructToJsonObjectString(Result, OutJson);
}
