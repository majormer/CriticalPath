// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

using UnrealBuildTool;

// The analysis engine: measurement only, structured results only.
// RULE: this module may never depend on UMG/Slate/UI modules of any kind. UI belongs to the
// CriticalPath module (or any other consumer), which uses these public headers like a stranger.
public class CriticalPathEngine : ModuleRules
{
    public CriticalPathEngine(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "FactoryGame",
            "SML",
            "Json",
            "JsonUtilities",
        });
    }
}
