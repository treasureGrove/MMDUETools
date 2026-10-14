#include "Editor/MMDEndfieldFaceBinding.h"

#include "Components/SkeletalMeshComponent.h"
#include "Editor/MMDEndfieldFaceSdfBaker.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "Rendering/MMDEndfieldFaceData.h"
#include "SkinnedAssetCompiler.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Stage/MMDStageLibrary.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"

bool MMDEndfieldFaceBinding::BakeAndBind(USkeletalMesh* Mesh, const FString& TextureFolder,
	bool bSaveTextures, FResult& Result, const TArray<int32>& Slots, int32 Size)
{
	Result = FResult();
	if (!IsValid(Mesh))
	{
		Result.Errors.Add(TEXT("自动脸部烘焙缺少骨骼网格"));
		return false;
	}
	TArray<USkinnedAsset*> Pending = { Mesh };
	FSkinnedAssetCompilingManager::Get().FinishCompilation(Pending);
	MMDEnfieldFaceSdf::FBakeSettings Settings;
	Settings.TextureSize = Size;
	MMDEnfieldFaceSdf::EHeadFrameSource Source;
	FString FrameMessage;
	if (!MMDEnfieldFaceSdf::DeriveHeadFrame(Mesh, NAME_None, Settings, Source, FrameMessage)
		|| Source != MMDEnfieldFaceSdf::EHeadFrameSource::HeadBone)
	{
		Result.Notes.Add(TEXT("没有可靠头骨，未绑定 SDF，保留通用 Toon：") + FrameMessage);
		Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
		if (bSaveTextures) Mesh->MarkPackageDirty();
		return true;
	}
	if (!FrameMessage.IsEmpty()) Result.Notes.Add(FrameMessage);
	TArray<int32> Candidates = Slots;
	if (Candidates.IsEmpty())
	{
		TStrongObjectPtr<USkeletalMeshComponent> Probe(NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient));
		Probe->SetSkeletalMesh(Mesh);
		for (int32 Slot = 0; Slot < Mesh->GetMaterials().Num(); ++Slot)
			if (UMMDStageLibrary::GetResolvedCharacterMaterialRole(Probe.Get(), Slot) == EMMDMaterialRole::Skin)
				Candidates.Add(Slot);
	}
	TStrongObjectPtr<UMMDEndfieldFaceData> Data(NewObject<UMMDEndfieldFaceData>(Mesh, NAME_None,
		bSaveTextures ? RF_NoFlags : RF_Transient));
	for (const int32 Slot : Candidates)
	{
		if (!Mesh->GetMaterials().IsValidIndex(Slot))
		{
			Result.Errors.Add(FString::Printf(TEXT("脸部材质槽越界：%d"), Slot));
			continue;
		}
		Settings.MaterialIndices = { Slot };
		MMDEnfieldFaceSdf::FBakeResult Bake;
		const bool bBaked = MMDEnfieldFaceSdf::BakeFaceSdf(Mesh, Settings, Bake);
		FSlotStats& Stats = Result.SlotStats.AddDefaulted_GetRef();
		Stats.Slot = Slot;
		Stats.FaceTexels = Bake.FaceTexelCount;
		Stats.MirroredTexels = Bake.MirroredTexelCount;
		Stats.ConflictingTexels = Bake.ConflictingTexelCount;
		Stats.Triangles = Bake.ConsideredTriangleCount;
		Stats.FrontDarkTexels = Bake.FrontDarkTexelCount;
		Stats.NonContinuousTexels = Bake.NonContinuousTexelCount;
		if (!bBaked)
		{
			Result.Notes.Add(FString::Printf(TEXT("槽 %d 无可靠脸部覆盖，保持回退：%s"), Slot, *Bake.Error));
			continue;
		}
		FString Name = FString::Printf(TEXT("T_%s_FaceSdf_%d"), *Mesh->GetName(), Slot);
		FString Package = TextureFolder / Name;
		if (bSaveTextures && FPackageName::DoesPackageExist(Package))
		{
			UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *(Package + TEXT(".") + Name));
			const FString Version = Existing ? Existing->GetOutermost()->GetMetaData().GetValue(Existing, TEXT("MMDGeneratedFaceSdfVersion")) : FString();
			if (!Existing || (Version != TEXT("2") && Version != TEXT("3")))
			{
				// 同名用户资产不属于派生缓存，保留原件并选择新的资产路径。
				const FString OriginalPackage = Package;
				FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateUniqueAssetName(OriginalPackage, TEXT(""), Package, Name);
				Result.Notes.Add(TEXT("保留同名现有资产，改用派生纹理路径：") + Package);
			}
		}
		FString Error;
		UTexture2D* Texture = nullptr;
		if (bSaveTextures)
		{
			if (!FPackageName::IsValidLongPackageName(TextureFolder))
				Error = TEXT("派生纹理目录不是有效资产路径：") + TextureFolder;
			else if (MMDEnfieldFaceSdf::SaveAsTexture(Bake, Package, Name, Error))
				Texture = LoadObject<UTexture2D>(nullptr, *(Package + TEXT(".") + Name));
		}
		else
			Texture = MMDEnfieldFaceSdf::CreateTexture(Bake, Mesh, FName(Name), RF_Transient, Error);
		if (!Texture)
		{
			Result.Errors.Add(Error.IsEmpty() ? TEXT("烘焙纹理创建失败：") + Name : Error);
			continue;
		}
		FMMDGeneratedFaceSdfSlot& Binding = Data->Slots.AddDefaulted_GetRef();
		Binding.MaterialIndex = Slot;
		Binding.MaterialSlotName = Mesh->GetMaterials()[Slot].MaterialSlotName;
		Binding.Texture = Texture;
		Result.BoundSlots.Add(Slot);
		Result.TexturePaths.Add(Texture->GetPathName());
		Result.FaceTexelCount += Bake.FaceTexelCount;
	}
	if (Result.Errors.IsEmpty())
	{
		// 重烘无覆盖也清掉旧缓存，避免重导后的新 UV 继续消费旧纹理。
		Mesh->RemoveUserDataOfClass(UMMDEndfieldFaceData::StaticClass());
		if (!Data->Slots.IsEmpty()) Mesh->AddAssetUserData(Data.Get());
		if (bSaveTextures) Mesh->MarkPackageDirty();
	}
	return Result.Errors.IsEmpty();
}
