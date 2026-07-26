// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#include "CPRCO.h"

#include "CPPanelSubsystem.h"
#include "CPWorldAnalysis.h"
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "Misc/Compression.h"
#include "Net/UnrealNetwork.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogCriticalPathNet, Log, All);

namespace
{
	/** Serving a fresh analysis to every client on every press would let one player with a held
	 *  key cost the host a full capture per frame. One analysis is shared by everyone who asks
	 *  inside this window; the panel already shows the reading's age, so a slightly older report
	 *  is honest rather than hidden. */
	constexpr double ServerCacheSeconds = 5.0;

	FCPAnalysisResult GServerCachedReport;
	double GServerCachedAt = -1.0;
	bool GServerCacheValid = false;
}

void UCPRCO::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// SML rule: an RCO replicates only if it registers at least one replicated property.
	DOREPLIFETIME(UCPRCO, bDummyReplicated);
}

// ---------------------------------------------------------------------------------------------
// Payload packing
// ---------------------------------------------------------------------------------------------

bool UCPRCO::PackReport(const FCPAnalysisResult& Report, TArray<uint8>& OutPayload, int32& OutUncompressedSize)
{
	TArray<uint8> Raw;
	{
		FMemoryWriter Writer(Raw, /*bIsPersistent*/ true);
		// Non-const because the serializer takes a mutable pointer; it is not modified.
		FCPAnalysisResult Copy = Report;
		FCPAnalysisResult::StaticStruct()->SerializeItem(Writer, &Copy, nullptr);
	}
	if (Raw.Num() == 0)
	{
		return false;
	}

	OutUncompressedSize = Raw.Num();
	int32 Bound = FCompression::CompressMemoryBound(NAME_Zlib, Raw.Num());
	OutPayload.SetNumUninitialized(Bound);
	if (!FCompression::CompressMemory(NAME_Zlib, OutPayload.GetData(), Bound, Raw.GetData(), Raw.Num()))
	{
		OutPayload.Reset();
		return false;
	}
	OutPayload.SetNum(Bound, EAllowShrinking::Yes);
	return true;
}

bool UCPRCO::UnpackReport(const TArray<uint8>& Payload, int32 UncompressedSize, FCPAnalysisResult& OutReport)
{
	if (Payload.Num() == 0 || UncompressedSize <= 0)
	{
		return false;
	}
	TArray<uint8> Raw;
	Raw.SetNumUninitialized(UncompressedSize);
	if (!FCompression::UncompressMemory(NAME_Zlib, Raw.GetData(), UncompressedSize, Payload.GetData(), Payload.Num()))
	{
		return false;
	}
	FMemoryReader Reader(Raw, /*bIsPersistent*/ true);
	FCPAnalysisResult::StaticStruct()->SerializeItem(Reader, &OutReport, nullptr);
	return !Reader.IsError();
}

// ---------------------------------------------------------------------------------------------
// Server side
// ---------------------------------------------------------------------------------------------

bool UCPRCO::Server_RequestReport_Validate()
{
	return true;
}

void UCPRCO::Server_RequestReport_Implementation()
{
	AFGPlayerController* PC = GetOuterAFGPlayerController();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}

	const double Now = FPlatformTime::Seconds();
	const bool bCacheFresh = GServerCacheValid && (Now - GServerCachedAt) < ServerCacheSeconds;

	if (!bCacheFresh)
	{
		FCPSnapshotParams Params;
		Params.PlayerContext = PC;   // the requesting player's pockets, not the host's
		FCPAnalysisResult Result;
		FString Error;
		if (!FCPWorldAnalysis::Analyze(World, Params, Result, Error))
		{
			UE_LOG(LogCriticalPathNet, Warning, TEXT("Report request failed: %s"), *Error);
			Client_ReportUnavailable(Error.IsEmpty()
				? TEXT("The host could not read the factory just now.") : Error);
			return;
		}
		GServerCachedReport = MoveTemp(Result);
		GServerCachedAt = Now;
		GServerCacheValid = true;
	}

	TArray<uint8> Payload;
	int32 UncompressedSize = 0;
	if (!PackReport(GServerCachedReport, Payload, UncompressedSize))
	{
		Client_ReportUnavailable(TEXT("The host could not package the report for sending."));
		return;
	}

	const int32 ChunkCount = FMath::DivideAndRoundUp(Payload.Num(), ChunkBytes);
	UE_LOG(LogCriticalPathNet, Log,
		TEXT("Serving report to %s: %d bytes raw, %d compressed (%.1f%%), %d chunk(s), cache %s"),
		*GetNameSafe(PC), UncompressedSize, Payload.Num(),
		UncompressedSize > 0 ? 100.0f * Payload.Num() / UncompressedSize : 0.0f,
		ChunkCount, bCacheFresh ? TEXT("hit") : TEXT("miss"));

	Client_BeginReport(ChunkCount, UncompressedSize);
	for (int32 Offset = 0; Offset < Payload.Num(); Offset += ChunkBytes)
	{
		const int32 Size = FMath::Min(ChunkBytes, Payload.Num() - Offset);
		TArray<uint8> Chunk;
		Chunk.Append(Payload.GetData() + Offset, Size);
		Client_ReceiveChunk(Chunk);
	}
}

// ---------------------------------------------------------------------------------------------
// Client side
// ---------------------------------------------------------------------------------------------

void UCPRCO::Client_BeginReport_Implementation(int32 ChunkCount, int32 UncompressedSize)
{
	// A new transfer always supersedes an incomplete one; a half-received report is never merged
	// into a newer one, which would produce a corrupt payload that decompresses to nonsense.
	IncomingPayload.Reset();
	ExpectedChunks = ChunkCount;
	ReceivedChunks = 0;
	ExpectedUncompressedSize = UncompressedSize;
}

void UCPRCO::Client_ReceiveChunk_Implementation(const TArray<uint8>& Chunk)
{
	if (ExpectedChunks <= 0)
	{
		return; // chunk without a begin: a stale transfer, discard rather than guess
	}
	IncomingPayload.Append(Chunk);
	++ReceivedChunks;
	if (ReceivedChunks >= ExpectedChunks)
	{
		DeliverAssembledReport();
	}
}

void UCPRCO::DeliverAssembledReport()
{
	const int32 Size = ExpectedUncompressedSize;
	TArray<uint8> Payload = MoveTemp(IncomingPayload);
	ExpectedChunks = 0;
	ReceivedChunks = 0;
	ExpectedUncompressedSize = 0;

	FCPAnalysisResult Report;
	if (!UnpackReport(Payload, Size, Report))
	{
		UE_LOG(LogCriticalPathNet, Warning, TEXT("Received report failed to unpack"));
		Client_ReportUnavailable_Implementation(TEXT("The report from the host arrived damaged."));
		return;
	}

	AFGPlayerController* PC = GetOuterAFGPlayerController();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (UCPPanelSubsystem* Panel = World ? World->GetSubsystem<UCPPanelSubsystem>() : nullptr)
	{
		Panel->ApplyHostReport(Report);
	}
}

void UCPRCO::Client_ReportUnavailable_Implementation(const FString& Reason)
{
	AFGPlayerController* PC = GetOuterAFGPlayerController();
	UWorld* World = PC ? PC->GetWorld() : nullptr;
	if (UCPPanelSubsystem* Panel = World ? World->GetSubsystem<UCPPanelSubsystem>() : nullptr)
	{
		Panel->ApplyHostReportFailure(Reason);
	}
}
