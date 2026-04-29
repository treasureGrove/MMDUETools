#include "TMMDMeshBuilder.h"
#include "TPMXParser.h"

#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "Misc/FrameRate.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
//构建
#include "ImportUtils/SkelImport.h"                  
#include "ImportUtils/SkeletalMeshImportUtils.h"       
#include "MeshUtilities.h"                             
#include "Engine/SkinnedAssetCommon.h"     
#include "Rendering/SkeletalMeshModel.h"             
#include "Rendering/SkeletalMeshLODModel.h"           
#include "Components/SkinnedMeshComponent.h"  
//材质
#include "Materials/Material.h"
#include "MaterialDomain.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceBasePropertyOverrides.h"
//转换
#include "Factories/TextureFactory.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/FileHelper.h"
#include "Engine/Texture2D.h"
//动画蓝图
#include "Factories/AnimBlueprintFactory.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#include "Animation/MorphTarget.h"
//IKRig
#if WITH_EDITOR
#include "RigEditor/IKRigController.h"
#include "Rig/Solvers/IKRig_FBIKSolver.h"
#include "Retargeter/IKRetargeter.h"
#include "RetargetEditor/IKRetargeterController.h"   
#endif
#pragma region 材质贴图
static constexpr uint8 PMX_DRAW_DOUBLE_SIDED = 0x01;
static constexpr uint8 PMX_DRAW_CAST_SHADOW = 0x04;

static uint16 ReadBmpU16(const TArray<uint8>& Data, int32 Offset)
{
	return (Offset >= 0 && Offset + 1 < Data.Num())
		? static_cast<uint16>(Data[Offset] | (Data[Offset + 1] << 8))
		: 0;
}

static uint32 ReadBmpU32(const TArray<uint8>& Data, int32 Offset)
{
	return (Offset >= 0 && Offset + 3 < Data.Num())
		? static_cast<uint32>(Data[Offset] | (Data[Offset + 1] << 8) | (Data[Offset + 2] << 16) | (Data[Offset + 3] << 24))
		: 0;
}

static int32 ReadBmpS32(const TArray<uint8>& Data, int32 Offset)
{
	return static_cast<int32>(ReadBmpU32(Data, Offset));
}

static uint8 ExtractBmpMaskChannel(uint32 Pixel, uint32 Mask)
{
	if (Mask == 0)
	{
		return 255;
	}

	uint32 Shift = 0;
	while (((Mask >> Shift) & 1u) == 0u && Shift < 32)
	{
		++Shift;
	}

	const uint32 Value = (Pixel & Mask) >> Shift;
	const uint32 MaxValue = Mask >> Shift;
	return MaxValue > 0 ? static_cast<uint8>((Value * 255u + MaxValue / 2u) / MaxValue) : 0;
}

static bool DecodeBmpToBGRA8(const TArray<uint8>& FileData, TArray<uint8>& OutPixels, int32& OutWidth, int32& OutHeight)
{
	if (FileData.Num() < 54 || FileData[0] != 'B' || FileData[1] != 'M')
	{
		return false;
	}

	const int32 PixelOffset = static_cast<int32>(ReadBmpU32(FileData, 10));
	const int32 HeaderSize = static_cast<int32>(ReadBmpU32(FileData, 14));
	if (HeaderSize < 40 || PixelOffset <= 0 || PixelOffset >= FileData.Num())
	{
		return false;
	}

	const int32 Width = ReadBmpS32(FileData, 18);
	const int32 SignedHeight = ReadBmpS32(FileData, 22);
	const uint16 Planes = ReadBmpU16(FileData, 26);
	const uint16 BitsPerPixel = ReadBmpU16(FileData, 28);
	const uint32 Compression = ReadBmpU32(FileData, 30);

	if (Width <= 0 || SignedHeight == 0 || Planes != 1)
	{
		return false;
	}

	if (!((BitsPerPixel == 24 && Compression == 0) || (BitsPerPixel == 32 && (Compression == 0 || Compression == 3))))
	{
		return false;
	}

	const int32 Height = FMath::Abs(SignedHeight);
	const bool bTopDown = SignedHeight < 0;
	const int64 RowStride = ((static_cast<int64>(Width) * BitsPerPixel + 31) / 32) * 4;
	if (RowStride <= 0 || static_cast<int64>(PixelOffset) + RowStride * Height > FileData.Num())
	{
		return false;
	}

	uint32 RedMask = 0x00ff0000;
	uint32 GreenMask = 0x0000ff00;
	uint32 BlueMask = 0x000000ff;
	uint32 AlphaMask = 0xff000000;
	if (BitsPerPixel == 32 && Compression == 3)
	{
		const int32 MaskOffset = 14 + 40;
		if (MaskOffset + 11 < FileData.Num())
		{
			RedMask = ReadBmpU32(FileData, MaskOffset);
			GreenMask = ReadBmpU32(FileData, MaskOffset + 4);
			BlueMask = ReadBmpU32(FileData, MaskOffset + 8);
			AlphaMask = MaskOffset + 15 < FileData.Num() ? ReadBmpU32(FileData, MaskOffset + 12) : 0;
		}
	}

	OutWidth = Width;
	OutHeight = Height;
	OutPixels.SetNumUninitialized(Width * Height * 4);

	for (int32 Y = 0; Y < Height; ++Y)
	{
		const int32 SourceY = bTopDown ? Y : (Height - 1 - Y);
		const int64 SourceRow = static_cast<int64>(PixelOffset) + static_cast<int64>(SourceY) * RowStride;
		uint8* Dest = OutPixels.GetData() + static_cast<int64>(Y) * Width * 4;

		for (int32 X = 0; X < Width; ++X)
		{
			if (BitsPerPixel == 24)
			{
				const int64 Source = SourceRow + static_cast<int64>(X) * 3;
				Dest[X * 4 + 0] = FileData[Source + 0];
				Dest[X * 4 + 1] = FileData[Source + 1];
				Dest[X * 4 + 2] = FileData[Source + 2];
				Dest[X * 4 + 3] = 255;
			}
			else
			{
				const uint32 Pixel = ReadBmpU32(FileData, static_cast<int32>(SourceRow + static_cast<int64>(X) * 4));
				Dest[X * 4 + 0] = ExtractBmpMaskChannel(Pixel, BlueMask);
				Dest[X * 4 + 1] = ExtractBmpMaskChannel(Pixel, GreenMask);
				Dest[X * 4 + 2] = ExtractBmpMaskChannel(Pixel, RedMask);
				Dest[X * 4 + 3] = AlphaMask != 0 ? ExtractBmpMaskChannel(Pixel, AlphaMask) : 255;
			}
		}
	}

	return true;
}

static UTexture2D* CreateTextureFromBGRA8(UPackage* Package, const FString& AssetName, const TArray<uint8>& Pixels, int32 Width, int32 Height)
{
	if (!Package || Pixels.Num() != Width * Height * 4)
	{
		return nullptr;
	}

	UTexture2D* Texture = NewObject<UTexture2D>(Package, FName(*AssetName), RF_Public | RF_Standalone);
	if (!Texture)
	{
		return nullptr;
	}

	Texture->Source.Init(Width, Height, 1, 1, TSF_BGRA8, Pixels.GetData());
	Texture->SRGB = true;
	Texture->CompressionSettings = TC_Default;
	Texture->MipGenSettings = TMGS_FromTextureGroup;
	Texture->UpdateResource();
	Texture->PostEditChange();

	return Texture;
}

FString FixMMDName(const FString& InName, const FString& Prefix = TEXT(""))
{
	FString Name = InName;
	Name = Name.Replace(TEXT(" "), TEXT("_"))
		.Replace(TEXT("."), TEXT("_"))
		.Replace(TEXT("-"), TEXT("_"))
		.Replace(TEXT("("), TEXT("_"))
		.Replace(TEXT(")"), TEXT("_"))
		.Replace(TEXT("["), TEXT("_"))
		.Replace(TEXT("]"), TEXT("_"))
		.Replace(TEXT("中"), TEXT("ZH"))
		.Replace(TEXT("文"), TEXT("WEN"))
		.Replace(TEXT("<"), TEXT("_"))
		.Replace(TEXT(">"), TEXT("_"))
		.Replace(TEXT(":"), TEXT("_"))
		.Replace(TEXT("*"), TEXT("_"))
		.Replace(TEXT("?"), TEXT("_"))
		.Replace(TEXT("\""), TEXT("_"))
		.Replace(TEXT("|"), TEXT("_"))
		.Replace(TEXT(","), TEXT("_"))
		.Replace(TEXT("&"), TEXT("_"))
		.Replace(TEXT("!"), TEXT("_"))
		.Replace(TEXT("~"), TEXT("_"))
		.Replace(TEXT("@"), TEXT("_"))
		.Replace(TEXT("#"), TEXT("_"))
		.Replace(TEXT("'"), TEXT("_"));
	while (Name.Contains(TEXT("__"))) Name = Name.Replace(TEXT("__"), TEXT("_"));
	if (!Name.IsEmpty() && !FChar::IsAlpha(Name[0])) Name = Prefix + Name;
	if (Name.IsEmpty()) Name = Prefix + TEXT("Unknown");
	return Name;
}

FString FixAnimAssetName(const FString& InName)
{
	return FixMMDName(InName, TEXT("Anim_"));
}

FVector ConvertMMDPositionToUnreal(const FVector& InPos, float Scale)
{
	FVector TempPos(InPos.Z * Scale, InPos.X * Scale, InPos.Y * Scale);
	return FVector(TempPos.Y, -TempPos.X, TempPos.Z);
}

FQuat ConvertMMDQuatToUnreal(const FQuat& InQuat)
{
	const FQuat AxisSwap(InQuat.Z, InQuat.X, InQuat.Y, InQuat.W);
	return FQuat(AxisSwap.Y, -AxisSwap.X, AxisSwap.Z, AxisSwap.W);
}

FString BuildMMDAnimationFolderPath(const FMMDAnimationImportContext& Context)
{
	FString ModelName = TEXT("UnknownModel");
	if (Context.PMXData != nullptr)
	{
		ModelName = !Context.PMXData->ModelNameEN.IsEmpty() ? Context.PMXData->ModelNameEN : Context.PMXData->ModelNameJP;
	}
	if (!Context.SourcePMXFilePath.IsEmpty())
	{
		ModelName = FPaths::GetBaseFilename(Context.SourcePMXFilePath);
	}
	return FString("/Game/MMDModels/") + FixMMDName(ModelName) + TEXT("/Animation");
}

template<typename KeyType, typename NameAccessor>
TArray<TPair<FString, int32>> BuildTrackCounts(const TArray<KeyType>& Keys, NameAccessor&& Accessor)
{
	TMap<FString, int32> Counts;
	for (const KeyType& Key : Keys)
	{
		const FString Name = Accessor(Key);
		int32& Count = Counts.FindOrAdd(Name);
		++Count;
	}

	TArray<TPair<FString, int32>> Result;
	for (const TPair<FString, int32>& Pair : Counts)
	{
		Result.Add(Pair);
	}
	Result.Sort([](const TPair<FString, int32>& A, const TPair<FString, int32>& B)
	{
		return A.Key < B.Key;
	});
	return Result;
}

bool TryResolveMorphTargetName(const USkeletalMesh* SkeletalMesh, const FString& SourceName, FName& OutMorphName)
{
	if (SkeletalMesh == nullptr)
	{
		return false;
	}

	for (UMorphTarget* MorphTarget : SkeletalMesh->GetMorphTargets())
	{
		if (MorphTarget == nullptr)
		{
			continue;
		}

		const FString Candidate = MorphTarget->GetFName().ToString();
		if (Candidate == SourceName || FixMMDName(Candidate) == FixMMDName(SourceName))
		{
			OutMorphName = MorphTarget->GetFName();
			return true;
		}
	}

	return false;
}

template<typename TReportArray>
void AppendUniqueMessage(TReportArray& Messages, const FString& Message)
{
	Messages.AddUnique(Message);
}

struct FResolvedVMDBoneKey
{
	int32 Frame = 0;
	FVector Position = FVector::ZeroVector;
	FQuat Rotation = FQuat::Identity;
};

bool BuildResolvedBoneKeyMap(const VMDData& VmdData, const FMMDAnimationImportReport& Report, const FMMDAnimationImportSettings& Settings, TMap<FName, TArray<FResolvedVMDBoneKey>>& OutTrackMap)
{
	OutTrackMap.Reset();

	TMap<FString, FName> SourceToTargetBoneMap;
	for (const FMMDResolvedBoneTrack& Track : Report.BoneTracks)
	{
		if (Track.bMatched && Track.TargetBoneName != NAME_None)
		{
			SourceToTargetBoneMap.Add(Track.SourceBoneName, Track.TargetBoneName);
		}
	}

	for (const VMDBoneKeyframe& Keyframe : VmdData.BoneKeyframes)
	{
		const FName* TargetBoneName = SourceToTargetBoneMap.Find(Keyframe.BoneName);
		if (TargetBoneName == nullptr || *TargetBoneName == NAME_None)
		{
			continue;
		}

		FResolvedVMDBoneKey ResolvedKey;
		ResolvedKey.Frame = static_cast<int32>(Keyframe.FrameNumber);
		ResolvedKey.Position = ConvertMMDPositionToUnreal(Keyframe.Position, Settings.PositionScale);
		ResolvedKey.Rotation = ConvertMMDQuatToUnreal(Keyframe.Rotation).GetNormalized();
		OutTrackMap.FindOrAdd(*TargetBoneName).Add(MoveTemp(ResolvedKey));
	}

	for (TPair<FName, TArray<FResolvedVMDBoneKey>>& Pair : OutTrackMap)
	{
		Pair.Value.Sort([](const FResolvedVMDBoneKey& A, const FResolvedVMDBoneKey& B)
		{
			return A.Frame < B.Frame;
		});
	}

	return OutTrackMap.Num() > 0;
}

int32 BuildMorphTargetsFromPMX(USkeletalMesh* SkeletalMesh, const PMXDatas& PMXInfo)
{
#if WITH_EDITOR
	if (SkeletalMesh == nullptr)
	{
		return 0;
	}

	FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
	if (ImportedModel == nullptr || ImportedModel->LODModels.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("PMX Morph: SkeletalMesh has no imported LOD model."));
		return 0;
	}

	const FSkeletalMeshLODModel& BaseLODModel = ImportedModel->LODModels[0];
	const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(0);
	const float PositionThreshold = LODInfo ? LODInfo->BuildSettings.MorphThresholdPosition : UE_THRESH_POINTS_ARE_NEAR;

	int32 ImportedMorphCount = 0;
	int32 SkippedMorphCount = 0;
	bool bNeedInitMorphTargets = false;
	auto ConvertMorphOffsetToUnreal = [](const FVector& PMXVector) -> FVector3f
	{
		FVector3f TempPos(PMXVector.Z * 8.0f, PMXVector.X * 8.0f, PMXVector.Y * 8.0f);
		return FVector3f(TempPos.Y, -TempPos.X, TempPos.Z);
	};

	for (const FPMXMorph& Morph : PMXInfo.ModelMorphs)
	{
		if (Morph.MorphType != 1)
		{
			++SkippedMorphCount;
			continue;
		}

		const FString PreferredName = !Morph.NameJP.IsEmpty() ? Morph.NameJP : Morph.NameEN;
		if (PreferredName.IsEmpty())
		{
			++SkippedMorphCount;
			continue;
		}

		TArray<FMorphTargetDelta> Deltas;
		Deltas.Reserve(Morph.Vertices.Num());

		for (const FPMXMorphVertex& MorphVertex : Morph.Vertices)
		{
			if (!PMXInfo.ModelVertices.IsValidIndex(MorphVertex.VertexIndex))
			{
				continue;
			}

			FMorphTargetDelta Delta;
			Delta.SourceIdx = static_cast<uint32>(MorphVertex.VertexIndex);
			Delta.PositionDelta = ConvertMorphOffsetToUnreal(MorphVertex.PositionOffset);
			Delta.TangentZDelta = FVector3f::ZeroVector;

			if (!Delta.PositionDelta.IsNearlyZero())
			{
				Deltas.Add(Delta);
			}
		}

		if (Deltas.Num() == 0)
		{
			++SkippedMorphCount;
			continue;
		}

		const FName MorphTargetName(*PreferredName);
		UMorphTarget* MorphTarget = SkeletalMesh->FindMorphTarget(MorphTargetName);
		if (MorphTarget == nullptr)
		{
			MorphTarget = NewObject<UMorphTarget>(SkeletalMesh, MorphTargetName);
		}

		if (MorphTarget == nullptr)
		{
			++SkippedMorphCount;
			continue;
		}

		MorphTarget->PopulateDeltas(Deltas, 0, BaseLODModel.Sections, false, false, PositionThreshold);
		if (MorphTarget->HasValidData())
		{
			bNeedInitMorphTargets |= SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
			++ImportedMorphCount;
		}
		else
		{
			++SkippedMorphCount;
		}
	}

	if (bNeedInitMorphTargets)
	{
		SkeletalMesh->InitMorphTargetsAndRebuildRenderData();
	}

	UE_LOG(LogTemp, Log, TEXT("PMX Morph: imported %d vertex morph targets, skipped %d non-imported morphs."), ImportedMorphCount, SkippedMorphCount);
	return ImportedMorphCount;
#else
	(void)SkeletalMesh;
	(void)PMXInfo;
	return 0;
#endif
}
UTexture2D* CreateTextureFromFile(const FString& TexturePath, const FString& OutPath, const FString& AssetName) {

	if (!FPaths::FileExists(TexturePath))
	{
		UE_LOG(LogTemp, Warning, TEXT("Texture file does not exist: %s"), *TexturePath);
		return nullptr;
	}

	UE_LOG(LogTemp, Warning, TEXT("=== CreateTextureFromFile Debug ==="));
	UE_LOG(LogTemp, Warning, TEXT("原始资源名: %s"), *AssetName);

	FString CleanAssetName = FixMMDName(AssetName, TEXT("M_"));


	UE_LOG(LogTemp, Warning, TEXT("清理后的资源名: %s"), *CleanAssetName);

	FString SafeOutPath = FixMMDName(OutPath);
	if (!SafeOutPath.EndsWith(TEXT("/"))) {
		SafeOutPath += TEXT("/");
	}
	FString PackageName = SafeOutPath + CleanAssetName;

	PackageName = PackageName.Replace(TEXT("//"), TEXT("/"));

	UE_LOG(LogTemp, Warning, TEXT("最终包名: %s"), *PackageName);

	if (PackageName.Contains(TEXT(" "))) {
		UE_LOG(LogTemp, Error, TEXT("包名仍然包含空格，这会导致错误: %s"), *PackageName);
		return nullptr;
	}
	UTextureFactory::SuppressImportOverwriteDialog(true);
	UTextureFactory* TextureFactory = NewObject<UTextureFactory>();
	TextureFactory->bCreateMaterial = false;

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *TexturePath)) {
		UE_LOG(LogTemp, Warning, TEXT("Failed to load texture file: %s"), *TexturePath);
		return nullptr;
	}

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package) {
		UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *PackageName);
		return nullptr;
	}

	const FString Extension = FPaths::GetExtension(TexturePath).ToLower();
	if (Extension == TEXT("bmp"))
	{
		TArray<uint8> DecodedPixels;
		int32 Width = 0;
		int32 Height = 0;
		if (DecodeBmpToBGRA8(FileData, DecodedPixels, Width, Height))
		{
			UTexture2D* BmpTexture = CreateTextureFromBGRA8(Package, CleanAssetName, DecodedPixels, Width, Height);
			if (BmpTexture)
			{
				FAssetRegistryModule::AssetCreated(BmpTexture);
				Package->MarkPackageDirty();
				UE_LOG(LogTemp, Log, TEXT("Successfully imported BMP texture: %s (%dx%d)"), *PackageName, Width, Height);
				return BmpTexture;
			}
		}
	}

	const uint8* FileBuffer = FileData.GetData();

	UTexture2D* ImportedTexture = Cast<UTexture2D>(TextureFactory->FactoryCreateBinary(
		UTexture2D::StaticClass(),
		Package,
		FName(*CleanAssetName),  // 使用清理后的名称
		RF_Public | RF_Standalone,
		nullptr,
		*FPaths::GetExtension(TexturePath),
		FileBuffer,
		FileBuffer + FileData.Num(),
		nullptr)
	);

	if (ImportedTexture) {
		FAssetRegistryModule::AssetCreated(ImportedTexture);
		Package->MarkPackageDirty();
		UE_LOG(LogTemp, Log, TEXT("Successfully imported texture: %s"), *PackageName);
	}
	else {
		if (Extension == TEXT("bmp"))
		{
			TArray<uint8> DecodedPixels;
			int32 Width = 0;
			int32 Height = 0;
			if (DecodeBmpToBGRA8(FileData, DecodedPixels, Width, Height))
			{
				ImportedTexture = CreateTextureFromBGRA8(Package, CleanAssetName, DecodedPixels, Width, Height);
				if (ImportedTexture)
				{
					FAssetRegistryModule::AssetCreated(ImportedTexture);
					Package->MarkPackageDirty();
					UE_LOG(LogTemp, Log, TEXT("Successfully imported BMP texture via fallback: %s (%dx%d)"), *PackageName, Width, Height);
				}
			}
		}

		if (!ImportedTexture)
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to import texture: %s"), *PackageName);
		}
	}

	return ImportedTexture;
}

static void SetScalarParameterIfPresent(UMaterialInstanceConstant* MaterialInstance, const FName& ParameterName, float Value)
{
	FMaterialParameterInfo ParamInfo(ParameterName);
	MaterialInstance->SetScalarParameterValueEditorOnly(ParamInfo, Value);
}

static void SetVectorParameterIfPresent(UMaterialInstanceConstant* MaterialInstance, const FName& ParameterName, const FLinearColor& Value)
{
	FMaterialParameterInfo ParamInfo(ParameterName);
	MaterialInstance->SetVectorParameterValueEditorOnly(ParamInfo, Value);
}

FString GetMaterialTexturePath(const FPMXMaterial& Material, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
	if (Material.TextureIndex >= 0 && Material.TextureIndex < PMXInfo.ModelTextureCount) {
		FString PMXDirectory = PMXFilePath;

		FString RelativeTexturePath = PMXInfo.ModelTexturePaths[Material.TextureIndex];
		// 使用 FPaths::Combine 安全拼接路径
		FString FullTexturePath = FPaths::Combine(PMXDirectory, RelativeTexturePath);

		UE_LOG(LogTemp, Warning, TEXT("拼接后的完整路径: '%s'"), *FullTexturePath);
		UE_LOG(LogTemp, Warning, TEXT("文件是否存在: %s"), FPaths::FileExists(FullTexturePath) ? TEXT("是") : TEXT("否"));

		return FullTexturePath;
	}
	else if (Material.TextureIndex == -1) {
		// 没有贴图
		return FString();
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("Invalid texture index %d for material %s"), Material.TextureIndex, *Material.NameEN);
		return FString();
	}
}
UMaterialInterface* CreateMaterialFromTexture(UTexture2D& Texture2D, const FPMXMaterial& PMXMaterial, const FString& MaterialName, const FString& OutPath) {

	static const FString BaseMaterialPath = TEXT("/Ue5MMDTools/Resources/MaterialInstance/Mat_MMD_Base.Mat_MMD_Base");
	UMaterial* BaseMaterial = Cast<UMaterial>(StaticLoadObject(UMaterial::StaticClass(), nullptr, *BaseMaterialPath));

	if (!BaseMaterial) {
		UE_LOG(LogTemp, Error, TEXT("Failed to load base material from path: %s"), *BaseMaterialPath);
		return nullptr;
	}

	FString CleanMaterialName = FixMMDName(MaterialName, TEXT("M_"));

	FString SafeOutPath = OutPath;
	if (!SafeOutPath.EndsWith(TEXT("/")))
		SafeOutPath += TEXT("/");
	FString PackageName = SafeOutPath + CleanMaterialName;
	PackageName = PackageName.Replace(TEXT("//"), TEXT("/"));
	PackageName = PackageName.Replace(TEXT(" "), TEXT("_"));

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("CreatePackage failed: %s"), *PackageName);
		return nullptr;
	}
	UMaterialInstanceConstant* MaterialInstance = NewObject<UMaterialInstanceConstant>(Package, *CleanMaterialName, RF_Public | RF_Standalone);
	MaterialInstance->SetParentEditorOnly(BaseMaterial);

	FMaterialParameterInfo ParamInfo("BaseColorMap");
	MaterialInstance->SetTextureParameterValueEditorOnly(ParamInfo, &Texture2D);

	const float MaterialAlpha = FMath::Clamp(PMXMaterial.DiffuseColor.W, 0.0f, 1.0f);
	const bool bHasTextureAlpha = Texture2D.HasAlphaChannel();
	const bool bNeedsAlphaEvaluation = bHasTextureAlpha || MaterialAlpha < 0.999f;
	const float AlphaClipValue = bNeedsAlphaEvaluation ? 0.01f : 0.0f;

	FMaterialInstanceBasePropertyOverrides Overrides = MaterialInstance->BasePropertyOverrides;
	Overrides.bOverride_TwoSided = true;
	Overrides.TwoSided = true;
	Overrides.bOverride_OpacityMaskClipValue = true;
	Overrides.OpacityMaskClipValue = AlphaClipValue;
	Overrides.bOverride_CastDynamicShadowAsMasked = true;
	Overrides.bCastDynamicShadowAsMasked = bNeedsAlphaEvaluation;
	if (bNeedsAlphaEvaluation)
	{
		Overrides.bOverride_BlendMode = true;
		Overrides.BlendMode = BLEND_Masked;
	}
	MaterialInstance->BasePropertyOverrides = Overrides;
	MaterialInstance->SetOverrideCastShadowAsMasked(true);
	MaterialInstance->SetCastShadowAsMasked(bNeedsAlphaEvaluation);

	SetScalarParameterIfPresent(MaterialInstance, TEXT("Alpha"), MaterialAlpha);
	SetScalarParameterIfPresent(MaterialInstance, TEXT("MaterialAlpha"), MaterialAlpha);
	SetScalarParameterIfPresent(MaterialInstance, TEXT("Opacity"), MaterialAlpha);
	SetScalarParameterIfPresent(MaterialInstance, TEXT("AlphaClip"), AlphaClipValue);
	SetScalarParameterIfPresent(MaterialInstance, TEXT("AlphaClipValue"), AlphaClipValue);
	SetScalarParameterIfPresent(MaterialInstance, TEXT("OpacityMaskClipValue"), AlphaClipValue);
	SetScalarParameterIfPresent(MaterialInstance, TEXT("MaskClipValue"), AlphaClipValue);
	SetScalarParameterIfPresent(MaterialInstance, TEXT("Cutoff"), AlphaClipValue);

	SetVectorParameterIfPresent(MaterialInstance, TEXT("DiffuseColor"), FLinearColor(PMXMaterial.DiffuseColor.X, PMXMaterial.DiffuseColor.Y, PMXMaterial.DiffuseColor.Z, MaterialAlpha));
	SetVectorParameterIfPresent(MaterialInstance, TEXT("AmbientColor"), FLinearColor(PMXMaterial.AmbientColor.X, PMXMaterial.AmbientColor.Y, PMXMaterial.AmbientColor.Z, 1.0f));
	SetVectorParameterIfPresent(MaterialInstance, TEXT("SpecularColor"), FLinearColor(PMXMaterial.SpecularColor.X, PMXMaterial.SpecularColor.Y, PMXMaterial.SpecularColor.Z, PMXMaterial.SpecularPower));

	FAssetRegistryModule::AssetCreated(MaterialInstance);
	MaterialInstance->PostEditChange();
	Package->MarkPackageDirty();

	return MaterialInstance;
}

#pragma endregion
#pragma region 顶点
FVector3f ConvertPMXVectorToUnreal(const FVector& PMXVector) {
	FVector3f TempPos(PMXVector.Z * 8.0f, PMXVector.X * 8.0f, PMXVector.Y * 8.0f);

	return FVector3f(TempPos.Y, -TempPos.X, TempPos.Z);
}
FVector3f ConvertPMXBonePositionToUnreal(const FVector& PMXPosition, float Scale = 8.0f) {
	FVector3f TempPos(PMXPosition.Z * Scale, PMXPosition.X * Scale, PMXPosition.Y * Scale);

	return FVector3f(TempPos.Y, -TempPos.X, TempPos.Z);
}
static FVector3f ConvertPMXNormalToUnreal(const FVector& PMXNormal)
{
	FVector3f Normal(-PMXNormal.Z, -PMXNormal.X, -PMXNormal.Y);
	const float LenSq = Normal.SizeSquared();
	if (!FMath::IsFinite(LenSq) || LenSq <= KINDA_SMALL_NUMBER)
	{
		return FVector3f(0, 0, 1);
	}
	return Normal * FMath::InvSqrt(LenSq);
}

static FVector3f GetFallbackFaceNormal(const FVector3f& P0, const FVector3f& P1, const FVector3f& P2)
{
	FVector3f FaceNormal = FVector3f::CrossProduct(P2 - P0, P1 - P0);
	const float LenSq = FaceNormal.SizeSquared();
	if (!FMath::IsFinite(LenSq) || LenSq <= KINDA_SMALL_NUMBER)
	{
		return FVector3f(0, 0, 1);
	}
	return FaceNormal * FMath::InvSqrt(LenSq);
}

static FVector3f SanitizePMXNormal(const FVector3f& Normal, const FVector3f& FallbackFaceNormal, int32& OutZeroFixedCount, int32& OutFlippedCount)
{
	const float LenSq = Normal.SizeSquared();
	if (!FMath::IsFinite(LenSq) || LenSq <= KINDA_SMALL_NUMBER)
	{
		++OutZeroFixedCount;
		return FallbackFaceNormal;
	}

	FVector3f Result = Normal * FMath::InvSqrt(LenSq);
	if (FVector3f::DotProduct(Result, FallbackFaceNormal) < -0.85f)
	{
		++OutFlippedCount;
		Result = -Result;
	}
	return Result;
}

static int32 AddPMXWedge(FSkeletalMeshImportData& ImportData, const FPMXVertex& Vertex, int32 VertexIndex)
{
	SkeletalMeshImportData::FVertex Wedge;
	Wedge.VertexIndex = VertexIndex;
	Wedge.UVs[0] = FVector2f(Vertex.UV.X, Vertex.UV.Y);
	for (int32 UVIndex = 0; UVIndex < Vertex.AdditionalUVs.Num() && UVIndex < 7; ++UVIndex)
	{
		if (Vertex.AdditionalUVs.IsValidIndex(UVIndex))
		{
			Wedge.UVs[UVIndex + 1] = FVector2f(Vertex.AdditionalUVs[UVIndex].X, Vertex.AdditionalUVs[UVIndex].Y);
		}
	}
	Wedge.Color = FColor::White;
	return ImportData.Wedges.Add(Wedge);
}
#pragma endregion


void LoadPMXImportData(FSkeletalMeshImportData& PMXImportData, const PMXDatas& PMXInfo, const FString& PMXFilePath) {
	FString PMXPath = FPaths::GetPath(PMXFilePath);
	FString PMXModelName = FPaths::GetBaseFilename(PMXFilePath);
	PMXImportData.bHasNormals = true;
	PMXImportData.bHasTangents = false;
	PMXImportData.bHasVertexColors = false;
	PMXImportData.NumTexCoords = 1 + PMXInfo.PMXGlobals.ExtraUV; // 主UV加上额外的UV
	PMXImportData.MaxMaterialIndex = PMXInfo.ModelMaterialCount;

#pragma region 材质
	PMXImportData.Materials.Reserve(PMXInfo.ModelMaterials.Num());

	// 确保至少有一个默认材质
	if (PMXInfo.ModelMaterials.Num() == 0) {
		SkeletalMeshImportData::FMaterial DefaultMaterial;
		DefaultMaterial.MaterialImportName = TEXT("DefaultMaterial");
		DefaultMaterial.Material = nullptr; // 使用引擎默认材质
		PMXImportData.Materials.Add(DefaultMaterial);
		UE_LOG(LogTemp, Warning, TEXT("没有材质，创建默认材质"));
	}
	else {
		for (int32 MaterialIndex = 0; MaterialIndex < PMXInfo.ModelMaterials.Num(); ++MaterialIndex) {
			const auto& Material = PMXInfo.ModelMaterials[MaterialIndex];
			SkeletalMeshImportData::FMaterial MaterialData;

			// 清理材质名称
			FString CleanMaterialName = Material.NameEN.IsEmpty() ? Material.NameJP : Material.NameEN;
			CleanMaterialName = FixMMDName(CleanMaterialName, TEXT("M_"));

			MaterialData.MaterialImportName = CleanMaterialName;

			FString TexturePath = GetMaterialTexturePath(Material, PMXInfo, PMXPath);

			// 只有当纹理存在时才创建材质
			if (!TexturePath.IsEmpty() && FPaths::FileExists(TexturePath)) {
				FString CleanFileName = FPaths::GetCleanFilename(TexturePath);
				// 也要清理文件名
				CleanFileName = CleanFileName.Replace(TEXT(" "), TEXT("_"));
				CleanFileName = CleanFileName.Replace(TEXT("-"), TEXT("_"));

				UTexture2D* Texture = CreateTextureFromFile(TexturePath,
					FString("/Game/MMDModels/") + PMXModelName + FString("/Textures"),
					CleanFileName);

				if (Texture) {
					MaterialData.Material = CreateMaterialFromTexture(*Texture,
						Material,
						CleanMaterialName,
						FString("/Game/MMDModels/") + PMXModelName + FString("/Materials"));
				}
			}

			PMXImportData.Materials.Add(MaterialData);
		}
	}

	PMXImportData.MaxMaterialIndex = FMath::Max(0, PMXImportData.Materials.Num() - 1);

	UE_LOG(LogTemp, Warning, TEXT("材质处理完成，共 %d 个材质"), PMXImportData.Materials.Num());
#pragma endregion

#pragma region Wedges

	PMXImportData.Wedges.Reserve(PMXInfo.ModelIndicesCount);
#pragma endregion

#pragma region 顶点Points
	PMXImportData.Points.Reserve(PMXInfo.ModelVertices.Num());

	for (int32 i = 0; i < PMXInfo.ModelVertices.Num(); ++i) {
		const FPMXVertex& Vertex = PMXInfo.ModelVertices[i];
		PMXImportData.Points.Add(ConvertPMXVectorToUnreal(Vertex.Position));
	}
#pragma endregion

#pragma region 面
	PMXImportData.Faces.Reserve(PMXInfo.ModelIndicesCount / 3);
	int32 BaseIndex = 0;
	int32 ZeroNormalCount = 0;
	int32 FlippedNormalCount = 0;
	for (int32 MatIndex = 0; MatIndex < PMXInfo.ModelMaterials.Num(); MatIndex++)
	{
		const FPMXMaterial& Material = PMXInfo.ModelMaterials[MatIndex];
		int32 FaceIndexCount = Material.FaceIndexCount;
		int32 TriangleCount = FaceIndexCount / 3;

		for (int32 f = 0; f < TriangleCount; f++)
		{
			int32 w0 = BaseIndex + f * 3 + 0;
			int32 w1 = BaseIndex + f * 3 + 1;
			int32 w2 = BaseIndex + f * 3 + 2;

			int32 vi0 = PMXInfo.ModelIndices[w0];
			int32 vi1 = PMXInfo.ModelIndices[w1];
			int32 vi2 = PMXInfo.ModelIndices[w2];

			// 退化剔除
			if (vi0 == vi1 || vi1 == vi2 || vi0 == vi2)
				continue;
			if (vi0 < 0 || vi1 < 0 || vi2 < 0 ||
				vi0 >= PMXInfo.ModelVertices.Num() ||
				vi1 >= PMXInfo.ModelVertices.Num() ||
				vi2 >= PMXInfo.ModelVertices.Num())
				continue;

			SkeletalMeshImportData::FTriangle Tri;
			Tri.WedgeIndex[0] = AddPMXWedge(PMXImportData, PMXInfo.ModelVertices[vi0], vi0);
			Tri.WedgeIndex[1] = AddPMXWedge(PMXImportData, PMXInfo.ModelVertices[vi1], vi1);
			Tri.WedgeIndex[2] = AddPMXWedge(PMXImportData, PMXInfo.ModelVertices[vi2], vi2);
			Tri.MatIndex = MatIndex;
			Tri.AuxMatIndex = 0;
			Tri.SmoothingGroups = 0;

			const FVector& N0 = PMXInfo.ModelVertices[vi0].Normal;
			const FVector& N1 = PMXInfo.ModelVertices[vi1].Normal;
			const FVector& N2 = PMXInfo.ModelVertices[vi2].Normal;
			const FVector3f P0 = PMXImportData.Points[vi0];
			const FVector3f P1 = PMXImportData.Points[vi1];
			const FVector3f P2 = PMXImportData.Points[vi2];
			const FVector3f FaceNormal = GetFallbackFaceNormal(P0, P1, P2);

			Tri.TangentZ[0] = SanitizePMXNormal(ConvertPMXNormalToUnreal(N0), FaceNormal, ZeroNormalCount, FlippedNormalCount);
			Tri.TangentZ[1] = SanitizePMXNormal(ConvertPMXNormalToUnreal(N1), FaceNormal, ZeroNormalCount, FlippedNormalCount);
			Tri.TangentZ[2] = SanitizePMXNormal(ConvertPMXNormalToUnreal(N2), FaceNormal, ZeroNormalCount, FlippedNormalCount);

			PMXImportData.Faces.Add(Tri);
		}
		BaseIndex += FaceIndexCount;
	}

	PMXImportData.ComputeSmoothGroupFromNormals();
	UE_LOG(LogTemp, Log, TEXT("Original PMX normals applied. Triangles=%d Wedges=%d ZeroFixed=%d Flipped=%d"),
		PMXImportData.Faces.Num(), PMXImportData.Wedges.Num(), ZeroNormalCount, FlippedNormalCount);
#pragma endregion

#pragma region 骨骼Bone
	// 建立一个集合防止骨骼重名
	TSet<FString> BoneNameSet;
	{
		SkeletalMeshImportData::FBone Root;
		Root.Name = TEXT("Root");
		Root.ParentIndex = INDEX_NONE;
		Root.NumChildren = 0;
		Root.BonePos.Transform = FTransform3f::Identity;
		Root.BonePos.Length = Root.BonePos.XSize = Root.BonePos.YSize = 1;
		PMXImportData.RefBonesBinary.Add(Root);
		BoneNameSet.Add(Root.Name);
	}

	for (int32 i = 0; i < PMXInfo.ModelBoneCount; ++i) {
		const FPMXBone& Bone = PMXInfo.ModelBones[i];
		SkeletalMeshImportData::FBone NewBone;

		;

		// 确保名字唯一
		int32 Suffix = 1;
		FString UniqueName = Bone.NameJP;
		while (BoneNameSet.Contains(UniqueName)) {
			UniqueName = Bone.NameJP + FString::Printf(TEXT("_%d"), Suffix++);
		}
		BoneNameSet.Add(UniqueName);

		NewBone.Name = UniqueName;
		int32 Parent = Bone.ParentBoneIndex;
		if (Parent >= 0 && Parent < PMXInfo.ModelBoneCount && Parent != i) {
			NewBone.ParentIndex = Parent + 1; // +1 因为Root
		}
		else {
			NewBone.ParentIndex = 0; // 默认挂在Root
		}
		FVector3f BoneGlobalPos = ConvertPMXBonePositionToUnreal(Bone.Position);

		// 计算相对父骨骼的位置（local）
		FVector3f BoneLocalPos = BoneGlobalPos;
		if (Bone.ParentBoneIndex >= 0 && Bone.ParentBoneIndex < PMXInfo.ModelBoneCount) {
			const FPMXBone& ParentPMXBone = PMXInfo.ModelBones[Bone.ParentBoneIndex];
			FVector3f ParentGlobalPos = ConvertPMXBonePositionToUnreal(ParentPMXBone.Position);
			BoneLocalPos = BoneGlobalPos - ParentGlobalPos;
		}
		NewBone.BonePos.Transform = FTransform3f(FQuat4f::Identity, BoneLocalPos);
		NewBone.BonePos.Length = NewBone.BonePos.XSize = NewBone.BonePos.YSize = 1;
		PMXImportData.RefBonesBinary.Add(NewBone);
	}
#pragma endregion

#pragma region 骨骼权重RawBoneInfluence
	TArray<TArray<TPair<int32, float>>> VertexInfluences;
	VertexInfluences.SetNum(PMXInfo.ModelVertexCount);

	for (int32 i = 0; i < PMXInfo.ModelVertexCount; ++i) {
		const FPMXVertex& Vertex = PMXInfo.ModelVertices[i];
		const FPMXVertexWeight& Weight = Vertex.Weight;

		switch (Weight.WeightDeformType)
		{
		case 0: // BDEF1
			if (Weight.BoneIndices[0] >= 0) {
				VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[0] + 1, 1.0f));
			}
			break;
		case 1: // BDEF2
		case 3: // SDEF (basically two bones)
		{
			if (Weight.BoneIndices[0] >= 0) {
				float w0 = Weight.Weights[0];
				VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[0] + 1, w0));
			}
			if (Weight.BoneIndices[1] >= 0) {
				float w1 = 1.0f - Weight.Weights[0];
				VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[1] + 1, w1));
			}
		}
		break;
		case 2: // BDEF4
		case 4: // QDEF
			for (int j = 0; j < 4; ++j) {
				if (Weight.BoneIndices[j] >= 0 && Weight.Weights[j] > 0.0f) {
					VertexInfluences[i].Add(TPair<int32, float>(Weight.BoneIndices[j] + 1, Weight.Weights[j]));
				}
			}
			break;
		default:
			// 绑定到 Root（0）
			VertexInfluences[i].Add(TPair<int32, float>(0, 1.0f));
			break;
		}
	}

	// 归一化并写入 PMXImportData.Influences
	for (int32 i = 0; i < VertexInfluences.Num(); ++i) {
		float Sum = 0.0f;
		for (auto& P : VertexInfluences[i]) Sum += P.Value;
		if (Sum <= 0.0f) {
			// 保底绑定到 Root
			SkeletalMeshImportData::FRawBoneInfluence Inf;
			Inf.VertexIndex = i;
			Inf.BoneIndex = 0;
			Inf.Weight = 1.0f;
			PMXImportData.Influences.Add(Inf);
			continue;
		}
		// 写入并归一化
		for (auto& P : VertexInfluences[i]) {
			SkeletalMeshImportData::FRawBoneInfluence Inf;
			Inf.VertexIndex = i;              // 顶点索引（对应 PMXImportData.Points 的索引）
			Inf.BoneIndex = P.Key;            // 已经 +1 以对应 Root 在前的 RefBonesBinary
			Inf.Weight = P.Value / Sum;
			if (Inf.Weight > KINDA_SMALL_NUMBER) {
				PMXImportData.Influences.Add(Inf);
			}
		}
	}
#pragma endregion

#pragma region 顶点映射
	PMXImportData.PointToRawMap.Reserve(PMXInfo.ModelVertices.Num());
	for (int32 i = 0; i < PMXInfo.ModelVertices.Num(); ++i) {
		PMXImportData.PointToRawMap.Add(i);
	}
#pragma endregion



#pragma region MeshInfo
	SkeletalMeshImportData::FMeshInfo MeshInfo;
	MeshInfo.Name = FName(*PMXModelName);
	MeshInfo.NumVertices = PMXInfo.ModelVertices.Num();
	MeshInfo.StartImportedVertex = 0;
	PMXImportData.MeshInfos.Add(MeshInfo);
#pragma endregion

	UE_LOG(LogTemp, Warning, TEXT("LoadPMXImportData 完成: 顶点=%d, 面=%d, 骨骼=%d, Influences=%d"),
		PMXImportData.Points.Num(), PMXImportData.Faces.Num(),
		PMXImportData.RefBonesBinary.Num(), PMXImportData.Influences.Num());
}


USkeletalMesh* TMMDMeshBuilder::BuildSkeletalMeshFromPMX(const PMXDatas& PMXInfo, const FString& PackagePath, const FString& AssetName, const FString& PMXFilePath)
{
	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString CleanAssetName = FixMMDName(AssetName);

	UE_LOG(LogTemp, Warning, TEXT("=== 开始构建骨骼网格：%s -> %s ==="), *AssetName, *CleanAssetName);

	FString BasePath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/");
	FString ModelPath = BasePath + TEXT("Model/");
	FString SkeletonPath = BasePath + TEXT("Skeleton/");

	FString PackageName = ModelPath + CleanAssetName;
	UPackage* Package = CreatePackage(*PackageName);
	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(Package, *CleanAssetName, RF_Public | RF_Standalone);

	FSkeletalMeshImportData PMXImportData;
	LoadPMXImportData(PMXImportData, PMXInfo, PMXFilePath);

	if (PMXImportData.Points.Num() == 0 || PMXImportData.Faces.Num() == 0) {
		UE_LOG(LogTemp, Error, TEXT("导入数据无效"));
		return nullptr;
	}

	FString SkeletonName = CleanAssetName + TEXT("_Skeleton");
	FString SkeletonPackageName = SkeletonPath + SkeletonName;
	UPackage* SkeletonPackage = CreatePackage(*SkeletonPackageName);
	USkeleton* Skeleton = NewObject<USkeleton>(SkeletonPackage, *SkeletonName, RF_Public | RF_Standalone);

	int32 SkeletalDepth = 0;
	FReferenceSkeleton RefSkeleton;

	SkeletalMeshImportUtils::ProcessImportMeshInfluences(PMXImportData, CleanAssetName);
	SkeletalMeshImportUtils::ProcessImportMeshMaterials(SkeletalMesh->GetMaterials(), PMXImportData);
	SkeletalMeshImportUtils::ProcessImportMeshSkeleton(Skeleton, RefSkeleton, SkeletalDepth, PMXImportData);

	SkeletalMesh->SetRefSkeleton(RefSkeleton);

	if (RefSkeleton.GetNum() > 0) {
		Skeleton->RecreateBoneTree(SkeletalMesh);
	}

	if (SkeletalMesh->GetLODNum() == 0)
	{
		FSkeletalMeshLODInfo LODInfo;
		LODInfo.ScreenSize.Default = 1.0f;
		LODInfo.LODHysteresis = 0.02f;

		LODInfo.BuildSettings.bRecomputeNormals = false;
		LODInfo.BuildSettings.bRecomputeTangents = true;
		LODInfo.BuildSettings.bUseMikkTSpace = true;
		LODInfo.BuildSettings.bComputeWeightedNormals = false;
		LODInfo.BuildSettings.bRemoveDegenerates = true;
		LODInfo.BuildSettings.bUseFullPrecisionUVs = false;
		LODInfo.BuildSettings.bUseHighPrecisionTangentBasis = false;

		LODInfo.bAllowCPUAccess = true;
		LODInfo.bSupportUniformlyDistributedSampling = false;

		SkeletalMesh->AddLODInfo(LODInfo);
		UE_LOG(LogTemp, Warning, TEXT("LODInfo 创建完成"));
	}

	FSkeletalMeshModel* ImportedModel = SkeletalMesh->GetImportedModel();
	if (!ImportedModel) {
		SkeletalMesh->AllocateResourceForRendering();
		ImportedModel = SkeletalMesh->GetImportedModel();
	}

	if (ImportedModel->LODModels.Num() == 0) {
		ImportedModel->LODModels.Add(new FSkeletalMeshLODModel());
	}

	FSkeletalMeshLODModel& LODModel = ImportedModel->LODModels[0];

	// 使用 MeshUtilities 构建骨骼网格
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

	IMeshUtilities::MeshBuildOptions BuildOptions;
	BuildOptions.bComputeNormals = false;
	BuildOptions.bComputeTangents = true;
	BuildOptions.bUseMikkTSpace = true;
	BuildOptions.bComputeWeightedNormals = false;
	BuildOptions.bRemoveDegenerateTriangles = true;

	TArray<FVector3f> LODPoints;
	TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
	TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
	TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
	TArray<int32> LODPointToRawMap;

	PMXImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

	UE_LOG(LogTemp, Warning, TEXT("LOD数据: 顶点=%d, 楔形点=%d, 面=%d, 影响=%d"),
		LODPoints.Num(), LODWedges.Num(), LODFaces.Num(), LODInfluences.Num());

	bool bBuildSuccess = MeshUtilities.BuildSkeletalMesh(
		LODModel,
		CleanAssetName,
		RefSkeleton,
		LODInfluences,
		LODWedges,
		LODFaces,
		LODPoints,
		LODPointToRawMap,
		BuildOptions
	);

	if (!bBuildSuccess) {
		UE_LOG(LogTemp, Error, TEXT("骨骼网格构建失败"));
		return nullptr;
	}

	for (FSkelMeshSection& Section : LODModel.Sections)
	{
		if (PMXInfo.ModelMaterials.IsValidIndex(Section.MaterialIndex))
		{
			const FPMXMaterial& PMXMaterial = PMXInfo.ModelMaterials[Section.MaterialIndex];
			Section.bCastShadow = (PMXMaterial.DrawFlags & PMX_DRAW_CAST_SHADOW) != 0;
		}
	}

	LODModel.NumTexCoords = FMath::Max<uint32>(1, PMXImportData.NumTexCoords);


	SkeletalMesh->SetSkeleton(Skeleton);
	Skeleton->SetPreviewMesh(SkeletalMesh);

	SkeletalMesh->CalculateInvRefMatrices();

	const int32 ImportedMorphTargets = BuildMorphTargetsFromPMX(SkeletalMesh, PMXInfo);
	if (ImportedMorphTargets > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("SkeletalMesh morph targets registered: %d"), ImportedMorphTargets);
	}

	const TArray<FMatrix44f>& RefBasesInvMatrix = SkeletalMesh->GetRefBasesInvMatrix();
	UE_LOG(LogTemp, Warning, TEXT("RefBasesInvMatrix 数量: %d"), RefBasesInvMatrix.Num());

	if (RefBasesInvMatrix.Num() == 0) {
		UE_LOG(LogTemp, Error, TEXT("RefBasesInvMatrix 未正确初始化！"));
	}

	if (PMXImportData.Points.Num() > 0) {
		FBox BoundingBox(ForceInit);
		for (const FVector3f& Point : PMXImportData.Points) {
			BoundingBox += FVector(Point);
		}
		FBoxSphereBounds ActualBounds(BoundingBox);
		SkeletalMesh->SetImportedBounds(ActualBounds);
	}
	else {
		FBoxSphereBounds DefaultBounds(FBox(FVector(-100, -100, -100), FVector(100, 100, 100)));
		SkeletalMesh->SetImportedBounds(DefaultBounds);
	}

	// 完成构建和初始化
	SkeletalMesh->PostEditChange();
	SkeletalMesh->MarkPackageDirty();

	// 注册资源
	FAssetRegistryModule::AssetCreated(SkeletalMesh);
	FAssetRegistryModule::AssetCreated(Skeleton);
	SkeletonPackage->MarkPackageDirty();

	UE_LOG(LogTemp, Log, TEXT("骨骼网格创建成功: %s"), *PackageName);

	return SkeletalMesh;
}

UIKRigDefinition* TMMDMeshBuilder::BuildIKRigFromPMX(USkeletalMesh* SkeletalMesh, const FString& PMXFilePath)
{
#if WITH_EDITOR
	if (!SkeletalMesh) {
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null"));
		return nullptr;
	}
	auto FindMMDBone = [SkeletalMesh](const TArray<FString>& PossibleNames) -> FName
		{
			const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
			for (const FString& BoneName : PossibleNames)
			{
				FName FoundName = FName(*BoneName);
				int32 BoneIndex = RefSkeleton.FindBoneIndex(FoundName);
				if (BoneIndex != INDEX_NONE)
				{
					UE_LOG(LogTemp, Log, TEXT("Found bone: %s"), *BoneName);
					return FoundName;
				}
			}
			return NAME_None;
		};
	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString IKRigPath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString IKRigName = PMXModelName + TEXT("_IKRig");

	FString UniquePackageName, UniqueAssetName; {
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(IKRigPath + TEXT("/") + IKRigName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UIKRigDefinition* IKRig = NewObject<UIKRigDefinition>(
		Package,
		UIKRigDefinition::StaticClass(),
		FName(*UniqueAssetName),
		RF_Public | RF_Standalone
	);
	if (!IKRig)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create IKRigDefinition"));
		return nullptr;
	}

	UIKRigController* Controller = UIKRigController::GetController(IKRig);
	if (!Controller)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get IKRigController"));
		return nullptr;
	}

	Controller->SetSkeletalMesh(SkeletalMesh);
	UE_LOG(LogTemp, Log, TEXT("Set SkeletalMesh for IKRig: %s"), *SkeletalMesh->GetName());

	FName RetargetRootBone = FindMMDBone({
			TEXT("腰"),      
			TEXT("Waist"),
			TEXT("センター"), 
			TEXT("Center"),
			TEXT("下半身"),    
			TEXT("LowerBody"),
			TEXT("Hips")
		});
	if (RetargetRootBone != NAME_None)
	{
		Controller->SetRetargetRoot(RetargetRootBone);
		UE_LOG(LogTemp, Log, TEXT("✅ Set Retarget Root: %s"), *RetargetRootBone.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find Retarget Root bone"));
	}
	// 添加重定向链
	{
		struct FMMDRetargetChain
		{
			FString ChainName;
			TArray<FString> StartBoneNames;
			TArray<FString> EndBoneNames;
		};
		TArray<FMMDRetargetChain> Chains = {
			{ TEXT("Root"),        { TEXT("Root") }, { TEXT("Root") } },

			{ TEXT("Spine"),       { TEXT("上半身") }, { TEXT("上半身2") } },

			{ TEXT("Neck"),        { TEXT("首") }, { TEXT("首") } },

			{ TEXT("Head"),        { TEXT("頭") }, { TEXT("頭") } },

			{ TEXT("RightArm"),    { TEXT("右腕") }, { TEXT("右手首") } },

			// ✅ 左腕、左手首（日文）- 注意截图是"左肩"开始
			{ TEXT("LeftArm"),     { TEXT("左腕") }, { TEXT("左手首") } },

			// ✅ 右足、右足先（日文）- EX是英文
			{ TEXT("RightLeg"),    { TEXT("右足D") }, { TEXT("右足先EX") } },

			// ✅ 左足、左足先（日文）
			{ TEXT("LeftLeg"),     { TEXT("左足D") }, { TEXT("左足先EX") } },

			// ✅ 手指（日文）- 親指、人指、中指、薬指、小指
			{ TEXT("LeftThumb"),   { TEXT("左親指０") }, { TEXT("左親指２") } },
			{ TEXT("LeftIndex"),   { TEXT("左人指１") }, { TEXT("左人指３") } },
			{ TEXT("LeftMiddle"),  { TEXT("左中指１") }, { TEXT("左中指３") } },
			{ TEXT("LeftRing"),    { TEXT("左薬指１") }, { TEXT("左薬指３") } },
			{ TEXT("LeftPinky"),   { TEXT("左小指１") }, { TEXT("左小指３") } },

			{ TEXT("RightThumb"),  { TEXT("右親指０") }, { TEXT("右親指２") } },
			{ TEXT("RightIndex"),  { TEXT("右人指１") }, { TEXT("右人指３") } },
			{ TEXT("RightMiddle"), { TEXT("右中指１") }, { TEXT("右中指３") } },
			{ TEXT("RightRing"),   { TEXT("右薬指１") }, { TEXT("右薬指３") } },
			{ TEXT("RightPinky"),  { TEXT("右小指１") }, { TEXT("右小指３") } },

			// ✅ 肩（日文）
			{ TEXT("LeftClavicle"), { TEXT("左肩") }, { TEXT("左肩") } },
			{ TEXT("RightClavicle"),{ TEXT("右肩") }, { TEXT("右肩") } }
		};

		int32 ChainCount = 0;
		for (const FMMDRetargetChain& Chain : Chains) {
			FName StartBone = FindMMDBone(Chain.StartBoneNames);
			FName EndBone = FindMMDBone(Chain.EndBoneNames);

			if (StartBone != NAME_None && EndBone != NAME_None)
			{
				FName ChainFName = FName(*Chain.ChainName);

				Controller->AddRetargetChain(ChainFName, StartBone, EndBone, NAME_None);
				ChainCount++;

				UE_LOG(LogTemp, Log, TEXT("✅ Added Retarget Chain: %s (%s -> %s)"),
					*Chain.ChainName, *StartBone.ToString(), *EndBone.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find bones for chain: %s"), *Chain.ChainName);
			}
		}
	}
	// 添加IK目标
	{
		struct FMMDIKGoal
		{
			FString GoalName;
			TArray<FString> BoneNames;
		};
		TArray<FMMDIKGoal> IKGoals = {
			// 左脚IK
			{
				TEXT("LeftFoot_IK"),
				{TEXT("左足首"), TEXT("LeftAnkle"), TEXT("LeftFoot")}
			},
			// 右脚IK
			{
				TEXT("RightFoot_IK"),
				{TEXT("右足首"), TEXT("RightAnkle"), TEXT("RightFoot")}
			},
			// 左手IK
			{
				TEXT("LeftHand_IK"),
				{TEXT("左手首"), TEXT("LeftWrist"), TEXT("LeftHand")}
			},
			// 右手IK
			{
				TEXT("RightHand_IK"),
				{TEXT("右手首"), TEXT("RightWrist"), TEXT("RightHand")}
			}
		};
		int32 GoalCount = 0;
		for (const FMMDIKGoal& Goal : IKGoals)
		{
			FName GoalBone = FindMMDBone(Goal.BoneNames);

			if (GoalBone != NAME_None)
			{
				FName GoalName = FName(*Goal.GoalName);

				Controller->AddNewGoal(GoalName, GoalBone);
				GoalCount++;
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("⚠️ Could not find bone for goal: %s"), *Goal.GoalName);
			}
		}
	}
	// 创建FBIK求解器
	{
		int32 SolverIndex = Controller->AddSolver(UIKRigFBIKSolver::StaticClass());

		if (SolverIndex != INDEX_NONE)
		{
			UE_LOG(LogTemp, Log, TEXT("✅ Added FBIK Solver at index: %d"), SolverIndex);

			// 连接Goals到求解器
			TArray<FName> GoalNames = {
				TEXT("LeftFoot_IK"),
				TEXT("RightFoot_IK"),
				TEXT("LeftHand_IK"),
				TEXT("RightHand_IK")
			};

			int32 ConnectedCount = 0;
			for (const FName& GoalName : GoalNames)
			{
				bool bConnected = Controller->ConnectGoalToSolver(GoalName, SolverIndex);

				if (bConnected)
				{
					ConnectedCount++;
					UE_LOG(LogTemp, Log, TEXT("✅ Connected Goal to Solver: %s"), *GoalName.ToString());
				}
			}

			UE_LOG(LogTemp, Log, TEXT("📊 Connected %d/%d Goals to Solver"), ConnectedCount, GoalNames.Num());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("⚠️ Failed to add FBIK Solver"));
		}
	}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(IKRig);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, IKRig, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Log, TEXT("Successfully saved IKRig: %s"), *FilePath);
		UE_LOG(LogTemp, Log, TEXT("IKRig created with MMD bone mapping"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to save IKRig: %s"), *FilePath);
	}

	return IKRig;

#else
	UE_LOG(LogTemp, Error, TEXT("IKRig can only be created in the editor."));
	return nullptr;
#endif
}

UAnimBlueprint* TMMDMeshBuilder::BuildAnimBlueprint(USkeletalMesh* SkeletalMesh, const FString& PMXFilePath)
{
	if (!SkeletalMesh) {
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh is null"));
		return nullptr;
	}

	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString AnimBPPackagePath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString AnimBPName = PMXModelName + TEXT("_AnimBP");

	USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	if (!Skeleton) {
		UE_LOG(LogTemp, Error, TEXT("TargetMesh has no skeleton"));
		return nullptr;
	}

	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(AnimBPPackagePath + TEXT("/") + AnimBPName, TEXT(""), UniquePackageName, UniqueAssetName);
	}
	UPackage* Package = CreatePackage(*UniquePackageName);
	if (!Package) {
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UAnimBlueprintFactory* Factory = NewObject<UAnimBlueprintFactory>();
	Factory->TargetSkeleton = Skeleton;
	//Factory->ParentClass = UMMDAnimInstance::StaticClass();
	Factory->PreviewSkeletalMesh = SkeletalMesh;

	UAnimBlueprint* NewAnimBP = Cast<UAnimBlueprint>(Factory->FactoryCreateNew(
		UAnimBlueprint::StaticClass(),
		Package,
		*UniqueAssetName,
		RF_Public | RF_Standalone,
		nullptr,
		GWarn
	));

	if (!NewAnimBP) {
		UE_LOG(LogTemp, Error, TEXT("Failed to create AnimBlueprint"));
		return nullptr;
	}
	//if (NewAnimBP->ParentClass != UMMDAnimInstance::StaticClass()) {
	//	NewAnimBP->ParentClass = UMMDAnimInstance::StaticClass();
	//}
	FKismetEditorUtilities::CompileBlueprint(NewAnimBP);

	// Set PMX source path on AnimInstance CDO for preview auto physics rebuild
    //if (NewAnimBP->GeneratedClass)
    //{
    //    //if (UMMDAnimInstance* AnimCDO = Cast<UMMDAnimInstance>(NewAnimBP->GeneratedClass->GetDefaultObject()))
    //    //{
    //    //    AnimCDO->SetSourcePMXFilePath(PMXFilePath);
    //    //    AnimCDO->Modify();
    //    //    UE_LOG(LogTemp, Verbose, TEXT("[TMMDMeshBuilder] Set SourcePMXFilePath on AnimInstance CDO: %s"), *PMXFilePath);
    //    //}
    //}

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(NewAnimBP);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.Error = GError;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, nullptr, *FilePath, SaveArgs)) {
		UE_LOG(LogTemp, Log, TEXT("Successfully created and saved AnimBlueprint: %s"), *FilePath);
	}
	else {
		UE_LOG(LogTemp, Warning, TEXT("AnimBlueprint created but failed to save: %s"), *FilePath);
	}

	return NewAnimBP;
}
#if WITH_EDITOR
UIKRetargeter* TMMDMeshBuilder::BuildIKRetargeterFromPMX(UIKRigDefinition* IKRigTarget, const FString& PMXFilePath)
{
	if (!IKRigTarget) {
		UE_LOG(LogTemp, Error, TEXT("IKRigTarget is null"));
		return nullptr;
	}

	FString MannequinIKRigPath = FString("/Engine/Characters/Mannequins/Rigs/IK_Mannequin.IK_Mannequin");

	UIKRigDefinition* IKRigSource = LoadObject<UIKRigDefinition>(nullptr, *MannequinIKRigPath);
	if (!IKRigSource)
	{
		UE_LOG(LogTemp, Warning, TEXT("Failed to load Mannequin IKRig from: %s"), *MannequinIKRigPath);
		UE_LOG(LogTemp, Warning, TEXT("Trying alternative paths..."));

		TArray<FString> AlternativePaths = {
			TEXT("/Game/Characters/Mannequins/Rigs/IK_Mannequin"),
			TEXT("/Engine/Characters/Mannequin/Rigs/IK_Mannequin"),
			TEXT("/Script/Engine.IKRigDefinition'/Engine/Characters/Mannequins/Rigs/IK_Mannequin.IK_Mannequin'")
		};

		for (const FString& AltPath : AlternativePaths)
		{
			IKRigSource = LoadObject<UIKRigDefinition>(nullptr, *AltPath);
			if (IKRigSource)
			{
				UE_LOG(LogTemp, Log, TEXT("Loaded Mannequin IKRig from: %s"), *AltPath);
				break;
			}
		}

		if (!IKRigSource)
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to load Mannequin IKRig. Please check the path."));
			return nullptr;
		}
	}

	FString PMXModelName = FixMMDName(FPaths::GetBaseFilename(PMXFilePath));
	FString RetargeterPath = FString("/Game/MMDModels/") + PMXModelName + TEXT("/Animation");
	FString RetargeterName = PMXModelName + TEXT("_RTG_FromMannequin");

	FString UniquePackageName, UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(RetargeterPath + TEXT("/") + RetargeterName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if(!Package)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create package: %s"), *UniquePackageName);
		return nullptr;
	}

	UIKRetargeter* Retargeter = NewObject<UIKRetargeter>(
		Package,
		UIKRetargeter::StaticClass(),
		FName(*UniqueAssetName),
		RF_Public | RF_Standalone
	);

	if(!Retargeter)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to create IKRetargeter"));
		return nullptr;
	}

	UIKRetargeterController* Controller = UIKRetargeterController::GetController(Retargeter);
	if (!Controller)
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to get IKRetargeterController"));
		return nullptr;
	}

	Controller->SetIKRig(ERetargetSourceOrTarget::Source, IKRigSource);
	Controller->SetIKRig(ERetargetSourceOrTarget::Target, IKRigTarget);
	UE_LOG(LogTemp, Log, TEXT("✅ Set Source IKRig: %s"), *IKRigSource->GetName());
	UE_LOG(LogTemp, Log, TEXT("✅ Set Target IKRig: %s"), *IKRigTarget->GetName());

	Retargeter->PostEditChange();

	// 自动映射骨骼链
	Controller->AutoMapChains(EAutoMapChainType::Fuzzy,true);
	UE_LOG(LogTemp, Log, TEXT("✅ Auto-mapped chains"));
	{
		FTargetChainSettings RootSettings = Controller->GetRetargetChainSettings(TEXT("Root"));

		RootSettings.FK.TranslationMode = ERetargetTranslationMode::GloballyScaled;
		RootSettings.FK.RotationMode = ERetargetRotationMode::Interpolated;

		Controller->SetRetargetChainSettings(TEXT("Root"), RootSettings);

		UE_LOG(LogTemp, Log, TEXT("✅ Root chain: TranslationMode=GloballyScaled"));
	}
	Controller->AutoAlignAllBones(ERetargetSourceOrTarget::Target);

	Package->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(Retargeter);

	const FString FilePath = FPackageName::LongPackageNameToFilename(
		Package->GetName(),
		FPackageName::GetAssetPackageExtension()
	);

	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;

	if (UPackage::SavePackage(Package, Retargeter, *FilePath, SaveArgs))
	{
		UE_LOG(LogTemp, Log, TEXT("========================================"));
		UE_LOG(LogTemp, Log, TEXT("✅ Successfully saved IKRetargeter!"));
		UE_LOG(LogTemp, Log, TEXT("========================================"));
		UE_LOG(LogTemp, Log, TEXT("📁 Path: %s"), *UniquePackageName);
		UE_LOG(LogTemp, Log, TEXT("📥 Source: UE5 Mannequin (IK_Mannequin)"));
		UE_LOG(LogTemp, Log, TEXT("📤 Target: %s"), *IKRigTarget->GetName());
		UE_LOG(LogTemp, Log, TEXT(""));
		UE_LOG(LogTemp, Log, TEXT("🎉 You can now retarget Mannequin animations to your MMD model!"));
		UE_LOG(LogTemp, Log, TEXT("========================================"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ Failed to save IKRetargeter: %s"), *FilePath);
	}

	return Retargeter;
}

bool TMMDMeshBuilder::BuildAnimationImportContext(USkeletalMesh* SkeletalMesh, const PMXDatas* PMXData, const FString& PMXFilePath, const FString& VMDFilePath, FMMDAnimationImportContext& OutContext, FMMDAnimationImportReport* OutReport)
{
	OutContext = FMMDAnimationImportContext{};
	OutContext.SkeletalMesh = SkeletalMesh;
	OutContext.PMXData = PMXData;
	OutContext.SourcePMXFilePath = PMXFilePath;
	OutContext.SourceVMDFilePath = VMDFilePath;
	OutContext.Skeleton = SkeletalMesh ? SkeletalMesh->GetSkeleton() : nullptr;

	if (SkeletalMesh == nullptr)
	{
		if (OutReport)
		{
			AppendUniqueMessage(OutReport->Errors, TEXT("Animation import requires a valid SkeletalMesh."));
		}
		return false;
	}

	if (OutContext.Skeleton == nullptr)
	{
		if (OutReport)
		{
			AppendUniqueMessage(OutReport->Errors, TEXT("Target SkeletalMesh does not have a USkeleton."));
		}
		return false;
	}

	return true;
}

bool TMMDMeshBuilder::AnalyzeVMDAnimationImport(const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport& OutReport)
{
	OutReport = FMMDAnimationImportReport{};
	OutReport.PackagePath = BuildMMDAnimationFolderPath(Context);
	OutReport.AssetName = FixAnimAssetName(FPaths::GetBaseFilename(Context.SourceVMDFilePath));

	if (Context.SkeletalMesh == nullptr || Context.Skeleton == nullptr)
	{
		AppendUniqueMessage(OutReport.Errors, TEXT("AnalyzeVMDAnimationImport requires a valid SkeletalMesh and USkeleton."));
		return false;
	}

	const FReferenceSkeleton& RefSkeleton = Context.Skeleton->GetReferenceSkeleton();
	if (RefSkeleton.GetNum() <= 0)
	{
		AppendUniqueMessage(OutReport.Errors, TEXT("Target skeleton has no bones."));
		return false;
	}

	for (const VMDBoneKeyframe& Keyframe : VmdData.BoneKeyframes)
	{
		OutReport.MaxFrame = FMath::Max(OutReport.MaxFrame, static_cast<int32>(Keyframe.FrameNumber));
	}
	for (const VMDMorphKeyframe& Keyframe : VmdData.MorphKeyframes)
	{
		OutReport.MaxFrame = FMath::Max(OutReport.MaxFrame, static_cast<int32>(Keyframe.FrameNumber));
	}

	if (Settings.bImportBoneTracks)
	{
		const TArray<TPair<FString, int32>> BoneCounts = BuildTrackCounts(VmdData.BoneKeyframes, [](const VMDBoneKeyframe& Keyframe)
		{
			return Keyframe.BoneName;
		});

		OutReport.SourceBoneTrackCount = BoneCounts.Num();
		for (const TPair<FString, int32>& Pair : BoneCounts)
		{
			FMMDResolvedBoneTrack Track;
			Track.SourceBoneName = Pair.Key;
			Track.KeyCount = Pair.Value;

			const FName ExactName(*Pair.Key);
			Track.TargetBoneIndex = RefSkeleton.FindBoneIndex(ExactName);
			if (Track.TargetBoneIndex != INDEX_NONE)
			{
				Track.TargetBoneName = ExactName;
				Track.bMatched = true;
				++OutReport.MatchedBoneTrackCount;
			}
			else
			{
				const FString SanitizedSource = FixMMDName(Pair.Key);
				for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
				{
					const FName CandidateName = RefSkeleton.GetBoneName(BoneIndex);
					if (FixMMDName(CandidateName.ToString()) == SanitizedSource)
					{
						Track.TargetBoneIndex = BoneIndex;
						Track.TargetBoneName = CandidateName;
						Track.bMatched = true;
						++OutReport.MatchedBoneTrackCount;
						break;
					}
				}
			}

			if (!Track.bMatched)
			{
				AppendUniqueMessage(OutReport.Warnings, FString::Printf(TEXT("Unmatched VMD bone track: %s"), *Track.SourceBoneName));
			}

			OutReport.BoneTracks.Add(MoveTemp(Track));
		}
	}

	if (Settings.bImportMorphCurves)
	{
		const TArray<TPair<FString, int32>> MorphCounts = BuildTrackCounts(VmdData.MorphKeyframes, [](const VMDMorphKeyframe& Keyframe)
		{
			return Keyframe.MorphName;
		});

		OutReport.SourceMorphTrackCount = MorphCounts.Num();
		for (const TPair<FString, int32>& Pair : MorphCounts)
		{
			FMMDResolvedMorphTrack Track;
			Track.SourceMorphName = Pair.Key;
			Track.KeyCount = Pair.Value;
			Track.bMatched = TryResolveMorphTargetName(Context.SkeletalMesh, Pair.Key, Track.TargetMorphName);
			if (Track.bMatched)
			{
				++OutReport.MatchedMorphTrackCount;
			}
			else
			{
				AppendUniqueMessage(OutReport.Warnings, FString::Printf(TEXT("Unmatched VMD morph track: %s"), *Track.SourceMorphName));
			}
			OutReport.MorphTracks.Add(MoveTemp(Track));
		}
	}

	if (Settings.bImportMorphCurves && Context.SkeletalMesh->GetMorphTargets().Num() == 0)
	{
		AppendUniqueMessage(OutReport.Warnings, TEXT("Target SkeletalMesh has no MorphTargets. Facial VMD curves cannot be played until PMX morph import is implemented."));
	}

	return !OutReport.HasErrors();
}

UAnimSequence* TMMDMeshBuilder::BuildVMDAnimation(const VMDData& VmdData, const FMMDAnimationImportContext& Context, const FMMDAnimationImportSettings& Settings, FMMDAnimationImportReport* OutReport)
{
	FMMDAnimationImportReport LocalReport;
	if (!AnalyzeVMDAnimationImport(VmdData, Context, Settings, LocalReport))
	{
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

#if WITH_EDITOR
	if (!Settings.bImportBoneTracks)
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("Current BuildVMDAnimation implementation requires bone track import to be enabled."));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	if (LocalReport.MatchedBoneTrackCount <= 0)
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("No VMD bone tracks matched the target skeleton."));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	FString UniquePackageName;
	FString UniqueAssetName;
	{
		FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
		AssetToolsModule.Get().CreateUniqueAssetName(LocalReport.PackagePath / LocalReport.AssetName, TEXT(""), UniquePackageName, UniqueAssetName);
	}

	UPackage* Package = CreatePackage(*UniquePackageName);
	if (Package == nullptr)
	{
		AppendUniqueMessage(LocalReport.Errors, FString::Printf(TEXT("Failed to create animation package: %s"), *UniquePackageName));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	TMap<FName, TArray<FResolvedVMDBoneKey>> BoneTrackMap;
	if (!BuildResolvedBoneKeyMap(VmdData, LocalReport, Settings, BoneTrackMap))
	{
		AppendUniqueMessage(LocalReport.Errors, TEXT("Failed to build resolved VMD bone key map."));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(Package, *UniqueAssetName, RF_Public | RF_Standalone);
	if (AnimSequence == nullptr)
	{
		AppendUniqueMessage(LocalReport.Errors, FString::Printf(TEXT("Failed to create UAnimSequence: %s"), *UniqueAssetName));
		if (OutReport != nullptr)
		{
			*OutReport = LocalReport;
		}
		return nullptr;
	}

	AnimSequence->SetSkeleton(Context.Skeleton);
	AnimSequence->SetPreviewMesh(Context.SkeletalMesh);

	const FReferenceSkeleton& RefSkeleton = Context.Skeleton->GetReferenceSkeleton();
	const TArray<FTransform>& RefPose = RefSkeleton.GetRefBonePose();
	const int32 NumFrames = FMath::Max(LocalReport.MaxFrame + 1, 1);

	IAnimationDataController& Controller = AnimSequence->GetController();
	Controller.OpenBracket(FText::FromString(TEXT("Import VMD Bone Animation")));
	Controller.InitializeModel();
	Controller.SetFrameRate(FFrameRate(FMath::RoundToInt32(Settings.FrameRate), 1), true);
	Controller.SetNumberOfFrames(NumFrames, true);

	for (const TPair<FName, TArray<FResolvedVMDBoneKey>>& Pair : BoneTrackMap)
	{
		const FName BoneName = Pair.Key;
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			continue;
		}

		const FTransform DefaultTransform = RefPose.IsValidIndex(BoneIndex) ? RefPose[BoneIndex] : FTransform::Identity;
		FVector CurrentPos = DefaultTransform.GetTranslation();
		FQuat CurrentRot = DefaultTransform.GetRotation();
		const FVector CurrentScale = DefaultTransform.GetScale3D();

		TArray<FVector3f> PosKeys;
		TArray<FQuat4f> RotKeys;
		TArray<FVector3f> ScaleKeys;
		PosKeys.SetNum(NumFrames);
		RotKeys.SetNum(NumFrames);
		ScaleKeys.SetNum(NumFrames);

		const TArray<FResolvedVMDBoneKey>& Keys = Pair.Value;
		int32 KeyIndex = 0;
		for (int32 FrameIndex = 0; FrameIndex < NumFrames; ++FrameIndex)
		{
			while (KeyIndex < Keys.Num() && Keys[KeyIndex].Frame == FrameIndex)
			{
				CurrentPos = DefaultTransform.GetTranslation() + Keys[KeyIndex].Position;
				CurrentRot = (DefaultTransform.GetRotation() * Keys[KeyIndex].Rotation).GetNormalized();
				++KeyIndex;
			}

			PosKeys[FrameIndex] = FVector3f(CurrentPos);
			RotKeys[FrameIndex] = FQuat4f(CurrentRot);
			ScaleKeys[FrameIndex] = FVector3f(CurrentScale);
		}

		Controller.AddBoneTrack(BoneName);
		Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
	}

	Controller.CloseBracket();
	AnimSequence->PostEditChange();
	AnimSequence->MarkPackageDirty();
	FAssetRegistryModule::AssetCreated(AnimSequence);

	const FString FilePath = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs SaveArgs;
	SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
	SaveArgs.Error = GError;
	SaveArgs.SaveFlags = SAVE_None;
	SaveArgs.bWarnOfLongFilename = false;
	if (!UPackage::SavePackage(Package, AnimSequence, *FilePath, SaveArgs))
	{
		AppendUniqueMessage(LocalReport.Warnings, FString::Printf(TEXT("Failed to save generated animation package: %s"), *FilePath));
	}

	LocalReport.PackagePath = UniquePackageName;
	LocalReport.AssetName = UniqueAssetName;
	if (Settings.bImportMorphCurves && LocalReport.MatchedMorphTrackCount > 0)
	{
		AppendUniqueMessage(LocalReport.Warnings, TEXT("Morph curves were analyzed but are not written yet. Bone animation is imported; facial animation is still pending PMX MorphTarget + curve import."));
	}

	if (OutReport != nullptr)
	{
		*OutReport = LocalReport;
	}
	return AnimSequence;
#else
	AppendUniqueMessage(LocalReport.Errors, TEXT("BuildVMDAnimation is editor-only."));
	if (OutReport != nullptr)
	{
		*OutReport = LocalReport;
	}
	return nullptr;
#endif
}

UAnimSequence* TMMDMeshBuilder::BuildVMDAnimation(const VMDData& VmdData, const FString& VMDFilePath)
{
	FMMDAnimationImportContext Context;
	Context.SourceVMDFilePath = VMDFilePath;

	FMMDAnimationImportSettings Settings;
	FMMDAnimationImportReport Report;
	AppendUniqueMessage(Report.Errors, TEXT("Legacy BuildVMDAnimation overload no longer has enough context. Pass a SkeletalMesh via FMMDAnimationImportContext."));
	(void)VmdData;
	(void)Settings;
	return nullptr;
}
#endif

