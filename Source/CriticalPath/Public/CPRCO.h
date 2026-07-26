// Copyright (c) 2026-present Finalomega Labs. All rights reserved. See LICENSE.md.

#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"
#include "CPEngineTypes.h"
#include "CPRCO.generated.h"

/**
 * Host-to-client report relay.
 *
 * A joined client cannot COMPUTE a report: machine inventories, storage levels, station contents
 * and fluid boxes are server-side, so a client-side capture reads empty and would solve cleanly
 * into a confident wrong answer. That is why the client refuses to analyse locally.
 *
 * It can, however, be TOLD the answer. The host already computes a correct report, and
 * FCPAnalysisResult is plain data with no UObject references — the same property that lets it
 * cross threads lets it cross the network. The client asks, the host replies, the panel renders
 * a report it did not compute.
 *
 * The payload is serialised and compressed before sending. It compresses roughly thirty to one,
 * because a factory report is enormously repetitive — measured at 432 KB down to 15 KB on a live
 * world. It is then split into chunks, because a reliable RPC cannot carry a payload that size in
 * one call.
 *
 * SML discovers UFGRemoteCallObject subclasses automatically; no explicit registration is needed.
 */
UCLASS(Within = FGPlayerController)
class CRITICALPATH_API UCPRCO : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	//~ Begin UFGRemoteCallObject
	virtual bool ShouldRegisterRemoteCallObject(const class AFGGameMode* GameMode) const override { return true; }
	//~ End UFGRemoteCallObject

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Client → host: "send me a report". Rate limited and cached server-side. */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestReport();

	/** Host → client: a transfer is starting. Resets any partial transfer already in flight. */
	UFUNCTION(Client, Reliable)
	void Client_BeginReport(int32 ChunkCount, int32 UncompressedSize);

	/** Host → client: one chunk of the compressed payload, in order. */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveChunk(const TArray<uint8>& Chunk);

	/** Host → client: the host could not produce a report, and why. Never a silent failure. */
	UFUNCTION(Client, Reliable)
	void Client_ReportUnavailable(const FString& Reason);

	/** Compress a result for transport. Returns false rather than sending a partial payload. */
	static bool PackReport(const FCPAnalysisResult& Report, TArray<uint8>& OutPayload, int32& OutUncompressedSize);

	/** Inverse of PackReport. Returns false if the payload is truncated or corrupt. */
	static bool UnpackReport(const TArray<uint8>& Payload, int32 UncompressedSize, FCPAnalysisResult& OutReport);

	/** Bytes per RPC. Reliable RPCs cannot carry an entire report, and oversized bunches are
	 *  dropped rather than fragmented, so the payload is split conservatively. */
	static constexpr int32 ChunkBytes = 8192;

private:
	UPROPERTY(Replicated)
	bool bDummyReplicated = false;

	// ---- client-side transfer state ----
	TArray<uint8> IncomingPayload;
	int32 ExpectedChunks = 0;
	int32 ReceivedChunks = 0;
	int32 ExpectedUncompressedSize = 0;

	void DeliverAssembledReport();
};
