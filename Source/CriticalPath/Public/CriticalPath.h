// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

// Project config mutes LogTemp to Warning in shipping - always log through this category.
DECLARE_LOG_CATEGORY_EXTERN(LogCriticalPath, Log, All);

class FCriticalPathModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
