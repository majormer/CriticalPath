// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "CPInputSubsystem.generated.h"

class UFGInputMappingContext;
class UInputAction;

DECLARE_MULTICAST_DELEGATE(FCPOnTogglePanel);

/**
 * Registers the Critical Path player input context (always-on, gameplay-global — NOT scoped to
 * the build gun: this is a review tool used while walking, at the map, or staring at a dead
 * assembler). Default binding: F10, player-remappable via the "Critical Path" section of the
 * game's rebind menu. Local players only; never created on dedicated servers.
 */
UCLASS()
class CRITICALPATH_API UCPInputSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Fired on the game thread when the toggle key is pressed. The panel subscribes. */
	FCPOnTogglePanel OnTogglePanel;

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Deinitialize() override;

	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return !bInputRegistered; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UCPInputSubsystem, STATGROUP_Tickables); }

private:
	void HandleTogglePanel();

	UPROPERTY()
	TObjectPtr<UFGInputMappingContext> MappingContext;

	UPROPERTY()
	TObjectPtr<UInputAction> ToggleAction;

	TWeakObjectPtr<class APlayerController> RegisteredController;
	bool bInputRegistered = false;
};
