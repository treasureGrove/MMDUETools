// 启动时请求后端；模式与环境倍率写入自有 Profile alpha，shader 从 Profile 读取，无需按模式重编译。
// 用法：-run=MMDEndfieldNativeBackendProbe -OutDir=<Saved目录> -Tag=<前缀> [-Exp=backend,lighting]
// -AllowInactiveBackend 只允许没有请求适配器的原生负对照，其 backend_success 始终为 false。
// -AdapterOnly 在空白宿主中验证映射、协议和三份发布父材质编译，不创建角色或绘制装置。
// 仅写测试报告/截图；沿用 ToonProbe 的只读资产加载和 Transient 场景，不复制测试装置。
#include "MMDEndfieldNativeBackendProbeCommandlet.h"

#include "MMDEndfieldToonProbeCommandlet.h"
#include "AssetCompilingManager.h"
#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Materials/Material.h"
#include "MaterialShared.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "RenderUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "ShaderCompilerCore.h"
#include "ShaderCompiler.h"
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
		TEXT("Core/TMMDEndfieldNativeIndirect.ush"), TEXT("Core/TMMDEndfieldNativeLegacy.ush"),
		TEXT("Core/TMMDEndfieldNativePayload.ush"), TEXT("Private/MMDEndfieldNativeBackendProtocol.usf") };

	bool EndfieldBackendProbeVerifyPluginSources(const MMDEndfieldNativeBackend::FStatus& Status,
		TArray<FString>& Errors, const TCHAR* Stage)
	{
		bool bUnchanged = !Status.PluginSourceHashes.IsEmpty();
		if (Status.CompiledPayloadProtocol != MMDEndfieldNativeBackend::GetCompiledPayloadProtocol())
		{
			Errors.Add(FString::Printf(TEXT("%s：payload 协议与当前模块编译版本不匹配"), Stage));
			bUnchanged = false;
		}
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
		Json->SetNumberField(TEXT("expected_cooked_global_shader_protocol"), MMDEndfieldNativeBackend::CookedGlobalShaderProtocolVersion);
		Json->SetNumberField(TEXT("cooked_global_shader_protocol"), Status.CookedGlobalShaderProtocol);
		Json->SetBoolField(TEXT("cooked_global_shader_protocol_verified"), Status.bCookedGlobalShaderProtocolVerified);
		Json->SetBoolField(TEXT("all_material_kernel_verified"), false);
		Json->SetStringField(TEXT("global_shader_protocol_scope"), TEXT("Cooked status is set from serialized compile reflection of the active RHI protocol global shader; not a GPU dispatch/readback or proof of every material/lighting kernel."));
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
		Json->SetStringField(TEXT("compiled_payload_protocol"), Status.CompiledPayloadProtocol);
		Json->SetBoolField(TEXT("compiled_payload_protocol_matches_module"),
			Status.CompiledPayloadProtocol == MMDEndfieldNativeBackend::GetCompiledPayloadProtocol());
		Json->SetStringField(TEXT("payload_protocol_scope"), TEXT("compiled module ABI declaration; private CustomOutput C++ files are not required; this field alone does not verify cooked shader bytecode"));
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
				TEXT("DiffuseIndirectComposite.usf"), TEXT("ReflectionEnvironmentPixelShader.usf"), TEXT("ClusteredDeferredShadingPixelShader.usf"),
				TEXT("ToonProfileCommon.ush") };
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

	void EndfieldBackendProbeCheckPublishedMaterials(TSharedRef<FJsonObject> Report, TArray<FString>& Errors)
	{
		const IConsoleVariable* Substrate = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Substrate"));
		if (!Substrate || Substrate->GetInt() == 0)
		{
			Errors.Add(TEXT("AdapterOnly 的发布 Toon 父材质编译验证要求启动时启用 r.Substrate=True"));
			Report->SetBoolField(TEXT("published_materials_compile_verified"), false);
			return;
		}
		const TCHAR* Paths[] = {
			TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Opaque.M_MMD_EndfieldV2_Opaque"),
			TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Masked.M_MMD_EndfieldV2_Masked"),
			TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Translucent.M_MMD_EndfieldV2_Translucent"),
			TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/M_MMD_EndfieldV2_Outline.M_MMD_EndfieldV2_Outline") };
		TArray<TStrongObjectPtr<UMaterial>> Materials;
		for (const TCHAR* Path : Paths)
		{
			UMaterial* Material = LoadObject<UMaterial>(nullptr, Path);
			Materials.Emplace(Material);
			// 仅更新内存 shader 资源，不改 StateId、不保存资产。
			if (Material) Material->ForceRecompileForRendering(EMaterialShaderPrecompileMode::Default);
			else Errors.Add(TEXT("发布父材质缺失：") + FString(Path));
		}
		FAssetCompilingManager::Get().FinishAllCompilation();
		if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
		FlushRenderingCommands();
		TArray<TSharedPtr<FJsonValue>> States;
		bool bAllReady = true;
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Paths); ++Index)
		{
			UMaterial* Material = Materials[Index].Get();
			const FMaterialResource* Resource = Material ? Material->GetMaterialResource(GMaxRHIShaderPlatform) : nullptr;
			const FMaterialShaderMap* Map = Resource ? Resource->GetGameThreadShaderMap() : nullptr;
			TArray<TSharedPtr<FJsonValue>> CompileErrors;
			if (Resource) for (const FString& Error : Resource->GetCompileErrors()) CompileErrors.Add(MakeShared<FJsonValueString>(Error));
			const bool bFinished = Resource && Resource->IsCompilationFinished();
			const bool bSucceeded = Map && Map->CompiledSuccessfully();
			const bool bFinalized = Map && Map->IsCompilationFinalized();
			const bool bRenderingValid = Map && Map->IsValidForRendering(false);
			const int32 ShaderCount = Map ? Map->GetShaderNum() : 0;
			const bool bReady = bFinished && bSucceeded && bFinalized && bRenderingValid && ShaderCount > 0 && CompileErrors.IsEmpty();
			TSharedRef<FJsonObject> State = MakeShared<FJsonObject>();
			State->SetStringField(TEXT("asset"), Paths[Index]);
			State->SetBoolField(TEXT("loaded"), Material != nullptr);
			State->SetBoolField(TEXT("shader_map_ready"), bReady);
			State->SetBoolField(TEXT("compilation_finished"), bFinished);
			State->SetBoolField(TEXT("compiled_successfully"), bSucceeded);
			State->SetBoolField(TEXT("compilation_finalized"), bFinalized);
			State->SetBoolField(TEXT("rendering_valid"), bRenderingValid);
			State->SetNumberField(TEXT("shader_count"), ShaderCount);
			State->SetArrayField(TEXT("compile_errors"), CompileErrors);
			States.Add(MakeShared<FJsonValueObject>(State));
			bAllReady &= bReady;
			if (!bReady) Errors.Add(TEXT("发布父材质的 shader map 未完整编译：") + FString(Paths[Index]));
		}
		Report->SetArrayField(TEXT("published_materials"), States);
		Report->SetBoolField(TEXT("published_materials_compile_verified"), bAllReady);
		Report->SetNumberField(TEXT("material_count"), States.Num());
		Report->SetNumberField(TEXT("image_count"), 0);
		Report->SetNumberField(TEXT("slot_check_count"), 0);
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
	const bool bAdapterOnly = FParse::Param(*Params, TEXT("AdapterOnly"));
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	TArray<FString> Errors;
	Report->SetStringField(TEXT("schema"), TEXT("mmd.endfield.native-backend-probe.v1"));
	Report->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Report->SetObjectField(TEXT("adapter"), EndfieldBackendProbeStatusJson(Status));
	Report->SetBoolField(TEXT("inactive_control"), bInactiveControl);
	Report->SetBoolField(TEXT("adapter_only"), bAdapterOnly);
	if (bAdapterOnly)
	{
		Report->SetBoolField(TEXT("published_materials_compile_verified"), false);
		Report->SetArrayField(TEXT("published_materials"), TArray<TSharedPtr<FJsonValue>>());
		Report->SetNumberField(TEXT("material_count"), 0);
		Report->SetNumberField(TEXT("image_count"), 0);
		Report->SetNumberField(TEXT("slot_check_count"), 0);
	}
	Report->SetStringField(TEXT("hash_algorithm"), TEXT("sha1"));
	Report->SetStringField(TEXT("hash_scope"), TEXT("raw-file-bytes/integrity-guard"));
	Report->SetBoolField(TEXT("all_material_kernel_verified"), false);
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
	FString ReportPath = FPaths::Combine(OutDir, Tag + TEXT("_backend_report.json"));
	FParse::Value(*Params, TEXT("Report="), ReportPath);
	ReportPath = FPaths::ConvertRelativePathToFull(ReportPath);
	Report->SetStringField(TEXT("report_file"), ReportPath);
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
	Report->SetStringField(TEXT("delegate_report"), bAdapterOnly ? FString() : ProbeReportPath);
	int32 ProbeExit = -1;
	bool bDelegateAttempted = false;
	bool bDrawVerified = false;
	if (Errors.IsEmpty() && bAdapterOnly) EndfieldBackendProbeCheckPublishedMaterials(Report, Errors);
	if (Errors.IsEmpty() && !bAdapterOnly)
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
			const int32 PreviousErrorCount = Errors.Num();
			EndfieldBackendProbeCheckDelegate(ProbeJson, Report, Errors, Status.bProfileMarkerAllowed);
			bDrawVerified = ProbeExit == 0 && PreviousErrorCount == Errors.Num();
			FString Hash;
			EndfieldBackendProbeHashFile(ProbeReportPath, Hash);
			Report->SetStringField(TEXT("delegate_report_hash"), Hash);
		}
	}
	Report->SetBoolField(TEXT("delegate_attempted"), bDelegateAttempted);
	Report->SetBoolField(TEXT("draw_verified"), bDrawVerified);
	Report->SetNumberField(TEXT("delegate_exit_code"), ProbeExit);
	uint32 GlobalShaderProtocol = 0u;
	FString GlobalShaderProtocolReason;
	const bool bGlobalShaderFound = MMDEndfieldNativeBackend::ReadGlobalShaderProtocol(GlobalShaderProtocol, GlobalShaderProtocolReason);
	const uint32 ExpectedGlobalShaderProtocol = bInactiveControl ? 0u : MMDEndfieldNativeBackend::CookedGlobalShaderProtocolVersion;
	const bool bGlobalShaderProtocolVerified = bGlobalShaderFound && GlobalShaderProtocol == ExpectedGlobalShaderProtocol;
	Report->SetNumberField(TEXT("expected_global_shader_protocol"), ExpectedGlobalShaderProtocol);
	Report->SetNumberField(TEXT("global_shader_protocol"), GlobalShaderProtocol);
	Report->SetBoolField(TEXT("global_shader_found"), bGlobalShaderFound);
	Report->SetBoolField(TEXT("global_shader_protocol_verified"), bGlobalShaderProtocolVerified);
	Report->SetStringField(TEXT("global_shader_protocol_reason"), GlobalShaderProtocolReason);
	Report->SetStringField(TEXT("global_shader_protocol_scope"), TEXT("Active RHI global shader object's serialized compile-reflection field only. Editor results describe the Editor shader map, not a separately cooked library. No GPU dispatch/readback; all_material_kernel_verified=false."));
	if (!bGlobalShaderProtocolVerified) Errors.Add(TEXT("全局 shader 编译协议验证失败：") + GlobalShaderProtocolReason);
	Report->SetBoolField(TEXT("engine_sources_unchanged_after"), EndfieldBackendProbeVerifyEngineSources(VerificationStatus, Errors, TEXT("绘制后")));
	Report->SetBoolField(TEXT("plugin_sources_unchanged_after"), EndfieldBackendProbeVerifyPluginSources(VerificationStatus, Errors, TEXT("绘制后")));
	Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
	Report->SetBoolField(TEXT("backend_success"), Errors.IsEmpty() && Status.bRequested && Status.bActive);
	Report->SetBoolField(TEXT("visual_accepted"), false);
	Report->SetStringField(TEXT("validation_scope"), bAdapterOnly
		? TEXT("AdapterOnly：验证启动映射、patched 磁盘源码 hash、引擎/插件 shader 前后完整性、模块 payload 协议、当前 Editor 全局 shader 的编译反射协议和四份发布父材质的真实 shader map 编译；不创建角色或测试绘制。draw_verified=false，image_count=0，all_material_kernel_verified=false，成功不代表 GPU 绘制、独立 Cook 库或最终画质通过。")
		: TEXT("验证启动映射、patched 磁盘源码 hash、引擎/插件源文件前后完整性、真实绘制 shader、非默认/fallback 材质和截图产出；不读取或更改 shader 源码缓存。success 表示测试装置验证通过，backend_success 另要求后端请求且激活。显式 AllowInactiveBackend 只允许完全未请求的原生引擎负对照，绝不接受请求后激活失败。成功不等于画面复刻验收。kernel 模式/环境倍率来自自有 Profile alpha metadata，不因模式改变而重编译；报告配置是请求默认值，实际每个 Profile 的 GPU 响应由实拍对照判定。"));
	Report->SetStringField(TEXT("experimental_limits"), bAdapterOnly
		? TEXT("仅验证空白宿主适配器与材质编译，不执行 backend/lighting 实验、材质槽 fallback 核对或截图。Cook/Shipping 仍需独立运行与图像对照。")
		: TEXT("backend 实验提供独立光源/遮挡及无直射环境对照，lighting 实验提供 GI/反射/AO/天空开关；仅执行相应 Exp 的项目才受测。VSM/CSM 的启动配置与实际 GPU 路径需分别验证；Lumen/反射开关的像素差必须超过历史噪声才能判有贡献，接近噪声时结论为未定。Cook/Shipping 尚需独立验证。"));
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
	if (!IFileManager::Get().MakeDirectory(*FPaths::GetPath(ReportPath), true)
		|| !FFileHelper::SaveStringToFile(Text, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogTemp, Error, TEXT("[EndfieldNativeBackendProbe] 无法保存报告：%s"), *ReportPath);
		return 1;
	}
	UE_LOG(LogTemp, Display, TEXT("[EndfieldNativeBackendProbe] mode=%d success=%d failures=%d report=%s"),
		Status.Mode, Errors.IsEmpty() ? 1 : 0, Errors.Num(), *ReportPath);
	return Errors.IsEmpty() ? 0 : 1;
}
