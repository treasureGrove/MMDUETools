#pragma once

#include "CoreMinimal.h"

namespace MMDEndfieldNativeBackend
{
	// 同步 generated overlay marker 与版本命名的 shader binding；旧 Cook 必须拒绝路由。
	inline constexpr uint32 CookedGlobalShaderProtocolVersion = 11u;

	struct UE5MMDTOOLS_API FStatus
	{
		bool bRequested = false;
		bool bActive = false;
		bool bCookedRuntime = false;
		bool bCookedData = false;
		bool bProfileMarkerAllowed = false;
		bool bCookedGlobalShaderProtocolVerified = false;
		uint32 CookedGlobalShaderProtocol = 0;
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
		FString CompiledPayloadProtocol;
		FString PatchedEvaluationHash;
		FString PatchedIndirectHash;
		FString Reason;
		TMap<FString, FString> SourceHashes;
		TMap<FString, FString> PatchedSourceHashes;
		TMap<FString, FString> PluginSourceHashes;
	};

	/** 在 PostConfigInit 的游戏线程生成插件私有适配副本，验证后才注册更深 shader 映射。 */
	UE5MMDTOOLS_API void Initialize();
	/** Cooked 运行在 OnPostEngineInit 检查已加载 shader 的序列化协议，成功后才允许 Profile 标记。 */
	UE5MMDTOOLS_API void VerifyCookedShaderProtocol();
	/** 读取现有全局 shader map 的编译反射记录；不编译、不创建 shader map、不执行 GPU。 */
	UE5MMDTOOLS_API bool ReadGlobalShaderProtocol(uint32& OutProtocol, FString& OutReason);
	UE5MMDTOOLS_API const FStatus& GetStatus();
	/** CustomOutput 与 packed payload 的模块编译协议；分发时不读取私有 C++ 源文件。 */
	UE5MMDTOOLS_API const TCHAR* GetCompiledPayloadProtocol();
	/** 原始文件字节的 SHA1，仅用于固定版本完整性检查和诊断。 */
	UE5MMDTOOLS_API bool ComputeFileHash(const FString& Path, FString& OutHash);
}
