// 启动时请求后端；模式与环境倍率写入自有 Profile alpha，shader 从 Profile 读取，无需按模式重编译。
// 用法：-run=MMDEndfieldNativeBackendProbe -OutDir=<Saved目录> -Tag=<前缀> [-Exp=backend,lighting]
// -AllowInactiveBackend 只允许没有请求适配器的原生负对照，其 backend_success 始终为 false。
// 仅写测试报告/截图；沿用 ToonProbe 的只读资产加载和 Transient 场景，不复制测试装置。
#include "MMDEndfieldNativeBackendProbeCommandlet.h"

#include "MMDEndfieldToonProbeCommandlet.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "RenderUtils.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompilerCore.h"
#include "ShaderCore.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	bool EndfieldBackendProbeHashFile(const FString& Path, FString& Hash)
	{
		TArray<uint8> Bytes;
		FSHAHash Signature;
		if (!IFileManager::Get().FileExists(*Path)) return false;
		if (!FFileHelper::LoadFileToArray(Bytes, *Path)) return false;
		FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Signature.Hash);
		Hash = Signature.ToString();
		return true;
	}

	FString EndfieldBackendProbeEngineSourcePath(const FString& Key)
	{
		return Key.StartsWith(TEXT("Source/")) ? FPaths::Combine(FPaths::EngineDir(), Key)
			: FPaths::Combine(FPaths::EngineDir(), TEXT("Shaders"), Key);
	}

	bool EndfieldBackendProbeVerifyEngineSources(const MMDEndfieldNativeBackend::FStatus& Status,
		TArray<FString>& Errors, const TCHAR* Stage)
	{
		bool bUnchanged = !Status.SourceHashes.IsEmpty();
		for (const TPair<FString, FString>& Source : Status.SourceHashes)
		{
			FString Hash;
			const FString Path = EndfieldBackendProbeEngineSourcePath(Source.Key);
			if (!EndfieldBackendProbeHashFile(Path, Hash) || !Hash.Equals(Source.Value, ESearchCase::IgnoreCase))
			{
				Errors.Add(FString::Printf(TEXT("%s：引擎 shader 源文件缺失或已变化：%s"), Stage, *Source.Key));
				bUnchanged = false;
			}
		}
		if (Status.SourceHashes.IsEmpty()) Errors.Add(TEXT("适配器没有提供引擎源码基线 hash"));
		return bUnchanged;
	}

	FString EndfieldBackendProbePluginSourcePath(const FString& Key)
	{
		return FPaths::ConvertRelativePathToFull(GetShaderSourceFilePath(TEXT("/Plugin/Ue5MMDTools/") + Key));
	}

	const TCHAR* EndfieldBackendProbePluginSources[] = { TEXT("Core/TMMDEndfieldNativeBSDF.ush"),
		TEXT("Core/TMMDEndfieldNativeData.ush"), TEXT("Core/TMMDEndfieldNativeLightCapture.ush"),
		TEXT("Core/TMMDEndfieldNativeIndirect.ush"), TEXT("Core/TMMDEndfieldNativeLegacy.ush") };

	bool EndfieldBackendProbeVerifyPluginSources(const MMDEndfieldNativeBackend::FStatus& Status,
		TArray<FString>& Errors, const TCHAR* Stage)
	{
		bool bUnchanged = !Status.PluginSourceHashes.IsEmpty();
		for (const TCHAR* Key : EndfieldBackendProbePluginSources)
		{
			if (!Status.PluginSourceHashes.Contains(Key))
			{
				Errors.Add(FString::Printf(TEXT("%s：缺少插件 shader 源码基线：%s"), Stage, Key));
				bUnchanged = false;
			}
		}
		for (const TPair<FString, FString>& Source : Status.PluginSourceHashes)
		{
			FString Hash;
			if (!EndfieldBackendProbeHashFile(EndfieldBackendProbePluginSourcePath(Source.Key), Hash)
				|| !Hash.Equals(Source.Value, ESearchCase::IgnoreCase))
			{
				Errors.Add(FString::Printf(TEXT("%s：插件 shader 源文件缺失或已变化：%s"), Stage, *Source.Key));
				bUnchanged = false;
			}
		}
		return bUnchanged;
	}

	TSharedRef<FJsonObject> EndfieldBackendProbeStatusJson(const MMDEndfieldNativeBackend::FStatus& Status)
	{
		TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
		Json->SetBoolField(TEXT("requested"), Status.bRequested);
		Json->SetBoolField(TEXT("active"), Status.bActive);
		Json->SetBoolField(TEXT("cooked_runtime"), Status.bCookedRuntime);
		Json->SetBoolField(TEXT("cooked_data"), Status.bCookedData);
		Json->SetBoolField(TEXT("profile_marker_allowed"), Status.bProfileMarkerAllowed);
		Json->SetBoolField(TEXT("engine_sources_verified_at_startup"), Status.bEngineSourcesVerified);
		Json->SetBoolField(TEXT("engine_sources_unchanged_at_startup"), Status.bEngineSourcesUnchanged);
		Json->SetNumberField(TEXT("mode"), Status.Mode);
		Json->SetNumberField(TEXT("indirect_diffuse_scale"), Status.IndirectDiffuseScale);
		Json->SetNumberField(TEXT("indirect_specular_scale"), Status.IndirectSpecularScale);
		Json->SetNumberField(TEXT("requested_mode"), Status.Mode);
		Json->SetNumberField(TEXT("requested_indirect_diffuse_scale"), Status.IndirectDiffuseScale);
		Json->SetNumberField(TEXT("requested_indirect_specular_scale"), Status.IndirectSpecularScale);
		Json->SetStringField(TEXT("kernel_control_source"), TEXT("owned ToonProfile diffuse-ramp alpha metadata; mode/scale fields above are requested defaults, not a GPU readback of every Profile"));
		Json->SetBoolField(TEXT("mode_change_requires_shader_recompile"), false);
		Json->SetStringField(TEXT("engine_version"), Status.EngineVersion);
		Json->SetStringField(TEXT("signature"), Status.Signature);
		Json->SetStringField(TEXT("virtual_directory"), Status.VirtualDirectory);
		Json->SetStringField(TEXT("overlay_directory"), Status.OverlayDirectory);
		Json->SetStringField(TEXT("manifest"), Status.ManifestPath);
		Json->SetStringField(TEXT("plugin_shader_hash"), Status.PluginShaderHash);
		Json->SetStringField(TEXT("patched_evaluation_hash"), Status.PatchedEvaluationHash);
		Json->SetStringField(TEXT("patched_indirect_hash"), Status.PatchedIndirectHash);
		Json->SetStringField(TEXT("reason"), Status.Reason);
		Json->SetStringField(TEXT("hash_algorithm"), TEXT("sha1"));
		TSharedRef<FJsonObject> Sources = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Source : Status.SourceHashes) Sources->SetStringField(Source.Key, Source.Value);
		Json->SetObjectField(TEXT("engine_source_hashes"), Sources);
		TSharedRef<FJsonObject> PatchedSources = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Source : Status.PatchedSourceHashes) PatchedSources->SetStringField(Source.Key, Source.Value);
		Json->SetObjectField(TEXT("patched_source_hashes"), PatchedSources);
		TSharedRef<FJsonObject> PluginSources = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Source : Status.PluginSourceHashes) PluginSources->SetStringField(Source.Key, Source.Value);
		Json->SetObjectField(TEXT("plugin_source_hashes"), PluginSources);
		return Json;
	}

	void EndfieldBackendProbeCheckMapping(const MMDEndfieldNativeBackend::FStatus& Status,
		TSharedRef<FJsonObject> Report, TArray<FString>& Errors, bool bInactiveControl)
	{
		TSharedRef<FJsonObject> Mappings = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& Mapping : AllShaderSourceDirectoryMappings())
			if (Mapping.Key.StartsWith(TEXT("/Engine/Private")) || Mapping.Key.StartsWith(TEXT("/Plugin/Ue5MMDTools")))
				Mappings->SetStringField(Mapping.Key, Mapping.Value);
		Report->SetObjectField(TEXT("registered_mappings"), Mappings);
		Report->SetBoolField(TEXT("shader_source_cache_inspected"), false);
		Report->SetStringField(TEXT("mapping_check_scope"), TEXT("directory resolution and disk SHA1 only; shader source cache is neither loaded nor changed"));
		TArray<TSharedPtr<FJsonValue>> Sources;
		TArray<FString> Keys;
		Status.PatchedSourceHashes.GetKeys(Keys);
		if (!bInactiveControl && Keys.IsEmpty()) Errors.Add(TEXT("适配器没有提供 patched shader 源码 hash 白名单"));
		if (bInactiveControl && Keys.IsEmpty())
		{
			Keys = { TEXT("Substrate/SubstrateEvaluation.ush"), TEXT("Substrate/SubstrateDeferredLighting.ush"),
				TEXT("DiffuseIndirectComposite.usf"), TEXT("ReflectionEnvironmentPixelShader.usf"), TEXT("ClusteredDeferredShadingPixelShader.usf") };
		}
		Keys.AddUnique(TEXT("Substrate/SubstrateToonBSDF.ush"));
		Keys.Sort();
		for (const FString& Key : Keys)
		{
			const FString VirtualSource = Status.VirtualDirectory + TEXT("/") + Key;
			TArray<FShaderCompilerError> SourceErrors;
			const FString PhysicalPath = FPaths::ConvertRelativePathToFull(GetShaderSourceFilePath(*VirtualSource, &SourceErrors));
			FString Hash;
			const bool bMatchesVirtualRoot = !Status.VirtualDirectory.IsEmpty()
				&& VirtualSource.StartsWith(Status.VirtualDirectory + TEXT("/"));
			const FString OverlayPath = bMatchesVirtualRoot ? FPaths::ConvertRelativePathToFull(FPaths::Combine(
				Status.OverlayDirectory, VirtualSource.Mid(Status.VirtualDirectory.Len() + 1))) : FString();
			const bool bMapped = Status.bActive && !Status.OverlayDirectory.IsEmpty() && bMatchesVirtualRoot
				&& FPaths::IsUnderDirectory(PhysicalPath, FPaths::ConvertRelativePathToFull(Status.OverlayDirectory))
				&& FPaths::IsSamePath(PhysicalPath, OverlayPath);
			const bool bReadable = EndfieldBackendProbeHashFile(PhysicalPath, Hash);
			const FString EnginePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::EngineDir(),
				TEXT("Shaders"), VirtualSource.Mid(FString(TEXT("/Engine/")).Len())));
			const bool bMappingPassed = bInactiveControl ? FPaths::IsSamePath(PhysicalPath, EnginePath) : bMapped;
			const FString* ExpectedHash = bInactiveControl ? nullptr : Status.PatchedSourceHashes.Find(Key);
			if (!ExpectedHash && !bInactiveControl) ExpectedHash = Status.SourceHashes.Find(TEXT("Private/") + Key);
			const bool bExpectedHashPassed = bInactiveControl || (ExpectedHash && Hash.Equals(*ExpectedHash, ESearchCase::IgnoreCase));
			TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("virtual_path"), VirtualSource);
			Item->SetStringField(TEXT("physical_path"), PhysicalPath);
			Item->SetStringField(TEXT("source_hash"), Hash);
			Item->SetBoolField(TEXT("mapped_to_adapter"), bMapped);
			Item->SetBoolField(TEXT("mapping_passed"), bMappingPassed);
			Item->SetBoolField(TEXT("source_readable"), bReadable);
			Item->SetStringField(TEXT("expected_source_hash"), ExpectedHash ? *ExpectedHash : TEXT(""));
			Item->SetBoolField(TEXT("expected_source_hash_passed"), bExpectedHashPassed);
			Item->SetBoolField(TEXT("source_cache_inspected"), false);
			Sources.Add(MakeShared<FJsonValueObject>(Item));
			if (!bMappingPassed || !bReadable || !bExpectedHashPassed)
				Errors.Add(TEXT("原生 shader 映射或磁盘源码 hash 不符合适配器状态：") + VirtualSource);
		}
		Report->SetArrayField(TEXT("resolved_sources"), Sources);
	}

	void EndfieldBackendProbeCheckDelegate(TSharedPtr<FJsonObject> Probe, TSharedRef<FJsonObject> Report,
		TArray<FString>& Errors, bool bProfileMarkerAllowed)
	{
		int32 Failures = -1;
		if (!Probe->TryGetNumberField(TEXT("failures"), Failures) || Failures != 0)
			Errors.Add(TEXT("ToonProbe 报告存在失败或缺少 failures 字段"));
		const TArray<TSharedPtr<FJsonValue>>* Materials = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Slots = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
		if (!Probe->TryGetArrayField(TEXT("toon_materials"), Materials) || Materials->Num() < 2)
			Errors.Add(TEXT("缺少真实原生 Toon 材质编译结果"));
		else for (const TSharedPtr<FJsonValue>& Value : *Materials)
		{
			const TSharedPtr<FJsonObject> Item = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			bool bReady = false, bSucceeded = false, bFinalized = false;
			int32 ShaderCount = 0;
			if (!Item.IsValid() || !Item->TryGetBoolField(TEXT("shader_map_ready"), bReady) || !bReady
				|| !Item->TryGetBoolField(TEXT("compiled_successfully"), bSucceeded) || !bSucceeded
				|| !Item->TryGetBoolField(TEXT("compilation_finalized"), bFinalized) || !bFinalized
				|| !Item->TryGetNumberField(TEXT("shader_count"), ShaderCount) || ShaderCount <= 0)
				Errors.Add(TEXT("原生 Toon 材质没有完整的真实绘制 shader"));
		}
		if (!Probe->TryGetArrayField(TEXT("slot_checks"), Slots) || Slots->IsEmpty())
			Errors.Add(TEXT("缺少实际绘制父材质/fallback 检查"));
		else for (const TSharedPtr<FJsonValue>& Value : *Slots)
		{
			const TSharedPtr<FJsonObject> Item = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			bool bDefault = true, bFallback = true;
			if (!Item.IsValid() || !Item->TryGetBoolField(TEXT("rendered_default_material"), bDefault) || bDefault
				|| !Item->TryGetBoolField(TEXT("rendered_fallback"), bFallback) || bFallback)
				Errors.Add(TEXT("实际绘制使用默认材质、fallback 或缺少诊断字段"));
		}
		TSet<FString> ImageNames;
		if (!Probe->TryGetArrayField(TEXT("images"), Images) || Images->IsEmpty())
			Errors.Add(TEXT("ToonProbe 没有实际截图"));
		else for (const TSharedPtr<FJsonValue>& Value : *Images)
		{
			const TSharedPtr<FJsonObject> Item = Value.IsValid() && Value->Type == EJson::Object ? Value->AsObject() : nullptr;
			bool bOk = false;
			FString Path;
			if (!Item.IsValid() || !Item->TryGetBoolField(TEXT("ok"), bOk) || !bOk
				|| !Item->TryGetStringField(TEXT("file"), Path) || IFileManager::Get().FileSize(*Path) <= 0)
				Errors.Add(TEXT("截图失败、文件缺失或为空"));
			FString Name;
			if (Item.IsValid() && Item->TryGetStringField(TEXT("name"), Name)) ImageNames.Add(Name);
		}
		FString ExpText;
		TArray<FString> Experiments;
		Probe->TryGetStringField(TEXT("experiments"), ExpText);
		ExpText.ParseIntoArray(Experiments, TEXT(","), true);
		const bool bBackendExperiment = Experiments.Contains(TEXT("backend")) || Experiments.Contains(TEXT("all"));
		Report->SetBoolField(TEXT("backend_experiment_requested"), bBackendExperiment);
		if (bBackendExperiment)
		{
			bool bReportedMarker = !bProfileMarkerAllowed;
			if (!Probe->TryGetBoolField(TEXT("backend_profile_marker_allowed"), bReportedMarker)
				|| bReportedMarker != bProfileMarkerAllowed)
				Errors.Add(TEXT("backend 实验的 Profile 路由许可与启动状态不一致"));
			for (const TCHAR* Light : { TEXT("directional"), TEXT("point"), TEXT("spot"), TEXT("rect") })
				for (const TCHAR* State : { TEXT("clear"), TEXT("blocked") })
				{
					const FString Name = FString::Printf(TEXT("backend_%s_%s_full"), Light, State);
					if (!ImageNames.Contains(Name)) Errors.Add(TEXT("缺少 backend 单灯/遮挡实拍：") + Name);
				}
			for (const TCHAR* Name : { TEXT("backend_all_lights_full"), TEXT("backend_indirect_full"), TEXT("backend_indirect_face") })
				if (!ImageNames.Contains(Name)) Errors.Add(TEXT("缺少 backend 多灯/环境实拍：") + FString(Name));
		}
		Report->SetNumberField(TEXT("material_count"), Materials ? Materials->Num() : 0);
		Report->SetNumberField(TEXT("slot_check_count"), Slots ? Slots->Num() : 0);
		Report->SetNumberField(TEXT("image_count"), Images ? Images->Num() : 0);
	}
}

UMMDEndfieldNativeBackendProbeCommandlet::UMMDEndfieldNativeBackendProbeCommandlet()
{
	IsClient = true;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
	ShowErrorCount = true;
}

int32 UMMDEndfieldNativeBackendProbeCommandlet::Main(const FString& Params)
{
	const MMDEndfieldNativeBackend::FStatus& Status = MMDEndfieldNativeBackend::GetStatus();
	const bool bInactiveControl = FParse::Param(*Params, TEXT("AllowInactiveBackend")) && !Status.bRequested && !Status.bActive;
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<FString> Errors;
	Report->SetStringField(TEXT("schema"), TEXT("mmd.endfield.native-backend-probe.v1"));
	Report->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Report->SetObjectField(TEXT("adapter"), EndfieldBackendProbeStatusJson(Status));
	Report->SetBoolField(TEXT("inactive_control"), bInactiveControl);
	Report->SetStringField(TEXT("hash_algorithm"), TEXT("sha1"));
	Report->SetStringField(TEXT("hash_scope"), TEXT("raw-file-bytes/integrity-guard"));
	FString OutDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Phase2Reference/Endfield2/native-backend-probe"));
	FParse::Value(*Params, TEXT("OutDir="), OutDir);
	OutDir = FPaths::ConvertRelativePathToFull(OutDir);
	FString Tag = TEXT("native_backend");
	FParse::Value(*Params, TEXT("Tag="), Tag);
	if (Tag.IsEmpty() || FPaths::MakeValidFileName(Tag) != Tag)
	{
		Errors.Add(TEXT("Tag 必须是非空文件名前缀，不能包含路径字符"));
		Tag = TEXT("native_backend_invalid_tag");
	}
	IFileManager::Get().MakeDirectory(*OutDir, true);
	Report->SetStringField(TEXT("tag"), Tag);
	if (!bInactiveControl && !Status.bRequested) Errors.Add(TEXT("没有在进程启动时请求原生后端；此探针不把普通 Toon 回退当作后端成功"));
	if (!bInactiveControl && !Status.bActive) Errors.Add(TEXT("原生后端没有激活：") + Status.Reason);
	if (Status.bCookedRuntime) Errors.Add(TEXT("此 Editor 探针验证源码映射；Cooked shader 库需要独立打包运行验证，不能在此切换后端模式"));
	if (!bInactiveControl && (!Status.bEngineSourcesVerified || !Status.bEngineSourcesUnchanged))
		Errors.Add(TEXT("适配器启动时未通过引擎源码完整性校验"));
	if (Status.bActive || bInactiveControl) EndfieldBackendProbeCheckMapping(Status, Report, Errors, bInactiveControl);
	MMDEndfieldNativeBackend::FStatus VerificationStatus = Status;
	if (bInactiveControl && VerificationStatus.SourceHashes.IsEmpty())
	{
		for (const TCHAR* Key : { TEXT("Private/Substrate/SubstrateEvaluation.ush"), TEXT("Private/Substrate/SubstrateToonBSDF.ush"),
			TEXT("Private/Substrate/SubstrateDeferredLighting.ush"), TEXT("Private/DiffuseIndirectComposite.usf"),
			TEXT("Private/ReflectionEnvironmentPixelShader.usf"), TEXT("Private/ClusteredDeferredShadingPixelShader.usf"),
			TEXT("Private/ToonProfileCommon.ush"), TEXT("Shared/SubstrateDefinitions.h") })
		{
			FString Hash;
			if (EndfieldBackendProbeHashFile(EndfieldBackendProbeEngineSourcePath(Key), Hash)) VerificationStatus.SourceHashes.Add(Key, Hash);
			else Errors.Add(TEXT("无法读取负对照的原始引擎 shader：") + FString(Key));
		}
	}
	if (bInactiveControl && VerificationStatus.PluginSourceHashes.IsEmpty())
	{
		for (const TCHAR* Key : EndfieldBackendProbePluginSources)
		{
			FString Hash;
			if (EndfieldBackendProbeHashFile(EndfieldBackendProbePluginSourcePath(Key), Hash)) VerificationStatus.PluginSourceHashes.Add(Key, Hash);
			else Errors.Add(TEXT("无法读取负对照的插件 shader：") + FString(Key));
		}
	}
	Report->SetObjectField(TEXT("source_integrity_baseline"), EndfieldBackendProbeStatusJson(VerificationStatus));
	const bool bEngineUnchangedBefore = EndfieldBackendProbeVerifyEngineSources(VerificationStatus, Errors, TEXT("绘制前"));
	Report->SetBoolField(TEXT("engine_sources_unchanged_before"), bEngineUnchangedBefore);
	Report->SetBoolField(TEXT("plugin_sources_unchanged_before"), EndfieldBackendProbeVerifyPluginSources(VerificationStatus, Errors, TEXT("绘制前")));

	FString ProbeParams = Params;
	FString Existing;
	if (!FParse::Value(*Params, TEXT("OutDir="), Existing)) ProbeParams += TEXT(" -OutDir=\"") + OutDir + TEXT("\"");
	if (!FParse::Value(*Params, TEXT("Tag="), Existing)) ProbeParams += TEXT(" -Tag=") + Tag;
	if (!FParse::Value(*Params, TEXT("Exp="), Existing, false)) ProbeParams += TEXT(" -Exp=backend,lighting");
	if (!FParse::Value(*Params, TEXT("NativePreset="), Existing))
		ProbeParams += TEXT(" -NativePreset=/Ue5MMDTools/Stages/Presets/EndfieldV2/DA_Render_EndfieldV2.DA_Render_EndfieldV2");
	if (!FParse::Value(*Params, TEXT("Frames="), Existing)) ProbeParams += TEXT(" -Frames=10");
	if (!FParse::Param(*Params, TEXT("LumenTracking"))) ProbeParams += TEXT(" -LumenTracking");
	const FString ProbeReportPath = FPaths::Combine(OutDir, Tag + TEXT("_report.json"));
	Report->SetStringField(TEXT("delegate_report"), ProbeReportPath);
	int32 ProbeExit = -1;
	bool bDelegateAttempted = false;
	if (Errors.IsEmpty())
	{
		bDelegateAttempted = true;
		const FDateTime Started = FDateTime::UtcNow();
		TStrongObjectPtr<UMMDEndfieldToonProbeCommandlet> Probe(NewObject<UMMDEndfieldToonProbeCommandlet>(GetTransientPackage()));
		ProbeExit = Probe->Main(ProbeParams);
		if (ProbeExit != 0) Errors.Add(FString::Printf(TEXT("ToonProbe 返回失败：%d"), ProbeExit));
		FString ProbeText;
		TSharedPtr<FJsonObject> ProbeJson;
		const bool bFresh = IFileManager::Get().GetTimeStamp(*ProbeReportPath) >= Started - FTimespan::FromSeconds(2);
		if (!bFresh || !FFileHelper::LoadFileToString(ProbeText, *ProbeReportPath)
			|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ProbeText), ProbeJson) || !ProbeJson.IsValid())
			Errors.Add(TEXT("没有取得本次 ToonProbe 的新报告，拒绝复用旧结果"));
		else
		{
			Report->SetObjectField(TEXT("toon_probe"), ProbeJson);
			EndfieldBackendProbeCheckDelegate(ProbeJson, Report, Errors, Status.bProfileMarkerAllowed);
			FString Hash;
			EndfieldBackendProbeHashFile(ProbeReportPath, Hash);
			Report->SetStringField(TEXT("delegate_report_hash"), Hash);
		}
	}
	Report->SetBoolField(TEXT("delegate_attempted"), bDelegateAttempted);
	Report->SetNumberField(TEXT("delegate_exit_code"), ProbeExit);
	Report->SetBoolField(TEXT("engine_sources_unchanged_after"), EndfieldBackendProbeVerifyEngineSources(VerificationStatus, Errors, TEXT("绘制后")));
	Report->SetBoolField(TEXT("plugin_sources_unchanged_after"), EndfieldBackendProbeVerifyPluginSources(VerificationStatus, Errors, TEXT("绘制后")));
	Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
	Report->SetBoolField(TEXT("backend_success"), Errors.IsEmpty() && Status.bRequested && Status.bActive);
	Report->SetBoolField(TEXT("visual_accepted"), false);
	Report->SetStringField(TEXT("validation_scope"), TEXT("验证启动映射、patched 磁盘源码 hash、引擎/插件源文件前后完整性、真实绘制 shader、非默认/fallback 材质和截图产出；不读取或更改 shader 源码缓存。success 表示测试装置验证通过，backend_success 另要求后端请求且激活。显式 AllowInactiveBackend 只允许完全未请求的原生引擎负对照，绝不接受请求后激活失败。成功不等于画面复刻验收。kernel 模式/环境倍率来自自有 Profile alpha metadata，不因模式改变而重编译；报告配置是请求默认值，实际每个 Profile 的 GPU 响应由实拍对照判定。"));
	Report->SetStringField(TEXT("experimental_limits"), TEXT("backend 实验提供独立光源/遮挡及无直射环境对照，lighting 实验提供 GI/反射/AO/天空开关；仅执行相应 Exp 的项目才受测。VSM/CSM 的启动配置与实际 GPU 路径需分别验证；Lumen/反射开关的像素差必须超过历史噪声才能判有贡献，接近噪声时结论为未定。Cook/Shipping 尚需独立验证。"));
	TArray<TSharedPtr<FJsonValue>> JsonErrors;
	for (const FString& Error : Errors)
	{
		JsonErrors.Add(MakeShared<FJsonValueString>(Error));
		UE_LOG(LogTemp, Error, TEXT("[EndfieldNativeBackendProbe] %s"), *Error);
	}
	Report->SetArrayField(TEXT("errors"), JsonErrors);
	Report->SetNumberField(TEXT("failures"), Errors.Num());
	FString Text;
	FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Text));
	const FString ReportPath = FPaths::Combine(OutDir, Tag + TEXT("_backend_report.json"));
	if (!FFileHelper::SaveStringToFile(Text, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("[EndfieldNativeBackendProbe] 无法保存报告：%s"), *ReportPath);
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("[EndfieldNativeBackendProbe] mode=%d success=%d failures=%d report=%s"),
		Status.Mode, Errors.IsEmpty() ? 1 : 0, Errors.Num(), *ReportPath);
	return Errors.IsEmpty() ? 0 : 1;
}
