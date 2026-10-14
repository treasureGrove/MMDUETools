#pragma once

#include "CoreMinimal.h"

namespace MMDEndfieldNativeBackend
{
	struct UE5MMDTOOLS_API FStatus
	{
		bool bRequested = false;
		bool bActive = false;
		bool bCookedRuntime = false;
		bool bCookedData = false;
		bool bProfileMarkerAllowed = false;
		bool bEngineSourcesVerified = false;
		bool bEngineSourcesUnchanged = false;
		int32 Mode = 2;
		float IndirectDiffuseScale = 1.0f;
		float IndirectSpecularScale = 1.0f;
		FString EngineVersion;
		FString Signature;
		FString VirtualDirectory = TEXT("/Engine/Private");
		FString OverlayDirectory;
		FString ManifestPath;
		FString PluginShaderHash;
		FString PatchedEvaluationHash;
		FString PatchedIndirectHash;
		FString Reason;
		TMap<FString, FString> SourceHashes;
		TMap<FString, FString> PatchedSourceHashes;
		TMap<FString, FString> PluginSourceHashes;
	};

	/** 在 PostConfigInit 的游戏线程生成插件私有适配副本，验证后才注册更深 shader 映射。 */
	UE5MMDTOOLS_API void Initialize();
	UE5MMDTOOLS_API const FStatus& GetStatus();
	/** 原始文件字节的 SHA1，仅用于固定版本完整性检查和诊断。 */
	UE5MMDTOOLS_API bool ComputeFileHash(const FString& Path, FString& OutHash);
}
