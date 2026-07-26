// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPInputSubsystem.h"

#include "CriticalPath.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Input/FGInputMappingContext.h"
#include "InputAction.h"

namespace
{
// Base-game-style soft loads: assets ship with this mod, so these paths are our own.
const TCHAR* MappingContextPath = TEXT("/CriticalPath/CriticalPath/Input/MC_CriticalPath_Player.MC_CriticalPath_Player");
const TCHAR* ToggleActionPath = TEXT("/CriticalPath/CriticalPath/Input/IA_CriticalPath_TogglePanel.IA_CriticalPath_TogglePanel");

// Low priority: F10 conflicts with nothing vanilla; we never need to outrank base-game input
// (contrast with build-gun-scoped contexts that must consume wheel/keys at high priority).
constexpr int32 ContextPriority = 10;
}

bool UCPInputSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	return World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}

void UCPInputSubsystem::Deinitialize()
{
	// Leave no context behind in the Enhanced Input stack.
	if (APlayerController* PC = RegisteredController.Get())
	{
		if (PC->GetLocalPlayer())
		{
			if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
			{
				if (MappingContext)
				{
					InputSubsystem->RemoveMappingContext(MappingContext);
				}
			}
		}
	}
	Super::Deinitialize();
}

void UCPInputSubsystem::Tick(float DeltaTime)
{
	// Poll until the local player exists, register once, then stop ticking (IsTickable gates).
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC || !PC->GetLocalPlayer() || !PC->InputComponent)
	{
		return;
	}

	if (!MappingContext)
	{
		MappingContext = LoadObject<UFGInputMappingContext>(nullptr, MappingContextPath);
	}
	if (!ToggleAction)
	{
		ToggleAction = LoadObject<UInputAction>(nullptr, ToggleActionPath);
	}
	if (!MappingContext || !ToggleAction)
	{
		UE_LOG(LogCriticalPath, Warning, TEXT("CriticalPath: input assets failed to load; keybind unavailable."));
		bInputRegistered = true; // stop ticking; nothing to retry
		return;
	}

	UEnhancedInputLocalPlayerSubsystem* InputSubsystem = PC->GetLocalPlayer()->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
	UEnhancedInputComponent* InputComponent = Cast<UEnhancedInputComponent>(PC->InputComponent);
	if (!InputSubsystem || !InputComponent)
	{
		return;
	}

	InputSubsystem->AddMappingContext(MappingContext, ContextPriority);
	InputComponent->BindAction(ToggleAction, ETriggerEvent::Started, this, &UCPInputSubsystem::HandleTogglePanel);
	RegisteredController = PC;
	bInputRegistered = true;
	UE_LOG(LogCriticalPath, Display, TEXT("CriticalPath: player input context registered (F10 default, priority %d)."), ContextPriority);
}

void UCPInputSubsystem::HandleTogglePanel()
{
	UE_LOG(LogCriticalPath, Display, TEXT("CriticalPath: toggle panel."));
	OnTogglePanel.Broadcast();
}
