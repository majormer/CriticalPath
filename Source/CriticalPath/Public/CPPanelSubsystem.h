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

private:
	void HandleTogglePanel();
	void ShowPanel();
	void HidePanel();

	/** Re-run the engine and push into the panel. Falls back to the cached result on failure. */
	void RefreshReport();

	UPROPERTY()
	TObjectPtr<UCPPanelWidget> Panel;

	FCPAnalysisResult CachedResult;
	float CachedAtWorldSeconds = -1.0f;
	bool bHasCachedResult = false;
	uint64 RefreshGeneration = 0;
	FDelegateHandle ToggleHandle;
};
