// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Features/IModularFeature.h"
#include "Features/IModularFeatures.h"

/**
 * Link-free consumption surface for optional consumers.
 *
 * Two ways to consume the engine:
 *  1. TYPED: depend on the CriticalPathEngine module and call FCPWorldAnalysis::Analyze for the
 *     complete enriched report (hard plugin dependency; structured C++ results). Lower-level
 *     capture and pure-analysis classes remain public for tests and specialized consumers.
 *  2. JSON: use ONLY this header — it is pure-virtual and header-only, so a consumer that
 *     restricts itself to it gains ZERO import-table entries from the engine DLL and can mark
 *     the CriticalPath plugin Optional. Look the feature up at runtime; when the plugin is not
 *     installed the lookup simply fails and the consumer degrades gracefully.
 *
 * WARNING to option-2 consumers: calling ANY non-inline engine symbol (collector, analysis,
 * types) silently re-creates the hard dependency. JSON in, JSON out, nothing else.
 */
class ICriticalPathEngineFeature : public IModularFeature
{
public:
	static FName GetModularFeatureName()
	{
		static const FName Name(TEXT("CriticalPathEngine"));
		return Name;
	}

	static ICriticalPathEngineFeature* Get()
	{
		IModularFeatures& Features = IModularFeatures::Get();
		if (!Features.IsModularFeatureAvailable(GetModularFeatureName()))
		{
			return nullptr;
		}
		return &Features.GetModularFeature<ICriticalPathEngineFeature>(GetModularFeatureName());
	}

	virtual ~ICriticalPathEngineFeature() = default;

	/**
	 * Collect + analyze + serialize in one call. Game thread only.
	 * On success OutJson holds: {"collectMs":..,"totalMs":..,"result":{...}[,"snapshot":{...}]}.
	 * Player-scoped ledger data is included when a local player exists (absent on dedicated
	 * servers, never zero).
	 */
	virtual bool GetReportJson(UObject* WorldContext, bool bIncludeSnapshot, FString& OutJson, FString& OutError) = 0;
};
