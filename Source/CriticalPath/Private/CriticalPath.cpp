// Copyright Epic Games, Inc. All Rights Reserved.

#include "CriticalPath.h"

DEFINE_LOG_CATEGORY(LogCriticalPath);

#define LOCTEXT_NAMESPACE "FCriticalPathModule"

void FCriticalPathModule::StartupModule()
{
	UE_LOG(LogCriticalPath, Display, TEXT("CriticalPath UI module loaded."));
}

void FCriticalPathModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FCriticalPathModule, CriticalPath)