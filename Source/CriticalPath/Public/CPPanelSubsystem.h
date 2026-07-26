// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CPEngineTypes.h"
#include "CPPanelSubsystem.generated.h"

class UCPPanelWidget;

/**
 * Owns the Critical Path panel instance and its lifecycle: subscribes to the input subsystem's
 * toggle, runs the engine (collect + analyze on the game thread), and shows/hides the panel.
 *
 * Caching contract (PRD lifecycle states): the last result is kept with its capture time; a
 * toggle-open always re-collects (M0 collection is ~3ms at 402 buildings — cheap enough to be
 * fresh every open), but if collection fails the cached result is shown with its age so the
 * player never gets a blank panel that used to have data.
 */
UCLASS()
class CRITICALPATH_API UCPPanelSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Deinitialize() override;

	bool IsPanelVisible() const;

	/** A joined client cannot measure the factory, but the host can send its report. These are
	 *  called by the relay when one arrives, or when the host says it cannot produce one. */
	void ApplyHostReport(const FCPAnalysisResult& Report);
	void ApplyHostReportFailure(const FString& Reason);

private:
	void HandleTogglePanel();
	void ShowPanel();
	void HidePanel();

	/** Re-run the engine and push into the panel. Falls back to the cached result on failure. */
	void RefreshReport();

	/** Ask the host for a report. Only meaningful on a non-authoritative client. */
	void RequestReportFromHost();

	UPROPERTY()
	TObjectPtr<UCPPanelWidget> Panel;

	FCPAnalysisResult CachedResult;
	float CachedAtWorldSeconds = -1.0f;
	bool bHasCachedResult = false;
	uint64 RefreshGeneration = 0;
	FDelegateHandle ToggleHandle;
};
