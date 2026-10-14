// -run=MMDEndfieldBindSdf -Mesh=/Game/Model/SK_Model.SK_Model -Report=<报告.json>
// 显式加 -Generate 才保存网格与派生纹理；可指定 -Slots=0,1 -Size=512 -OutputFolder=/Game/Model/Derived。
#include "MMDEndfieldBindSdfCommandlet.h"

#include "Dom/JsonObject.h"
#include "Editor/MMDEndfieldFaceBinding.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rendering/MMDEndfieldFaceData.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "SkinnedAssetCompiler.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	bool EndfieldBindSdfInteger(const FString& Text, int32& OutValue)
	{
		if (Text.IsEmpty()) return false;
		int32 Value = 0;
		for (const TCHAR Character : Text)
		{
			if (Character < TEXT('0') || Character > TEXT('9')) return false;
			const int32 Digit = Character - TEXT('0');
			if (Value > (MAX_int32 - Digit) / 10) return false;
			Value = Value * 10 + Digit;
		}
		OutValue = Value;
		return true;
	}

	TArray<TSharedPtr<FJsonValue>> EndfieldBindSdfJsonStrings(const TArray<FString>& Strings)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const FString& String : Strings) Result.Add(MakeShared<FJsonValueString>(String));
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> EndfieldBindSdfJsonSlots(const TArray<int32>& Slots)
	{
		TArray<TSharedPtr<FJsonValue>> Result;
		for (const int32 Slot : Slots) Result.Add(MakeShared<FJsonValueNumber>(Slot));
		return Result;
	}

	bool EndfieldBindSdfSaveMesh(USkeletalMesh* Mesh, const FString& Filename, FString& Error)
	{
		Mesh->GetOutermost()->FullyLoad();
		Mesh->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.SaveFlags = SAVE_NoError;
		if (UPackage::SavePackage(Mesh->GetOutermost(), Mesh, *Filename, Args)) return true;
		Error = TEXT("保存显式网格失败：") + Mesh->GetPathName();
		return false;
	}

	void EndfieldBindSdfInspect(USkeletalMesh* Mesh, MMDEndfieldFaceBinding::FResult& Result,
		TArray<TSharedPtr<FJsonValue>>& Checks)
	{
		const UMMDEndfieldFaceData* Data = Mesh->GetAssetUserData<UMMDEndfieldFaceData>();
		if (!Data)
		{
			Result.Notes.Add(TEXT("网格没有自动 SDF 绑定，保持通用 Toon 回退"));
			return;
		}
		if (Data->Version != UMMDEndfieldFaceData::CurrentVersion)
			Result.Errors.Add(FString::Printf(TEXT("自动 SDF 绑定版本不匹配：%d"), Data->Version));
		TSet<int32> SeenSlots;
		for (const FMMDGeneratedFaceSdfSlot& Binding : Data->Slots)
		{
			TArray<FString> Failures;
			UTexture2D* Texture = Binding.Texture.Get();
			const bool bValidSlot = Mesh->GetMaterials().IsValidIndex(Binding.MaterialIndex);
			if (!bValidSlot) Failures.Add(TEXT("材质槽越界"));
			else if (Mesh->GetMaterials()[Binding.MaterialIndex].MaterialSlotName != Binding.MaterialSlotName)
				Failures.Add(TEXT("材质槽名称变化，旧绑定不可用"));
			if (SeenSlots.Contains(Binding.MaterialIndex)) Failures.Add(TEXT("材质槽重复绑定"));
			SeenSlots.Add(Binding.MaterialIndex);
			if (!IsValid(Texture)) Failures.Add(TEXT("纹理不存在"));
			else
			{
				if (Texture->HasAnyFlags(RF_Transient) || Texture->GetOutermost() == GetTransientPackage())
					Failures.Add(TEXT("绑定仍是临时纹理，不能随网格持久化"));
				if (Texture->SRGB) Failures.Add(TEXT("SDF 纹理启用了 sRGB"));
				if (!Texture->Source.IsValid()) Failures.Add(TEXT("SDF 纹理缺少源数据"));
				const FString VersionTag = FString::FromInt(UMMDEndfieldFaceData::CurrentVersion);
				if (Texture->GetOutermost()->GetMetaData().GetValue(Texture, TEXT("MMDGeneratedFaceSdfVersion")) != VersionTag)
					Failures.Add(TEXT("纹理缺少本工具当前版本的派生资产标记"));
				if (Data->FindTexture(Binding.MaterialIndex, Binding.MaterialSlotName) != Texture)
					Failures.Add(TEXT("运行时绑定查找不能返回该纹理"));
			}
			TSharedRef<FJsonObject> Check = MakeShared<FJsonObject>();
			Check->SetNumberField(TEXT("slot"), Binding.MaterialIndex);
			Check->SetStringField(TEXT("slot_name"), Binding.MaterialSlotName.ToString());
			Check->SetStringField(TEXT("texture"), Texture ? Texture->GetPathName() : TEXT("missing"));
			Check->SetBoolField(TEXT("passed"), Failures.IsEmpty());
			Check->SetArrayField(TEXT("errors"), EndfieldBindSdfJsonStrings(Failures));
			Checks.Add(MakeShared<FJsonValueObject>(Check));
			for (const FString& Failure : Failures)
				Result.Errors.Add(FString::Printf(TEXT("槽 %d：%s"), Binding.MaterialIndex, *Failure));
			if (Failures.IsEmpty())
			{
				Result.BoundSlots.Add(Binding.MaterialIndex);
				Result.TexturePaths.Add(Texture->GetPathName());
			}
		}
	}
}

UMMDEndfieldBindSdfCommandlet::UMMDEndfieldBindSdfCommandlet()
{
	IsClient = true;
	IsEditor = true;
	IsServer = false;
	LogToConsole = true;
}

int32 UMMDEndfieldBindSdfCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	ParseCommandLine(*Params, Tokens, Switches);
	TMap<FString, FString> Values;
	bool bGenerate = false;
	MMDEndfieldFaceBinding::FResult Result;
	for (const FString& Switch : Switches)
	{
		FString Key;
		FString Value;
		if (Switch.Split(TEXT("="), &Key, &Value))
		{
			Key = Key.ToLower();
			if (Key == TEXT("generate")) Result.Errors.Add(TEXT("写入开关必须使用无赋值的 -Generate"));
			else if (Key == TEXT("mesh") || Key == TEXT("report") || Key == TEXT("outputfolder") || Key == TEXT("slots") || Key == TEXT("size"))
			{
				if (Values.Contains(Key)) Result.Errors.Add(TEXT("参数重复：-") + Key);
				else Values.Add(Key, Value.TrimQuotes());
			}
		}
		else if (Switch.Equals(TEXT("Generate"), ESearchCase::IgnoreCase)) bGenerate = true;
		else if (Switch.Equals(TEXT("Mesh"), ESearchCase::IgnoreCase) || Switch.Equals(TEXT("Report"), ESearchCase::IgnoreCase)
			|| Switch.Equals(TEXT("OutputFolder"), ESearchCase::IgnoreCase) || Switch.Equals(TEXT("Slots"), ESearchCase::IgnoreCase)
			|| Switch.Equals(TEXT("Size"), ESearchCase::IgnoreCase)) Result.Errors.Add(TEXT("参数缺少值：-") + Switch);
	}
	const FString MeshPath = Values.FindRef(TEXT("mesh"));
	const FString ReportPath = Values.FindRef(TEXT("report"));
	FString OutputFolder = Values.FindRef(TEXT("outputfolder"));
	TArray<int32> Slots;
	int32 Size = 512;
	if (MeshPath.IsEmpty()) Result.Errors.Add(TEXT("缺少 -Mesh=<骨骼网格对象路径>"));
	else if (!FPackageName::IsValidObjectPath(MeshPath)) Result.Errors.Add(TEXT("Mesh 不是合法资产对象路径：") + MeshPath);
	if (ReportPath.IsEmpty()) Result.Errors.Add(TEXT("缺少 -Report=<报告.json>"));
	else if (!FPaths::GetExtension(ReportPath).Equals(TEXT("json"), ESearchCase::IgnoreCase))
		Result.Errors.Add(TEXT("Report 必须使用 .json 扩展名"));
	if (const FString* SizeText = Values.Find(TEXT("size")))
		if (!EndfieldBindSdfInteger(*SizeText, Size) || Size < 64 || Size > 4096)
			Result.Errors.Add(TEXT("Size 必须是 64..4096 的整数"));
	if (const FString* SlotsText = Values.Find(TEXT("slots")))
	{
		TArray<FString> Parts;
		SlotsText->ParseIntoArray(Parts, TEXT(","), false);
		if (Parts.IsEmpty() || SlotsText->IsEmpty()) Result.Errors.Add(TEXT("Slots 不能为空"));
		for (const FString& Part : Parts)
		{
			int32 Slot = INDEX_NONE;
			if (!EndfieldBindSdfInteger(Part, Slot)) Result.Errors.Add(TEXT("Slots 只接受逗号分隔的非负整数：") + Part);
			else if (Slots.Contains(Slot)) Result.Errors.Add(FString::Printf(TEXT("Slots 存在重复槽：%d"), Slot));
			else Slots.Add(Slot);
		}
	}
	const FString MeshPackage = FPackageName::ObjectPathToPackageName(MeshPath);
	if (!Values.Contains(TEXT("outputfolder")) && !MeshPackage.IsEmpty())
		OutputFolder = FPackageName::GetLongPackagePath(MeshPackage) / TEXT("Derived/EndfieldFaceSdf");
	if (Values.Contains(TEXT("outputfolder")) && OutputFolder.IsEmpty()) Result.Errors.Add(TEXT("OutputFolder 不能为空"));
	FString MeshFilename;
	if (bGenerate)
	{
		if (!FPackageName::IsValidLongPackageName(MeshPackage)
			|| !FPackageName::TryConvertLongPackageNameToFilename(MeshPackage, MeshFilename, FPackageName::GetAssetPackageExtension()))
			Result.Errors.Add(TEXT("显式 Mesh 不在已挂载的可写资产目录中"));
		else if (IFileManager::Get().IsReadOnly(*MeshFilename)) Result.Errors.Add(TEXT("显式 Mesh 文件为只读：") + MeshFilename);
		if (!FPackageName::IsValidLongPackageName(OutputFolder)) Result.Errors.Add(TEXT("OutputFolder 不是已挂载的可写资产目录：") + OutputFolder);
	}

	bool bMeshSaved = false;
	TArray<TSharedPtr<FJsonValue>> Checks;
	TStrongObjectPtr<USkeletalMesh> Mesh;
	if (Result.Errors.IsEmpty())
	{
		Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, *MeshPath));
		if (!Mesh.IsValid()) Result.Errors.Add(TEXT("无法加载显式骨骼网格：") + MeshPath);
		else
		{
			TArray<USkinnedAsset*> Pending = { Mesh.Get() };
			FSkinnedAssetCompilingManager::Get().FinishCompilation(Pending);
			for (const int32 Slot : Slots)
				if (!Mesh->GetMaterials().IsValidIndex(Slot)) Result.Errors.Add(FString::Printf(TEXT("材质槽越界：%d"), Slot));
			if (Result.Errors.IsEmpty() && bGenerate)
			{
				// 只保存显式网格及工具生成的纹理，不保存引用的骨架、材质或其他资产。
				MMDEndfieldFaceBinding::FResult Generated;
				if (MMDEndfieldFaceBinding::BakeAndBind(Mesh.Get(), OutputFolder, true, Generated, Slots, Size))
				{
					FString SaveError;
					bMeshSaved = EndfieldBindSdfSaveMesh(Mesh.Get(), MeshFilename, SaveError);
					if (!bMeshSaved) Generated.Errors.Add(SaveError);
				}
				Result = MoveTemp(Generated);
				if (Result.Errors.IsEmpty())
				{
					MMDEndfieldFaceBinding::FResult Inspected;
					EndfieldBindSdfInspect(Mesh.Get(), Inspected, Checks);
					Result.Errors.Append(Inspected.Errors);
					Result.Notes.Append(Inspected.Notes);
				}
			}
			else if (Result.Errors.IsEmpty()) EndfieldBindSdfInspect(Mesh.Get(), Result, Checks);
		}
	}
	const bool bSuccess = Result.Errors.IsEmpty() && (!bGenerate || bMeshSaved);
	TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
	Report->SetBoolField(TEXT("success"), bSuccess);
	Report->SetBoolField(TEXT("generate_requested"), bGenerate);
	Report->SetBoolField(TEXT("read_only"), !bGenerate);
	Report->SetBoolField(TEXT("mesh_saved"), bMeshSaved);
	Report->SetBoolField(TEXT("fallback"), Result.BoundSlots.IsEmpty());
	Report->SetStringField(TEXT("target"), TEXT("EndfieldV2AutomaticFaceSdfBinding"));
	Report->SetStringField(TEXT("mesh"), MeshPath);
	Report->SetStringField(TEXT("output_folder"), OutputFolder);
	Report->SetNumberField(TEXT("size"), Size);
	Report->SetNumberField(TEXT("face_texel_count"), Result.FaceTexelCount);
	Report->SetArrayField(TEXT("requested_slots"), EndfieldBindSdfJsonSlots(Slots));
	Report->SetArrayField(TEXT("bound_slots"), EndfieldBindSdfJsonSlots(Result.BoundSlots));
	Report->SetArrayField(TEXT("textures"), EndfieldBindSdfJsonStrings(Result.TexturePaths));
	Report->SetArrayField(TEXT("binding_checks"), Checks);
	Report->SetArrayField(TEXT("notes"), EndfieldBindSdfJsonStrings(Result.Notes));
	Report->SetArrayField(TEXT("errors"), EndfieldBindSdfJsonStrings(Result.Errors));
	Report->SetNumberField(TEXT("failed"), Result.Errors.Num());
	for (const FString& Error : Result.Errors) UE_LOG(LogTemp, Error, TEXT("[MMDEndfieldBindSdf] %s"), *Error);
	bool bReportSaved = false;
	if (!ReportPath.IsEmpty() && FPaths::GetExtension(ReportPath).Equals(TEXT("json"), ESearchCase::IgnoreCase))
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(FPaths::ConvertRelativePathToFull(ReportPath)), true);
		FString Json;
		bReportSaved = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json))
			&& FFileHelper::SaveStringToFile(Json, *ReportPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}
	if (!bReportSaved) UE_LOG(LogTemp, Error, TEXT("[MMDEndfieldBindSdf] JSON 报告未保存：%s"), *ReportPath);
	UE_LOG(LogTemp, Display, TEXT("[MMDEndfieldBindSdf] slots=%d saved=%d fallback=%d success=%d"),
		Result.BoundSlots.Num(), bMeshSaved, Result.BoundSlots.IsEmpty(), bSuccess && bReportSaved);
	return bSuccess && bReportSaved ? 0 : 1;
}
