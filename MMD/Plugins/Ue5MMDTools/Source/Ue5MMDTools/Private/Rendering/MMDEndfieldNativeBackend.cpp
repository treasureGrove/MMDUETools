#include "Rendering/MMDEndfieldNativeBackend.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformProperties.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "ShaderCompilerCore.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogMMDEndfieldNativeBackend, Log, All);

namespace
{
	constexpr TCHAR MMDEndfieldBackendSection[] = TEXT("Ue5MMDTools.EndfieldNativeBackend");
	constexpr TCHAR MMDEndfieldBackendVirtualRoot[] = TEXT("/Engine/Private");
	constexpr TCHAR MMDEndfieldBackendPatchVersion[] = TEXT("endfield-native-bsdf-v4-native-data-clustered-tile-input");
	MMDEndfieldNativeBackend::FStatus MMDEndfieldBackendStatus;
	bool MMDEndfieldBackendInitialized = false;

	struct FMMDEndfieldBackendGuard
	{
		const TCHAR* RelativePath;
		const TCHAR* ExpectedHash;
		bool bRequired;
	};

	// 固定引擎私有接口；源文件不同就保留原生着色，不尝试模糊匹配补丁。
	const FMMDEndfieldBackendGuard MMDEndfieldBackendGuards[] =
	{
		{ TEXT("Private/Substrate/SubstrateEvaluation.ush"), TEXT("7D6CF09DDE26622376826A4D8D48FDDE6C8A1586"), true },
		{ TEXT("Private/Substrate/Substrate.ush"), TEXT("4565C2E6522189E3BE54DDB9A4D4AD04BE9D8CD0"), true },
		{ TEXT("Private/Substrate/SubstrateToonBSDF.ush"), TEXT("EE6423AE0FAC8E72F5AF7588AB9283A40FB51976"), true },
		{ TEXT("Shared/SubstrateDefinitions.h"), TEXT("454E43696DDFFE263D7C23376A19C71BB597EED6"), true },
		{ TEXT("Private/ToonProfileCommon.ush"), TEXT("90FD17EC9C70A5A34321CE1A3F9D1BE36FE031E9"), true },
		{ TEXT("Private/ToonProfile.usf"), TEXT("98F1AE9310DF5D1F5ACE677E672C2443F375C8CC"), true },
		{ TEXT("Private/DiffuseIndirectComposite.usf"), TEXT("69B39A99AF6AFDA56A4CA10439A865D4EA4DDAC2"), true },
		{ TEXT("Private/Substrate/SubstrateDeferredLighting.ush"), TEXT("E4F804515B8BF01EBDBD23D9F2195361D33CC066"), true },
		{ TEXT("Private/ReflectionEnvironmentPixelShader.usf"), TEXT("991181D55BBC50A4DE0169934210159EC1DCCDC2"), true },
		{ TEXT("Private/DeferredLightingCommon.ush"), TEXT("3748B2D8EC74830208410121A373BE1B003214BB"), true },
		{ TEXT("Private/ShadingModels.ush"), TEXT("EC8761898EAAF49FC8077266C3498F63508E5456"), true },
		{ TEXT("Private/ClusteredDeferredShadingPixelShader.usf"), TEXT("2D2DCA949116D453ECBEE7138E43F2A8AE8305D1"), true },
		{ TEXT("Source/Runtime/Engine/Private/Rendering/ToonProfile.cpp"), TEXT("D53A66718C5C09DDB74687FBB8F2D14A49BA07F1"), false },
	};

	FString MMDEndfieldBackendCanonicalPath(const FString& Path)
	{
		FString Result = FPaths::ConvertRelativePathToFull(Path);
		FPaths::NormalizeFilename(Result);
		if (!FPaths::CollapseRelativeDirectories(Result)) return FString();
		return Result;
	}

	FString MMDEndfieldBackendHashBytes(const void* Data, uint64 Size)
	{
		return FSHA1::HashBuffer(Data, Size).ToString().ToUpper();
	}

	bool MMDEndfieldBackendNoSymlinkAncestors(const FString& Path)
	{
		// 不能让 Intermediate / Saved 的目录链接把写入重定向到引擎目录。
		FString Current = Path;
		while (!Current.IsEmpty())
		{
			if (FPlatformFileManager::Get().GetPlatformFile().IsSymlink(*Current) == ESymlinkResult::Symlink) return false;
			const FString Parent = FPaths::GetPath(Current);
			if (Parent.IsEmpty() || Parent == Current) break;
			Current = Parent;
		}
		return true;
	}

	FString MMDEndfieldBackendHashString(const FString& Value)
	{
		FTCHARToUTF8 Utf8(*Value);
		return MMDEndfieldBackendHashBytes(Utf8.Get(), static_cast<uint64>(Utf8.Length()));
	}

	FString MMDEndfieldBackendResolveSource(const FString& Key, const FString& EngineRoot, const FString& ShaderRoot)
	{
		return MMDEndfieldBackendCanonicalPath(FPaths::Combine(Key.StartsWith(TEXT("Source/")) ? EngineRoot : ShaderRoot, Key));
	}

	FString MMDEndfieldBackendQuote(const FString& Value)
	{
		FString Result(TEXT("\""));
		for (TCHAR Character : Value)
		{
			switch (Character)
			{
			case TEXT('"'): Result += TEXT("\\\""); break;
			case TEXT('\\'): Result += TEXT("\\\\"); break;
			case TEXT('\n'): Result += TEXT("\\n"); break;
			case TEXT('\r'): Result += TEXT("\\r"); break;
			case TEXT('\t'): Result += TEXT("\\t"); break;
			default:
				if (Character < 0x20) Result += FString::Printf(TEXT("\\u%04x"), static_cast<uint32>(Character));
				else Result.AppendChar(Character);
				break;
			}
		}
		return Result + TEXT("\"");
	}

	void MMDEndfieldBackendWriteManifest()
	{
		auto& Status = MMDEndfieldBackendStatus;
		if (!Status.bRequested) return;
		const FString SavedRoot = MMDEndfieldBackendCanonicalPath(FPaths::ProjectSavedDir());
		const FString Reports = MMDEndfieldBackendCanonicalPath(FPaths::Combine(SavedRoot, TEXT("MMDEndfieldNativeBackend/Reports")));
		if (SavedRoot.IsEmpty() || Reports.IsEmpty() || !FPaths::IsUnderDirectory(Reports, SavedRoot) || !MMDEndfieldBackendNoSymlinkAncestors(Reports)) return;
		Status.ManifestPath = FPaths::Combine(Reports, FString::Printf(TEXT("backend-%u.json"), FPlatformProcess::GetCurrentProcessId()));
		if (!IFileManager::Get().MakeDirectory(*Reports, true))
		{
			UE_LOG(LogMMDEndfieldNativeBackend, Warning, TEXT("Cannot create backend manifest directory: %s"), *Reports);
			return;
		}
		const auto Boolean = [](bool Value) { return Value ? TEXT("true") : TEXT("false"); };
		FString Json = TEXT("{\n  \"hash_algorithm\": \"sha1\",\n  \"patch_version\": ") + MMDEndfieldBackendQuote(MMDEndfieldBackendPatchVersion);
		Json += FString::Printf(TEXT(",\n  \"requested\": %s,\n  \"active\": %s,\n  \"cooked_data\": %s,\n  \"profile_marker_allowed\": %s,\n  \"engine_sources_verified\": %s,\n  \"engine_sources_unchanged\": %s,\n  \"mode\": %d,\n  \"indirect_diffuse_scale\": %.9f,\n  \"indirect_specular_scale\": %.9f"),
			Boolean(Status.bRequested), Boolean(Status.bActive), Boolean(Status.bCookedData), Boolean(Status.bProfileMarkerAllowed),
			Boolean(Status.bEngineSourcesVerified), Boolean(Status.bEngineSourcesUnchanged), Status.Mode, Status.IndirectDiffuseScale, Status.IndirectSpecularScale);
		Json += TEXT(",\n  \"engine_version\": ") + MMDEndfieldBackendQuote(Status.EngineVersion);
		Json += TEXT(",\n  \"signature\": ") + MMDEndfieldBackendQuote(Status.Signature);
		Json += TEXT(",\n  \"overlay_directory\": ") + MMDEndfieldBackendQuote(Status.OverlayDirectory);
		Json += TEXT(",\n  \"virtual_directory\": ") + MMDEndfieldBackendQuote(Status.VirtualDirectory);
		Json += TEXT(",\n  \"plugin_shader_hash\": ") + MMDEndfieldBackendQuote(Status.PluginShaderHash);
		Json += TEXT(",\n  \"patched_evaluation_hash\": ") + MMDEndfieldBackendQuote(Status.PatchedEvaluationHash);
		Json += TEXT(",\n  \"patched_indirect_hash\": ") + MMDEndfieldBackendQuote(Status.PatchedIndirectHash);
		for (const auto& Map : { TPair<FString, const TMap<FString, FString>*>(TEXT("patched_source_hashes"), &Status.PatchedSourceHashes), {TEXT("plugin_source_hashes"), &Status.PluginSourceHashes} })
		{
			Json += TEXT(",\n  ") + MMDEndfieldBackendQuote(Map.Key) + TEXT(": {");
			TArray<FString> MapKeys;
			Map.Value->GetKeys(MapKeys);
			MapKeys.Sort();
			for (int32 Index = 0; Index < MapKeys.Num(); ++Index)
				Json += (Index ? TEXT(",\n    ") : TEXT("\n    ")) + MMDEndfieldBackendQuote(MapKeys[Index]) + TEXT(": ") + MMDEndfieldBackendQuote((*Map.Value)[MapKeys[Index]]);
			Json += TEXT("\n  }");
		}
		Json += TEXT(",\n  \"reason\": ") + MMDEndfieldBackendQuote(Status.Reason);
		Json += TEXT(",\n  \"profile_isolation\": \"Only plugin-marked Endfield profiles use custom response and environment scales; all other Toon profiles evaluate native formulas.\"");
		Json += TEXT(",\n  \"control_source\": \"Mode and indirect scales are encoded in marked profile alpha; the native shader kernel is shared across those controls.\"");
		Json += TEXT(",\n  \"cook_contract\": \"Enable this plugin backend during Cook. Packaged runtime cannot install a source mapping or prove backend presence from startup flags. Runtime profile routing can update profile alpha controls; behavior requires image comparison and Cook provenance.\"");
		Json += TEXT(",\n  \"source_hashes\": {");
		TArray<FString> Keys;
		Status.SourceHashes.GetKeys(Keys);
		Keys.Sort();
		for (int32 Index = 0; Index < Keys.Num(); ++Index)
		{
			Json += Index == 0 ? TEXT("\n    ") : TEXT(",\n    ");
			Json += MMDEndfieldBackendQuote(Keys[Index]) + TEXT(": ") + MMDEndfieldBackendQuote(Status.SourceHashes[Keys[Index]]);
		}
		Json += TEXT("\n  },\n  \"guards\": [");
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(MMDEndfieldBackendGuards); ++Index)
		{
			const auto& Guard = MMDEndfieldBackendGuards[Index];
			Json += Index == 0 ? TEXT("\n    {") : TEXT(",\n    {");
			Json += TEXT("\"path\": ") + MMDEndfieldBackendQuote(Guard.RelativePath);
			Json += TEXT(", \"expected\": ") + MMDEndfieldBackendQuote(Guard.ExpectedHash);
			Json += FString::Printf(TEXT(", \"required\": %s, \"verified\": %s}"), Boolean(Guard.bRequired), Boolean(Status.SourceHashes.Contains(Guard.RelativePath) && Status.SourceHashes[Guard.RelativePath] == Guard.ExpectedHash));
		}
		Json += TEXT("\n  ]\n}\n");
		if (!FFileHelper::SaveStringToFile(Json, *Status.ManifestPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			UE_LOG(LogMMDEndfieldNativeBackend, Warning, TEXT("Cannot save backend manifest: %s"), *Status.ManifestPath);
	}

	bool MMDEndfieldBackendFail(const FString& Reason)
	{
		MMDEndfieldBackendStatus.bActive = false;
		MMDEndfieldBackendStatus.bProfileMarkerAllowed = false;
		MMDEndfieldBackendStatus.Reason = Reason;
		UE_LOG(LogMMDEndfieldNativeBackend, Warning, TEXT("Native backend disabled: %s"), *Reason);
		MMDEndfieldBackendWriteManifest();
		return false;
	}

	bool MMDEndfieldBackendParseMode(const FString& Text, int32& OutMode)
	{
		const FString Clean = Text.TrimStartAndEnd();
		if (Clean.Len() != 1 || Clean[0] < TEXT('0') || Clean[0] > TEXT('2')) return false;
		OutMode = Clean[0] - TEXT('0');
		return true;
	}

	bool MMDEndfieldBackendParseScale(const FString& Text, float& OutScale)
	{
		const FString Clean = Text.TrimStartAndEnd();
		if (Clean.IsEmpty()) return false;
		int32 Index = 0;
		if (Clean[Index] == TEXT('+') || Clean[Index] == TEXT('-')) ++Index;
		bool bHasDigit = false;
		while (Index < Clean.Len() && FChar::IsDigit(Clean[Index])) { bHasDigit = true; ++Index; }
		if (Index < Clean.Len() && Clean[Index] == TEXT('.'))
		{
			++Index;
			while (Index < Clean.Len() && FChar::IsDigit(Clean[Index])) { bHasDigit = true; ++Index; }
		}
		if (!bHasDigit) return false;
		if (Index < Clean.Len() && (Clean[Index] == TEXT('e') || Clean[Index] == TEXT('E')))
		{
			++Index;
			if (Index < Clean.Len() && (Clean[Index] == TEXT('+') || Clean[Index] == TEXT('-'))) ++Index;
			const int32 ExponentStart = Index;
			while (Index < Clean.Len() && FChar::IsDigit(Clean[Index])) ++Index;
			if (Index == ExponentStart) return false;
		}
		if (Index != Clean.Len()) return false;
		const double Parsed = FCString::Atod(*Clean);
		if (!FMath::IsFinite(Parsed) || Parsed < 0.0 || Parsed > 8.0) return false;
		OutScale = static_cast<float>(Parsed);
		return true;
	}

	bool MMDEndfieldBackendReadOptions()
	{
		auto& Status = MMDEndfieldBackendStatus;
		FString Mode(TEXT("2")), Diffuse(TEXT("1")), Specular(TEXT("1"));
		if (GConfig)
		{
			for (const FString& Config : { GEngineIni, GGameIni })
			{
				bool Enabled = false;
				if (GConfig->GetBool(MMDEndfieldBackendSection, TEXT("Enabled"), Enabled, Config)) Status.bRequested = Enabled;
				GConfig->GetString(MMDEndfieldBackendSection, TEXT("ProbeMode"), Mode, Config);
				GConfig->GetString(MMDEndfieldBackendSection, TEXT("IndirectDiffuseScale"), Diffuse, Config);
				GConfig->GetString(MMDEndfieldBackendSection, TEXT("IndirectSpecularScale"), Specular, Config);
			}
		}
		Status.bRequested |= FParse::Param(FCommandLine::Get(), TEXT("MMDEndfieldNativeBackend"));
		if (!Status.bRequested) return true;
		FParse::Value(FCommandLine::Get(), TEXT("MMDEndfieldNativeBackendMode="), Mode);
		FParse::Value(FCommandLine::Get(), TEXT("MMDEndfieldNativeIndirectDiffuseScale="), Diffuse);
		FParse::Value(FCommandLine::Get(), TEXT("MMDEndfieldNativeIndirectSpecularScale="), Specular);
		if (!MMDEndfieldBackendParseMode(Mode, Status.Mode)) return MMDEndfieldBackendFail(TEXT("Mode must be exactly 0, 1, or 2."));
		if (!MMDEndfieldBackendParseScale(Diffuse, Status.IndirectDiffuseScale)) return MMDEndfieldBackendFail(TEXT("IndirectDiffuseScale must be finite and within [0, 8]."));
		if (!MMDEndfieldBackendParseScale(Specular, Status.IndirectSpecularScale)) return MMDEndfieldBackendFail(TEXT("IndirectSpecularScale must be finite and within [0, 8]."));
		return true;
	}

	bool MMDEndfieldBackendSingleAnchor(const FString& Text, const FString& Anchor, int32& OutIndex)
	{
		OutIndex = Text.Find(Anchor, ESearchCase::CaseSensitive);
		return OutIndex != INDEX_NONE && Text.Find(Anchor, ESearchCase::CaseSensitive, ESearchDir::FromStart, OutIndex + Anchor.Len()) == INDEX_NONE;
	}

	bool MMDEndfieldBackendPatchEvaluation(FString& Text)
	{
		const FString Newline = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
		const FString FunctionAnchor = TEXT("FSubstrateEvaluateResult SubstrateEvaluateBSDFCommon(FSubstrateBSDFContext BSDFContext, FShadowTerms ShadowTerms, FAreaLight AreaLight, FSubstrateIntegrationSettings Settings, int IntegrationType)");
		const FString CallAnchor = TEXT("FSubstrateEvaluateToonResult ToonResult = SubstrateToonEvaluateCommon(");
		const FString CallEndAnchor = TEXT("BSDFContext.N, BSDFContext.V, AreaLight, ShadowTerms.SurfaceShadow, ShadowTerms.TransmittanceOrOpticalThickness);");
		const FString EnvironmentAnchor = TEXT("SpecEnvBRDF * ToonProfileGISettings.SpecularIndirectScale;");
		int32 FunctionIndex, CallIndex, CallEndIndex, EnvironmentIndex;
		if (!MMDEndfieldBackendSingleAnchor(Text, FunctionAnchor, FunctionIndex)
			|| !MMDEndfieldBackendSingleAnchor(Text, CallAnchor, CallIndex)
			|| !MMDEndfieldBackendSingleAnchor(Text, CallEndAnchor, CallEndIndex)
			|| !MMDEndfieldBackendSingleAnchor(Text, EnvironmentAnchor, EnvironmentIndex)
			|| CallEndIndex <= CallIndex) return false;
		const int32 EnvironmentColorIndex = Text.Find(TEXT("SubstrateEnvLightResult.SpecularColor"), ESearchCase::CaseSensitive, ESearchDir::FromStart, EnvironmentIndex);
		const int32 EnvironmentEndIndex = EnvironmentColorIndex == INDEX_NONE ? INDEX_NONE : Text.Find(TEXT(";"), ESearchCase::CaseSensitive, ESearchDir::FromStart, EnvironmentColorIndex);
		if (EnvironmentEndIndex == INDEX_NONE || EnvironmentEndIndex - EnvironmentIndex > 400
			|| !Text.Mid(EnvironmentColorIndex, EnvironmentEndIndex - EnvironmentColorIndex).Contains(TEXT("F0"))) return false;

		// 逆序替换，保证位置仍对应已核验的原始文件。
		const FString CallPatch = TEXT("TMMDEndfieldNativeBSDF mmd_endfield_bsdf;") + Newline
			+ TEXT("\t\t\tFSubstrateEvaluateToonResult ToonResult = mmd_endfield_bsdf.EvaluatePerLight(BSDFContext, ShadowTerms, AreaLight, bIsRectLight, bThisPassAppliesDiffuseRamp, 0u);");
		Text.RemoveAt(CallIndex, CallEndIndex + CallEndAnchor.Len() - CallIndex);
		Text.InsertAt(CallIndex, CallPatch);
		Text.InsertAt(FunctionIndex, TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeBSDF.ush\"") + Newline
			+ TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeIndirect.ush\"") + Newline + Newline);
		int32 ContextIndex;
		if (!MMDEndfieldBackendSingleAnchor(Text, TEXT("struct FSubstrateBSDFContext"), ContextIndex)) return false;
		const int32 ContextOpen = Text.Find(TEXT("{"), ESearchCase::CaseSensitive, ESearchDir::FromStart, ContextIndex);
		if (ContextOpen == INDEX_NONE || ContextOpen - ContextIndex > 100) return false;
		Text.InsertAt(ContextOpen + 1, Newline + TEXT("\tTMMDEndfieldNativeLightData mmd_light_data;"));
		Text.InsertAt(ContextIndex, TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeData.ush\"") + Newline + Newline);
		return true;
	}

	bool MMDEndfieldBackendPatchDeferred(FString& Text)
	{
		const FString Newline = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
		int32 FunctionIndex = INDEX_NONE, CaptureIndex = INDEX_NONE;
		if (!MMDEndfieldBackendSingleAnchor(Text, TEXT("FSubstrateDeferredLighting SubstrateDeferredLighting("), FunctionIndex)
			|| !MMDEndfieldBackendSingleAnchor(Text, TEXT("FShadowTerms BSDFShadowTerms = ShadowTerms;"), CaptureIndex)) return false;
		const FString Capture = TEXT("TMMDEndfieldNativeLightCapture mmd_capture;") + Newline
			+ TEXT("\t\tBSDFContext.mmd_light_data = mmd_capture.Build(LightData, L, ToLight, LightMask, SubstrateShadowTermInputParameters.LightAttenuation, SubstrateShadowTermInputParameters.PrecomputedShadowFactors, SubstrateShadowTermInputParameters.TranslatedWorldPosition, SubstrateShadowTermInputParameters.SceneDepth, SubstrateShadowTermInputParameters.Dither, SubstrateShadowTermInputParameters.bEvaluateShadowTerm, SubstrateShadowTermInputParameters.bUseShadowTermHairTransmittance);") + Newline + TEXT("\t\t");
		Text.InsertAt(CaptureIndex, Capture);
		Text.InsertAt(FunctionIndex, TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeLightCapture.ush\"") + Newline + Newline);
		return true;
	}

	#include "MMDEndfieldNativeIndirectPatch.inl"

	bool MMDEndfieldBackendPatchClustered(FString& Text)
	{
		const FString Newline = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
		const FString IncludeAnchor = TEXT("#include \"Substrate/SubstrateDeferredLighting.ush\"");
		const FString CallAnchor = TEXT("DirectLighting += GetDynamicLighting(TranslatedWorldPosition, CameraVector, ScreenSpaceData.GBuffer, ScreenSpaceData.AmbientOcclusion, LightData,LightAttenuation, Dither, uint2(0, 0), SurfaceShadow);");
		int32 IncludeIndex, CallIndex;
		if (!MMDEndfieldBackendSingleAnchor(Text, IncludeAnchor, IncludeIndex)
			|| !MMDEndfieldBackendSingleAnchor(Text, CallAnchor, CallIndex)) return false;
		const FString Patch = TEXT("#if SUBSTRATE_ENABLED") + Newline
			+ TEXT("\t\tTMMDEndfieldNativeLegacy mmd_legacy;") + Newline
			+ TEXT("\t\tDirectLighting += mmd_legacy.EvaluateDynamicLighting(TranslatedWorldPosition, CameraVector, ScreenSpaceData.GBuffer, ScreenSpaceData.AmbientOcclusion, LightData, LightAttenuation, Dither, uint2(0, 0), SurfaceShadow);") + Newline
			+ TEXT("\t\t#else") + Newline + TEXT("\t\t") + CallAnchor + Newline + TEXT("\t\t#endif");
		Text.RemoveAt(CallIndex, CallAnchor.Len());
		Text.InsertAt(CallIndex, Patch);
		Text.InsertAt(IncludeIndex + IncludeAnchor.Len(), Newline + TEXT("#if SUBSTRATE_ENABLED") + Newline
			+ TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeLegacy.ush\"") + Newline + TEXT("#endif"));
		// 引擎在 Substrate 下选择 TileVS；传统入口也须消费它的 float2 UV，而非旧 VS 的 float4。
		const FString SignatureAnchor = TEXT("in noperspective float4 UVAndScreenPos : TEXCOORD0,");
		const FString SignaturePatch = TEXT("#if SUBSTRATE_ENABLED") + Newline
			+ TEXT("\tin float2 mmd_tile_screen_uv : TEXCOORD0,") + Newline
			+ TEXT("\tin float3 mmd_tile_screen_vector : TEXCOORD1,") + Newline
			+ TEXT("#else") + Newline + TEXT("\t") + SignatureAnchor + Newline + TEXT("#endif");
		const FString CoordinateAnchor = TEXT("float2 ScreenUV = UVAndScreenPos.xy;") + Newline + TEXT("\tfloat2 ScreenPosition = UVAndScreenPos.zw;");
		const FString CoordinatePatch = TEXT("#if SUBSTRATE_ENABLED") + Newline
			+ TEXT("\tfloat2 ScreenUV = mmd_tile_screen_uv;") + Newline
			+ TEXT("\tfloat2 ScreenPosition = ViewportUVToScreenPos((SvPosition.xy - View.ViewRectMin.xy) * View.ViewSizeAndInvSize.zw);") + Newline
			+ TEXT("#else") + Newline + TEXT("\t") + CoordinateAnchor + Newline + TEXT("#endif");
		return MMDEndfieldBackendReplaceIndirectAnchor(Text, SignatureAnchor, SignaturePatch)
			&& MMDEndfieldBackendReplaceIndirectAnchor(Text, CoordinateAnchor, CoordinatePatch);
	}

	bool MMDEndfieldBackendPublishFile(const FString& Destination, const TArray<uint8>& Bytes, const FString& ExpectedHash, FString& OutReason)
	{
		FString Hash;
		if (MMDEndfieldNativeBackend::ComputeFileHash(Destination, Hash))
		{
			if (Hash == ExpectedHash) return true;
			OutReason = TEXT("Existing file differs inside the immutable overlay signature: ") + Destination;
			return false;
		}
		if (IFileManager::Get().FileExists(*Destination))
		{
			OutReason = TEXT("Existing overlay file cannot be read: ") + Destination;
			return false;
		}
		// 同目录完整临时文件再发布；永不先写回原始 Evaluation，也不删既有目标。
		const FString Temporary = Destination + FString::Printf(TEXT(".%u.%s.tmp"), FPlatformProcess::GetCurrentProcessId(), *FGuid::NewGuid().ToString(EGuidFormats::Digits));
		if (!MMDEndfieldBackendNoSymlinkAncestors(Temporary) || !FFileHelper::SaveArrayToFile(Bytes, *Temporary)
			|| !MMDEndfieldNativeBackend::ComputeFileHash(Temporary, Hash) || Hash != ExpectedHash)
		{
			OutReason = TEXT("Cannot write and verify complete temporary overlay file: ") + Temporary;
			return false;
		}
		if (FPlatformFileManager::Get().GetPlatformFile().MoveFile(*Destination, *Temporary))
		{
			if (MMDEndfieldNativeBackend::ComputeFileHash(Destination, Hash) && Hash == ExpectedHash) return true;
			OutReason = TEXT("Published overlay file failed hash verification: ") + Destination;
			return false;
		}
		if (MMDEndfieldNativeBackend::ComputeFileHash(Destination, Hash) && Hash == ExpectedHash)
		{
			// 另一进程已发布相同文件，只清理本进程的已核验临时文件。
			IFileManager::Get().Delete(*Temporary, false, false, true);
			return true;
		}
		OutReason = TEXT("Cannot atomically publish overlay file, or another process published different content: ") + Destination;
		return false;
	}

	bool MMDEndfieldBackendGenerateOverlay()
	{
		auto& Status = MMDEndfieldBackendStatus;
		const FString EngineRoot = MMDEndfieldBackendCanonicalPath(FPaths::EngineDir());
		const FString ShaderRoot = MMDEndfieldBackendCanonicalPath(FPaths::Combine(EngineRoot, TEXT("Shaders")));
		const FString SourceDirectory = MMDEndfieldBackendCanonicalPath(FPaths::Combine(ShaderRoot, TEXT("Private")));
		const FString IntermediateRoot = MMDEndfieldBackendCanonicalPath(FPaths::ProjectIntermediateDir());
		const FString CacheRoot = MMDEndfieldBackendCanonicalPath(FPaths::Combine(IntermediateRoot, TEXT("MMDEndfieldNativeBackend")));
		if (EngineRoot.IsEmpty() || ShaderRoot.IsEmpty() || SourceDirectory.IsEmpty() || IntermediateRoot.IsEmpty() || CacheRoot.IsEmpty()
			|| !FPaths::IsUnderDirectory(SourceDirectory, ShaderRoot) || !FPaths::IsUnderDirectory(CacheRoot, IntermediateRoot)
			|| FPaths::IsUnderDirectory(CacheRoot, EngineRoot) || !MMDEndfieldBackendNoSymlinkAncestors(CacheRoot) || !IFileManager::Get().DirectoryExists(*SourceDirectory))
			return MMDEndfieldBackendFail(TEXT("Unsafe or missing engine source / project Intermediate directory."));
		for (const auto& Mapping : AllShaderSourceDirectoryMappings())
		{
			if (Mapping.Key.Equals(MMDEndfieldBackendVirtualRoot, ESearchCase::IgnoreCase) || Mapping.Key.StartsWith(FString(MMDEndfieldBackendVirtualRoot) + TEXT("/")))
				return MMDEndfieldBackendFail(TEXT("/Engine/Private or a deeper directory already has a competing shader mapping; restart without it."));
		}
		if (IFileManager::Get().DirectoryExists(*FPaths::Combine(SourceDirectory, TEXT("Generated"))))
			return MMDEndfieldBackendFail(TEXT("Engine Private source contains reserved Generated directory; overlay mapping cannot be installed."));
		for (const auto& Guard : MMDEndfieldBackendGuards)
		{
			const FString Source = MMDEndfieldBackendResolveSource(Guard.RelativePath, EngineRoot, ShaderRoot);
			if (Source.IsEmpty() || !FPaths::IsUnderDirectory(Source, EngineRoot)) return MMDEndfieldBackendFail(TEXT("Source guard path escapes engine root."));
			if (!Guard.bRequired && !IFileManager::Get().FileExists(*Source)) continue;
			FString Hash;
			if (!MMDEndfieldNativeBackend::ComputeFileHash(Source, Hash) || Hash != Guard.ExpectedHash)
				return MMDEndfieldBackendFail(FString::Printf(TEXT("UE 5.8.3 source guard failed: %s (SHA1 %s)"), Guard.RelativePath, *Hash));
			Status.SourceHashes.Add(Guard.RelativePath, Hash);
		}
		Status.bEngineSourcesVerified = true;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
		if (!Plugin.IsValid()) return MMDEndfieldBackendFail(TEXT("Ue5MMDTools plugin directory was not found."));
		const FString PluginShaderRoot = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
        const TCHAR* PluginSources[] = { TEXT("Core/TMMDEndfieldNativeBSDF.ush"), TEXT("Core/TMMDEndfieldNativeData.ush"),
            TEXT("Core/TMMDEndfieldNativeLightCapture.ush"), TEXT("Core/TMMDEndfieldNativeIndirect.ush"), TEXT("Core/TMMDEndfieldNativeLegacy.ush") };
        FString PluginSignature;
        for (const TCHAR* Relative : PluginSources)
        {
            FString Hash;
            if (!MMDEndfieldNativeBackend::ComputeFileHash(FPaths::Combine(PluginShaderRoot, Relative), Hash)) return MMDEndfieldBackendFail(TEXT("Plugin native shader could not be read."));
            Status.PluginSourceHashes.Add(Relative, Hash);
            PluginSignature += FString(Relative) + TEXT("=") + Hash + TEXT("\n");
        }
        Status.PluginShaderHash = MMDEndfieldBackendHashString(PluginSignature);
		TArray<FString> SourceFiles;
		IFileManager::Get().FindFilesRecursive(SourceFiles, *SourceDirectory, TEXT("*"), true, false);
		SourceFiles.Sort();
		if (SourceFiles.IsEmpty()) return MMDEndfieldBackendFail(TEXT("Engine Private shader source tree is empty."));
		TMap<FString, FString> RelativeFiles;
		for (const FString& File : SourceFiles)
		{
			const FString Canonical = MMDEndfieldBackendCanonicalPath(File);
			if (Canonical.IsEmpty() || !FPaths::IsUnderDirectory(Canonical, SourceDirectory)) return MMDEndfieldBackendFail(TEXT("Private shader source file escapes its directory."));
			FString Relative = Canonical;
			if (!FPaths::MakePathRelativeTo(Relative, *(SourceDirectory + TEXT("/"))) || Relative.StartsWith(TEXT(".."))) return MMDEndfieldBackendFail(TEXT("Cannot derive safe Private shader relative path."));
			FString Hash;
			if (!MMDEndfieldNativeBackend::ComputeFileHash(Canonical, Hash)) return MMDEndfieldBackendFail(TEXT("Cannot hash complete Private shader tree."));
			Status.SourceHashes.Add(TEXT("Private/") + Relative, Hash);
			RelativeFiles.Add(Relative, Canonical);
		}
		for (const auto& Guard : MMDEndfieldBackendGuards)
		{
			const FString* Hash = Status.SourceHashes.Find(Guard.RelativePath);
			if ((Guard.bRequired && !Hash) || (Hash && *Hash != Guard.ExpectedHash))
				return MMDEndfieldBackendFail(TEXT("Guarded engine source changed while enumerating the Private shader tree."));
		}
        TMap<FString, TArray<uint8>> PatchedFiles;
        const auto PreparePatch = [&](const FString& Relative, bool (*Patch)(FString&))
        {
            FString Text;
            if (!FFileHelper::LoadFileToString(Text, *FPaths::Combine(SourceDirectory, Relative)) || !Patch(Text)) return false;
            FTCHARToUTF8 Utf8(*Text);
            TArray<uint8>& Bytes = PatchedFiles.Add(Relative);
            Bytes.Append(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
            Status.PatchedSourceHashes.Add(Relative, MMDEndfieldBackendHashBytes(Bytes.GetData(), static_cast<uint64>(Bytes.Num())));
            return true;
        };
        if (!PreparePatch(TEXT("Substrate/SubstrateEvaluation.ush"), MMDEndfieldBackendPatchEvaluation)
            || !PreparePatch(TEXT("Substrate/SubstrateDeferredLighting.ush"), MMDEndfieldBackendPatchDeferred)
            || !PreparePatch(TEXT("DiffuseIndirectComposite.usf"), MMDEndfieldBackendPatchIndirect)
            || !PreparePatch(TEXT("ReflectionEnvironmentPixelShader.usf"), MMDEndfieldBackendPatchReflection)
            || !PreparePatch(TEXT("ClusteredDeferredShadingPixelShader.usf"), MMDEndfieldBackendPatchClustered))
            return MMDEndfieldBackendFail(TEXT("Exact native direct/indirect patch anchor was not found once."));
        Status.PatchedEvaluationHash = Status.PatchedSourceHashes[TEXT("Substrate/SubstrateEvaluation.ush")];
        Status.PatchedIndirectHash = Status.PatchedSourceHashes[TEXT("DiffuseIndirectComposite.usf")];
		TArray<FString> Keys;
		Status.SourceHashes.GetKeys(Keys);
		Keys.Sort();
		FString SignatureInput = FString::Printf(TEXT("%s\n%s\n%s\n"), MMDEndfieldBackendPatchVersion, *Status.EngineVersion, *Status.PluginShaderHash);
        TArray<FString> PatchedKeys;
        Status.PatchedSourceHashes.GetKeys(PatchedKeys);
        PatchedKeys.Sort();
        for (const FString& Key : PatchedKeys) SignatureInput += Key + TEXT("=") + Status.PatchedSourceHashes[Key] + TEXT("\n");
		for (const FString& Key : Keys) SignatureInput += Key + TEXT("=") + Status.SourceHashes[Key] + TEXT("\n");
		Status.Signature = MMDEndfieldBackendHashString(SignatureInput);
		Status.OverlayDirectory = MMDEndfieldBackendCanonicalPath(FPaths::Combine(CacheRoot, Status.Signature, TEXT("Private")));
		if (Status.OverlayDirectory.IsEmpty() || !FPaths::IsUnderDirectory(Status.OverlayDirectory, CacheRoot) || FPaths::IsUnderDirectory(Status.OverlayDirectory, EngineRoot) || !MMDEndfieldBackendNoSymlinkAncestors(Status.OverlayDirectory)) return MMDEndfieldBackendFail(TEXT("Generated overlay path escapes project cache or contains a directory link."));
		TArray<FString> RelativeKeys;
		RelativeFiles.GetKeys(RelativeKeys);
		RelativeKeys.Sort();
		for (const FString& Relative : RelativeKeys)
		{
			const FString Destination = MMDEndfieldBackendCanonicalPath(FPaths::Combine(Status.OverlayDirectory, Relative));
			if (Destination.IsEmpty() || !FPaths::IsUnderDirectory(Destination, Status.OverlayDirectory) || !MMDEndfieldBackendNoSymlinkAncestors(Destination)) return MMDEndfieldBackendFail(TEXT("Generated shader file escapes overlay directory or contains a link."));
			if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(Destination), true)) return MMDEndfieldBackendFail(TEXT("Cannot create Private shader overlay directory."));
            const TArray<uint8>* PatchedBytes = PatchedFiles.Find(Relative);
            const FString ExpectedHash = PatchedBytes ? Status.PatchedSourceHashes[Relative] : Status.SourceHashes[TEXT("Private/") + Relative];
            TArray<uint8> OriginalBytes;
            if (!PatchedBytes && (!FFileHelper::LoadFileToArray(OriginalBytes, *RelativeFiles[Relative])
                || MMDEndfieldBackendHashBytes(OriginalBytes.GetData(), static_cast<uint64>(OriginalBytes.Num())) != ExpectedHash))
                return MMDEndfieldBackendFail(TEXT("Engine source changed before mirroring a Private shader file."));
			FString PublishReason;
			const TArray<uint8>& TargetBytes = PatchedBytes ? *PatchedBytes : OriginalBytes;
			if (!MMDEndfieldBackendPublishFile(Destination, TargetBytes, ExpectedHash, PublishReason)) return MMDEndfieldBackendFail(PublishReason);
		}
        for (const FString& Key : PatchedKeys)
        {
            FString Hash;
            if (!MMDEndfieldNativeBackend::ComputeFileHash(FPaths::Combine(Status.OverlayDirectory, Key), Hash) || Hash != Status.PatchedSourceHashes[Key])
                return MMDEndfieldBackendFail(TEXT("Patched shader round-trip hash validation failed."));
        }
		for (const FString& Key : Keys)
		{
			FString Hash;
			if (!MMDEndfieldNativeBackend::ComputeFileHash(MMDEndfieldBackendResolveSource(Key, EngineRoot, ShaderRoot), Hash) || Hash != Status.SourceHashes[Key])
				return MMDEndfieldBackendFail(TEXT("Engine source changed during adapter generation; mapping was not installed."));
		}
        for (const auto& Source : Status.PluginSourceHashes)
        {
            FString Hash;
            if (!MMDEndfieldNativeBackend::ComputeFileHash(FPaths::Combine(PluginShaderRoot, Source.Key), Hash) || Hash != Source.Value)
                return MMDEndfieldBackendFail(TEXT("Plugin shader changed during adapter generation; retry in a fresh process."));
        }
		Status.bEngineSourcesUnchanged = true;
		AddShaderSourceDirectoryMapping(MMDEndfieldBackendVirtualRoot, Status.OverlayDirectory);
		const FString* Registered = AllShaderSourceDirectoryMappings().Find(MMDEndfieldBackendVirtualRoot);
		if (!Registered || !FPaths::IsSamePath(*Registered, Status.OverlayDirectory)) return MMDEndfieldBackendFail(TEXT("Private shader overlay mapping was not registered."));
		Status.bActive = true;
		Status.bProfileMarkerAllowed = true;
		Status.Reason = TEXT("Source overlay registered. Only marked Endfield profiles use the plugin response; shader compilation/readback is verified separately by the probe.");
		UE_LOG(LogMMDEndfieldNativeBackend, Display, TEXT("Native backend mapped (mode=%d, GI diffuse=%.3f specular=%.3f): %s"), Status.Mode, Status.IndirectDiffuseScale, Status.IndirectSpecularScale, *Status.OverlayDirectory);
		MMDEndfieldBackendWriteManifest();
		return true;
	}
}

namespace MMDEndfieldNativeBackend
{
	bool ComputeFileHash(const FString& Path, FString& OutHash)
	{
		OutHash.Reset();
		if (!IFileManager::Get().FileExists(*Path)) return false;
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
		OutHash = MMDEndfieldBackendHashBytes(Bytes.GetData(), static_cast<uint64>(Bytes.Num()));
		return true;
	}

	const FStatus& GetStatus()
	{
		return MMDEndfieldBackendStatus;
	}

	void Initialize()
	{
		if (MMDEndfieldBackendInitialized) return;
		MMDEndfieldBackendInitialized = true;
		auto& Status = MMDEndfieldBackendStatus;
		Status.bCookedData = FPlatformProperties::RequiresCookedData();
		Status.bCookedRuntime = Status.bCookedData;
		const FEngineVersion& Version = FEngineVersion::Current();
		Status.EngineVersion = Version.ToString();
		if (!MMDEndfieldBackendReadOptions()) return;
		if (!Status.bRequested)
		{
			Status.Reason = TEXT("Disabled by default; enable explicitly through command line or project configuration.");
			return;
		}
		if (!IsInGameThread()) { MMDEndfieldBackendFail(TEXT("Initialize must run on the PostConfigInit game thread.")); return; }
		if (Version.GetMajor() != 5 || Version.GetMinor() != 8 || Version.GetPatch() != 3 || Version.GetChangelist() != 58210709)
		{
			MMDEndfieldBackendFail(FString::Printf(TEXT("Requires UE 5.8.3 CL58210709, found %s."), *Status.EngineVersion));
			return;
		}
		if (Status.bCookedData)
		{
			// 这里只许可 Profile 路由；Cook 产物是否含适配内核需要发行验证，不能在运行时证实。
			Status.bProfileMarkerAllowed = true;
			Status.Reason = TEXT("Cooked runtime requested Endfield profile routing. Source mapping is inactive; mode and GI scales come from marked profile alpha. Backend presence in cooked bytecode remains unverified here.");
			UE_LOG(LogMMDEndfieldNativeBackend, Display, TEXT("%s"), *Status.Reason);
			MMDEndfieldBackendWriteManifest();
			return;
		}
		if (!AllowShaderCompiling()) { MMDEndfieldBackendFail(TEXT("Shader compilation is disabled; source overlay cannot be activated.")); return; }
		MMDEndfieldBackendGenerateOverlay();
	}
}
