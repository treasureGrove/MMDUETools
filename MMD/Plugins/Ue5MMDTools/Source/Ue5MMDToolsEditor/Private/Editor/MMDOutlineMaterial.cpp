#include "Editor/MMDOutlineMaterial.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/FileManager.h"
#include "Import/MMDModelDataAsset.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionVertexColor.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Rendering/MMDOutlineLibrary.h"
#include "Rendering/MMDOutlineState.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RenderingThread.h"
#include "Serialization/ArchiveUObject.h"
#include "Dom/JsonObject.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	constexpr const TCHAR* OutlinePackageName = TEXT("/Ue5MMDTools/Resources/MaterialInstance/M_MMD_Outline");
	constexpr const TCHAR* OutlineObjectPath = TEXT("/Ue5MMDTools/Resources/MaterialInstance/M_MMD_Outline.M_MMD_Outline");

	template<typename T>
	T* AddExpression(UMaterial* Material)
	{
		T* Expression = NewObject<T>(Material);
		Expression->Material = Material;
		Material->GetExpressionCollection().Expressions.Add(Expression);
		return Expression;
	}

	UMaterialExpressionScalarParameter* AddScalar(UMaterial* Material, FName Name, float DefaultValue)
	{
		UMaterialExpressionScalarParameter* Scalar = AddExpression<UMaterialExpressionScalarParameter>(Material);
		Scalar->ParameterName = Name;
		Scalar->DefaultValue = DefaultValue;
		return Scalar;
	}

	FString MakeOutlineCode()
	{
		FString Source;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
		FString Signature(TEXT("unavailable"));
		if (Plugin && FFileHelper::LoadFileToString(Source, *FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders/TMMDShader/TMMDOutline.usf"))))
		{
			const FTCHARToUTF8 SourceUtf8(*Source);
			Signature = FMD5::HashBytes(reinterpret_cast<const uint8*>(SourceUtf8.Get()), SourceUtf8.Length());
		}
		return FString::Printf(TEXT("// MMDOutlineSource:%s\n#include \"/Plugin/Ue5MMDTools/TMMDShader/TMMDOutline.usf\"\nreturn 0;"), *Signature);
	}

	UMaterialExpressionCustom* GetOutlineOffset(UMaterial* Material)
	{
		UMaterialEditorOnlyData* Data = Material ? Material->GetEditorOnlyData() : nullptr;
		return Data ? Cast<UMaterialExpressionCustom>(Data->WorldPositionOffset.Expression) : nullptr;
	}

	const TCHAR* OutlineVisibilityCode = TEXT("struct TMMDOutlineVisibility\n{\n"
		"    float Mask(float opacity_mask, float screen_space)\n    {\n"
		"        return screen_space > 0.5 && IsOrthoProjection(ResolvedView) ? 0.0 : opacity_mask;\n    }\n};\n"
		"TMMDOutlineVisibility outline_visibility;\nreturn outline_visibility.Mask(opacity_mask, screen_space);");

	UMaterialExpressionCustom* AddOutlineVisibility(UMaterial* Material, UMaterialExpression* Opacity, UMaterialExpression* ScreenSpace, int32 OpacityOutputIndex = 0)
	{
		UMaterialExpressionCustom* Visibility = AddExpression<UMaterialExpressionCustom>(Material);
		Visibility->OutputType = CMOT_Float1;
		Visibility->Description = TEXT("细描边正交深度隔离");
		Visibility->Code = OutlineVisibilityCode;
		Visibility->Inputs.Reset();
		FCustomInput& MaskInput = Visibility->Inputs.AddDefaulted_GetRef();
		MaskInput.InputName = TEXT("opacity_mask");
		MaskInput.Input.Connect(OpacityOutputIndex, Opacity);
		FCustomInput& ScreenInput = Visibility->Inputs.AddDefaulted_GetRef();
		ScreenInput.InputName = TEXT("screen_space");
		ScreenInput.Input.Connect(0, ScreenSpace);
		return Visibility;
	}

	bool UpgradeOutlineGraph(UMaterial* Material, bool& bChanged)
	{
		bChanged = false;
		if (!Material || Material->MaterialDomain != MD_Surface || Material->GetBlendMode() != BLEND_Masked
			|| !Material->TwoSided || Material->GetShadingModels().GetFirstShadingModel() != MSM_Unlit
			|| !Material->GetUsageByFlag(MATUSAGE_SkeletalMesh)) return false;
		UMaterialExpressionCustom* Offset = GetOutlineOffset(Material);
		if (!Offset || !Offset->Code.Contains(TEXT("TMMDOutline.usf")) || (Offset->Inputs.Num() != 5 && Offset->Inputs.Num() != 8)) return false;
		const FName ExpectedNames[] = { TEXT("vertex_normal"), TEXT("edge_scale_encoded"), TEXT("edge_size"), TEXT("outline_width"), TEXT("max_offset"),
			TEXT("screen_space"), TEXT("pixel_width"), TEXT("reference_height") };
		for (int32 Index = 0; Index < Offset->Inputs.Num(); ++Index)
			if (Offset->Inputs[Index].InputName != ExpectedNames[Index] || !Offset->Inputs[Index].Input.Expression) return false;
		if (Offset->Inputs.Num() == 5)
		{
			Material->Modify();
			const FName ParameterNames[] = { TEXT("OutlineScreenSpace"), TEXT("OutlinePixelWidth"), TEXT("OutlineReferenceHeight") };
			const float Defaults[] = { 0.0f, 1.20f, 1080.0f };
			for (int32 Index = 0; Index < 3; ++Index)
			{
				FCustomInput& Input = Offset->Inputs.AddDefaulted_GetRef();
				Input.InputName = ExpectedNames[Index + 5];
				Input.Input.Connect(0, AddScalar(Material, ParameterNames[Index], Defaults[Index]));
			}
			bChanged = true;
		}
		const FString Code = MakeOutlineCode();
		if (Offset->Code != Code)
		{
			Material->Modify();
			Offset->Code = Code;
			bChanged = true;
		}
		UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
		UMaterialExpressionCustom* Visibility = Cast<UMaterialExpressionCustom>(Data->OpacityMask.Expression);
		if (!Visibility || !Visibility->Code.Contains(TEXT("TMMDOutlineVisibility")))
		{
			if (!Data->OpacityMask.Expression) return false;
			Material->Modify();
			Visibility = AddOutlineVisibility(Material, Data->OpacityMask.Expression, Offset->Inputs[5].Input.Expression, Data->OpacityMask.OutputIndex);
			Data->OpacityMask.Connect(0, Visibility);
			bChanged = true;
		}
		else if (Visibility->Code != OutlineVisibilityCode)
		{
			Material->Modify();
			Visibility->Code = OutlineVisibilityCode;
			bChanged = true;
		}
		return true;
	}
}

UMaterial* CreateMMDOutlineMaterial(UObject* Outer, FName Name, EObjectFlags Flags)
{
	if (!Outer || !IsInGameThread())
	{
		return nullptr;
	}
	UMaterial* Material = NewObject<UMaterial>(Outer, Name, Flags);
	FArchiveUObject DummyArchive;
	Material->Serialize(DummyArchive);
	Material->MaterialDomain = MD_Surface;
	Material->BlendMode = BLEND_Masked;
	Material->SetShadingModel(MSM_Unlit);
	Material->TwoSided = true;
	Material->SetUsageByFlag(MATUSAGE_SkeletalMesh, true);
	Material->OpacityMaskClipValue = 0.001f;
	Material->MaxWorldPositionOffsetDisplacement = 10.0f;

	UMaterialExpressionVectorParameter* EdgeColor = AddExpression<UMaterialExpressionVectorParameter>(Material);
	EdgeColor->ParameterName = TEXT("EdgeColor");
	EdgeColor->DefaultValue = FLinearColor::Black;
	UMaterialExpressionScalarParameter* EdgeSize = AddScalar(Material, TEXT("EdgeSize"), 1.0f);
	UMaterialExpressionScalarParameter* OutlineWidth = AddScalar(Material, TEXT("OutlineWidth"), 0.1f);
	UMaterialExpressionScalarParameter* MaxOffset = AddScalar(Material, TEXT("OutlineMaxOffset"), 10.0f);
	UMaterialExpressionScalarParameter* ScreenSpace = AddScalar(Material, TEXT("OutlineScreenSpace"), 0.0f);
	UMaterialExpressionScalarParameter* PixelWidth = AddScalar(Material, TEXT("OutlinePixelWidth"), 1.20f);
	UMaterialExpressionScalarParameter* ReferenceHeight = AddScalar(Material, TEXT("OutlineReferenceHeight"), 1080.0f);
	UMaterialExpressionVertexColor* VertexColor = AddExpression<UMaterialExpressionVertexColor>(Material);
	UMaterialExpressionVertexNormalWS* VertexNormal = AddExpression<UMaterialExpressionVertexNormalWS>(Material);
	UMaterialExpressionCustom* Offset = AddExpression<UMaterialExpressionCustom>(Material);
	Offset->OutputType = CMOT_Float3;
	Offset->Description = TEXT("PMX geometry outline offset");
	Offset->Code = MakeOutlineCode();
	Offset->Inputs.Reset();
	auto AddInput = [Offset](FName InputName, UMaterialExpression* Expression, int32 OutputIndex = 0)
	{
		FCustomInput& Input = Offset->Inputs.AddDefaulted_GetRef();
		Input.InputName = InputName;
		Input.Input.Connect(OutputIndex, Expression);
	};
	AddInput(TEXT("vertex_normal"), VertexNormal);
	AddInput(TEXT("edge_scale_encoded"), VertexColor, 4);
	AddInput(TEXT("edge_size"), EdgeSize);
	AddInput(TEXT("outline_width"), OutlineWidth);
	AddInput(TEXT("max_offset"), MaxOffset);
	AddInput(TEXT("screen_space"), ScreenSpace);
	AddInput(TEXT("pixel_width"), PixelWidth);
	AddInput(TEXT("reference_height"), ReferenceHeight);

	// TwoSidedSign 正面为 +1，背面为 -1；仅保留外扩后的背面壳。
	UMaterialExpressionTwoSidedSign* FaceSign = AddExpression<UMaterialExpressionTwoSidedSign>(Material);
	UMaterialExpressionOneMinus* BackFace = AddExpression<UMaterialExpressionOneMinus>(Material);
	BackFace->Input.Connect(0, FaceSign);
	UMaterialExpressionMultiply* BackFaceMask = AddExpression<UMaterialExpressionMultiply>(Material);
	BackFaceMask->A.Connect(0, BackFace);
	BackFaceMask->ConstB = 0.5f;
	UMaterialExpressionMultiply* AlphaMask = AddExpression<UMaterialExpressionMultiply>(Material);
	AlphaMask->A.Connect(0, BackFaceMask);
	AlphaMask->B.Connect(4, EdgeColor);
	UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
	if (!Data)
	{
		return nullptr;
	}
	Data->WorldPositionOffset.Connect(0, Offset);
	Data->EmissiveColor.Connect(0, EdgeColor);
	Data->OpacityMask.Connect(0, AddOutlineVisibility(Material, AlphaMask, ScreenSpace));
	return Material;
}

UMaterial* GetOrCreateMMDOutlineMaterial()
{
	if (!IsInGameThread())
	{
		return nullptr;
	}
	const bool bSavedPackageExists = FPackageName::DoesPackageExist(OutlinePackageName);
	UMaterial* Existing = bSavedPackageExists ? LoadObject<UMaterial>(nullptr, OutlineObjectPath)
		: FindObject<UMaterial>(nullptr, OutlineObjectPath);
	if (bSavedPackageExists && !Existing)
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDOutline] Cannot load existing outline parent: %s"), OutlineObjectPath);
		return nullptr;
	}
	bool bChanged = false;
	if (Existing)
	{
		if (!UpgradeOutlineGraph(Existing, bChanged) || !IsMMDOutlineMaterialGraphValid(Existing))
		{
			UE_LOG(LogTemp, Error, TEXT("[MMDOutline] Existing outline parent has an invalid graph: %s"), OutlineObjectPath);
			return nullptr;
		}
		if (bSavedPackageExists && !bChanged)
		{
			return Existing;
		}
	}

	UPackage* Package = CreatePackage(OutlinePackageName);
	UMaterial* Material = Existing ? Existing : CreateMMDOutlineMaterial(Package, TEXT("M_MMD_Outline"), RF_Public | RF_Standalone);
	if (!Material)
	{
		return nullptr;
	}
	Material->PostEditChange();
	Material->MarkPackageDirty();
	if (!Existing)
	{
		FAssetRegistryModule::AssetCreated(Material);
	}
	const FString Filename = FPackageName::LongPackageNameToFilename(OutlinePackageName, FPackageName::GetAssetPackageExtension());
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	if (!UPackage::SavePackage(Package, Material, *Filename, Args))
	{
		UE_LOG(LogTemp, Error, TEXT("[MMDOutline] Cannot save new outline parent: %s"), *Filename);
		return nullptr;
	}
	return Material;
}

UMaterialInstanceConstant* CreateMMDOutlineMaterialInstance(UPackage* Package, FName Name,
	UMaterialInterface* Parent, const FLinearColor& EdgeColor, float EdgeSize, EObjectFlags Flags)
{
	if (!IsInGameThread() || !Package || !Parent)
	{
		return nullptr;
	}
	UMaterialInstanceConstant* Instance = FindObject<UMaterialInstanceConstant>(Package, *Name.ToString());
	const bool bCreatedNew = Instance == nullptr;
	if (!Instance)
	{
		Instance = NewObject<UMaterialInstanceConstant>(Package, Name, Flags);
	}
	Instance->SetFlags(Flags);
	Instance->Modify();
	Instance->SetParentEditorOnly(Parent);
	const float SafeEdgeSize = FMath::IsFinite(EdgeSize) ? FMath::Max(EdgeSize, 0.0f) : 0.0f;
	FLinearColor SafeColor = EdgeColor;
	for (int32 Channel = 0; Channel < 4; ++Channel)
	{
		if (!FMath::IsFinite(SafeColor.Component(Channel)))
		{
			SafeColor.Component(Channel) = 0.0f;
		}
	}
	SafeColor.A = FMath::Clamp(SafeColor.A, 0.0f, 1.0f);
	Instance->SetVectorParameterValueEditorOnly(TEXT("EdgeColor"), SafeColor);
	Instance->SetScalarParameterValueEditorOnly(TEXT("EdgeSize"), SafeEdgeSize);
	Instance->PostEditChange();
	Instance->MarkPackageDirty();
	if (bCreatedNew && !Instance->HasAnyFlags(RF_Transient))
	{
		FAssetRegistryModule::AssetCreated(Instance);
	}
	return Instance;
}

bool IsMMDOutlineMaterialGraphValid(const UMaterial* Material)
{
	if (!Material || Material->MaterialDomain != MD_Surface || Material->GetBlendMode() != BLEND_Masked
		|| !Material->TwoSided || Material->GetShadingModels().GetFirstShadingModel() != MSM_Unlit
		|| !Material->GetUsageByFlag(MATUSAGE_SkeletalMesh))
	{
		return false;
	}
	const UMaterialEditorOnlyData* Data = Material->GetEditorOnlyData();
	const UMaterialExpressionCustom* Offset = Data ? Cast<UMaterialExpressionCustom>(Data->WorldPositionOffset.Expression) : nullptr;
	const UMaterialExpressionCustom* Visibility = Data ? Cast<UMaterialExpressionCustom>(Data->OpacityMask.Expression) : nullptr;
	return Data && Data->EmissiveColor.Expression && Data->OpacityMask.Expression && Offset
		&& Offset->Code.Contains(TEXT("TMMDOutline.usf")) && Offset->Inputs.Num() == 8
		&& Offset->Inputs[5].InputName == TEXT("screen_space") && Offset->Inputs[5].Input.Expression
		&& Offset->Inputs[6].InputName == TEXT("pixel_width") && Offset->Inputs[6].Input.Expression
		&& Offset->Inputs[7].InputName == TEXT("reference_height") && Offset->Inputs[7].Input.Expression
		&& Visibility && Visibility->Code == OutlineVisibilityCode && Visibility->Inputs.Num() == 2
		&& Visibility->Inputs[0].InputName == TEXT("opacity_mask") && Visibility->Inputs[0].Input.Expression
		&& Visibility->Inputs[1].InputName == TEXT("screen_space") && Visibility->Inputs[1].Input.Expression;
}

FBoxSphereBounds MakeMMDOutlineImportBounds(const USkeletalMesh* Mesh, const FBoxSphereBounds& GeometryBounds)
{
	const UMMDSkeletalMeshUserData* UserData = nullptr;
	const TArray<UAssetUserData*>* AssetUserData = Mesh ? Mesh->GetAssetUserDataArray() : nullptr;
	if (AssetUserData)
	{
		for (UAssetUserData* Entry : *AssetUserData)
		{
			if ((UserData = Cast<UMMDSkeletalMeshUserData>(Entry)) != nullptr)
			{
				break;
			}
		}
	}
	if (!UserData || !UserData->OutlineMaterials.ContainsByPredicate([](const TObjectPtr<UMaterialInterface>& Material)
		{ return Material != nullptr; }))
	{
		return GeometryBounds;
	}
	// 包含严格零厚度轴；Runtime 再按世界缩放补足 10cm 上限。
	const FVector ExpandedExtent = GeometryBounds.BoxExtent + FVector(10.0);
	return FBoxSphereBounds(GeometryBounds.Origin, ExpandedExtent, ExpandedExtent.Size());
}

TSharedRef<FJsonObject> ValidateMMDOutlineSlots()
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), false);
	Result->SetNumberField(TEXT("passed"), 0);
	Result->SetNumberField(TEXT("failed"), 1);
	if (!IsInGameThread())
	{
		Result->SetStringField(TEXT("error"), TEXT("Outline slot validation requires the game thread"));
		return Result;
	}

	TStrongObjectPtr<UMaterial> Parent(CreateMMDOutlineMaterial(GetTransientPackage()));
	TStrongObjectPtr<USkeletalMesh> Mesh(NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<USkeletalMeshComponent> Component(NewObject<USkeletalMeshComponent>(GetTransientPackage(), NAME_None, RF_Transient));
	Mesh->GetMaterials().SetNum(2);
	Mesh->SetImportedBounds(FBoxSphereBounds(FVector::ZeroVector, FVector(1.0, 2.0, 3.0), FMath::Sqrt(14.0)));
	UMMDSkeletalMeshUserData* UserData = NewObject<UMMDSkeletalMeshUserData>(Mesh.Get());
	UserData->OutlineMaterials = { Parent.Get(), nullptr };
	Mesh->AddAssetUserData(UserData);
	Component->SetSkeletalMeshAsset(Mesh.Get());
	Component->SetWorldScale3D(FVector(2.0, 3.0, 4.0));
	Component->SetOverlayMaterial(Parent.Get());
	Component->SetOverlayMaterial(Parent.Get(), true, 1);
	const bool bApplied = UMMDOutlineLibrary::ApplyMMDOutline(Component.Get());
	const bool bSlotMapping = Component->GetNumMaterials() == 2 && !Component->GetOverlayMaterial()
		&& Component->GetOverlayMaterial(true, 0) == Parent.Get() && !Component->GetOverlayMaterial(true, 1);
	const float FirstBoundsScale = Component->BoundsScale;
	const FVector WorldExtent = Mesh->GetBounds().TransformBy(Component->GetComponentTransform()).BoxExtent;
	const bool bBoundsCoverOffset = WorldExtent.X * (FirstBoundsScale - 1.0f) >= 10.0
		&& WorldExtent.Y * (FirstBoundsScale - 1.0f) >= 10.0 && WorldExtent.Z * (FirstBoundsScale - 1.0f) >= 10.0;
	const bool bRepeat = UMMDOutlineLibrary::ApplyMMDOutline(Component.Get())
		&& Component->GetOverlayMaterial(true, 0) == Parent.Get() && !Component->GetOverlayMaterial(true, 1)
		&& Component->BoundsScale == FirstBoundsScale;
	const bool bDisabled = UMMDOutlineLibrary::ApplyMMDOutline(Component.Get(), false)
		&& !Component->GetOverlayMaterial(true, 0) && !Component->GetOverlayMaterial(true, 1);
	UserData->OutlineMaterials = { nullptr, Parent.Get() };
	const bool bReapplied = UMMDOutlineLibrary::ApplyMMDOutline(Component.Get())
		&& !Component->GetOverlayMaterial(true, 0) && Component->GetOverlayMaterial(true, 1) == Parent.Get();

	const FBoxSphereBounds PlanarGeometryBounds(FVector(3.0, 4.0, 5.0), FVector(5.0, 0.0, 2.0), FMath::Sqrt(29.0));
	const FBoxSphereBounds PaddedPlanarBounds = MakeMMDOutlineImportBounds(Mesh.Get(), PlanarGeometryBounds);
	Mesh->SetImportedBounds(PaddedPlanarBounds);
	Component->InvalidateCachedBounds();
	Component->SetWorldScale3D(FVector(0.2, 0.05, 0.3));
	const bool bPlanarApplied = UMMDOutlineLibrary::ApplyMMDOutline(Component.Get());
	const FBoxSphereBounds RuntimePlanarBounds = Component->CalcBounds(Component->GetComponentTransform());
	const FVector PlanarWorldExtent = PlanarGeometryBounds.TransformBy(Component->GetComponentTransform()).BoxExtent;
	const FVector CoveredOffset = RuntimePlanarBounds.BoxExtent - PlanarWorldExtent;
	const bool bZeroThicknessCovered = bPlanarApplied && PaddedPlanarBounds.BoxExtent.Y == 10.0
		&& CoveredOffset.X >= 10.0 && CoveredOffset.Y >= 10.0 && CoveredOffset.Z >= 10.0;
	const FBoxSphereBounds ReimportBounds = MakeMMDOutlineImportBounds(Mesh.Get(), PlanarGeometryBounds);
	const bool bReimportPaddingStable = ReimportBounds.BoxExtent == PaddedPlanarBounds.BoxExtent
		&& ReimportBounds.Origin == PaddedPlanarBounds.Origin && ReimportBounds.SphereRadius == PaddedPlanarBounds.SphereRadius;
	UserData->OutlineMaterials = { nullptr, nullptr };
	const FBoxSphereBounds NoOutlineBounds = MakeMMDOutlineImportBounds(Mesh.Get(), PlanarGeometryBounds);
	const bool bNoOutlineUnchanged = NoOutlineBounds.BoxExtent == PlanarGeometryBounds.BoxExtent
		&& NoOutlineBounds.Origin == PlanarGeometryBounds.Origin && NoOutlineBounds.SphereRadius == PlanarGeometryBounds.SphereRadius;

	TStrongObjectPtr<UMaterial> LegacyParent(CreateMMDOutlineMaterial(GetTransientPackage()));
	UMaterialExpressionCustom* LegacyOffset = GetOutlineOffset(LegacyParent.Get());
	UMaterialEditorOnlyData* LegacyData = LegacyParent->GetEditorOnlyData();
	UMaterialExpressionCustom* LegacyVisibility = Cast<UMaterialExpressionCustom>(LegacyData->OpacityMask.Expression);
	LegacyData->OpacityMask.Connect(0, LegacyVisibility->Inputs[0].Input.Expression);
	LegacyOffset->Inputs.SetNum(5);
	LegacyOffset->Code = TEXT("#include \"/Plugin/Ue5MMDTools/TMMDShader/TMMDOutline.usf\"\nreturn 0;");
	bool bMigrationChanged = false;
	const bool bMigrated = UpgradeOutlineGraph(LegacyParent.Get(), bMigrationChanged) && bMigrationChanged
		&& IsMMDOutlineMaterialGraphValid(LegacyParent.Get());
	bool bRepeatMigrationChanged = true;
	const bool bMigrationIdempotent = UpgradeOutlineGraph(LegacyParent.Get(), bRepeatMigrationChanged) && !bRepeatMigrationChanged;

	TStrongObjectPtr<UMMDStageRenderPreset> Preset(NewObject<UMMDStageRenderPreset>(GetTransientPackage(), NAME_None, RF_Transient));
	TStrongObjectPtr<UMaterialInstanceConstant> ImportedOutline(CreateMMDOutlineMaterialInstance(GetTransientPackage(), MakeUniqueObjectName(
		GetTransientPackage(), UMaterialInstanceConstant::StaticClass(), TEXT("OutlineImportedValidation")), Parent.Get(), FLinearColor(0.8f, 0.03f, 0.2f, 0.35f), 2.0f, RF_Transient));
	UserData->OutlineMaterials = { ImportedOutline.Get(), nullptr };
	Mesh->GetMaterials()[0].MaterialSlotName = TEXT("skin");
	Component->SetOverlayMaterial(Parent.Get());
	Component->SetOverlayMaterial(nullptr, true, 0);
	Component->SetOverlayMaterial(Parent.Get(), true, 1);
	Component->SetBoundsScale(2.3f);
	const TArray<TObjectPtr<UMaterialInterface>> InitialOverlays = Component->MaterialSlotsOverlayMaterial;
	const float InitialBoundsScale = Component->BoundsScale;
	const bool bPresetApplied = UMMDOutlineLibrary::ApplyMMDOutlinePreset(Component.Get(), Preset.Get());
	UMaterialInstanceDynamic* PresetOutline = Cast<UMaterialInstanceDynamic>(Component->GetOverlayMaterial(true, 0));
	FLinearColor PresetColor;
	float PresetWidth = 0.0f, PresetEdgeSize = 0.0f, PresetScreenSpace = 0.0f;
	const bool bPresetParameters = PresetOutline
		&& PresetOutline->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EdgeColor")), PresetColor)
		&& PresetOutline->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlinePixelWidth")), PresetWidth)
		&& PresetOutline->GetScalarParameterValue(FMaterialParameterInfo(TEXT("EdgeSize")), PresetEdgeSize)
		&& PresetOutline->GetScalarParameterValue(FMaterialParameterInfo(TEXT("OutlineScreenSpace")), PresetScreenSpace)
		&& PresetColor.Equals(FLinearColor(Preset->SkinOutlineColor.R, Preset->SkinOutlineColor.G, Preset->SkinOutlineColor.B, 0.35f))
		&& FMath::IsNearlyEqual(PresetWidth, Preset->OutlinePixelWidth * 0.65f) && PresetEdgeSize == 2.0f && PresetScreenSpace == 1.0f;
	const bool bPresetSlots = !Component->GetOverlayMaterial() && !Component->GetOverlayMaterial(true, 1);
	const bool bPresetRepeat = UMMDOutlineLibrary::ApplyMMDOutlinePreset(Component.Get(), Preset.Get())
		&& Component->GetOverlayMaterial(true, 0) == PresetOutline;
	const bool bLegacySwitchOff = UMMDOutlineLibrary::ApplyMMDOutline(Component.Get(), false)
		&& !Component->GetOverlayMaterial(true, 0) && !Component->GetOverlayMaterial(true, 1);
	const bool bLegacySwitchOn = UMMDOutlineLibrary::ApplyMMDOutline(Component.Get(), true)
		&& Cast<UMaterialInstanceDynamic>(Component->GetOverlayMaterial(true, 0)) && !Component->GetOverlayMaterial(true, 1);
	Preset->bCharacterOutline = false;
	const bool bPresetDisabled = UMMDOutlineLibrary::ApplyMMDOutlinePreset(Component.Get(), Preset.Get())
		&& !Component->GetOverlayMaterial(true, 0) && !Component->GetOverlayMaterial(true, 1);
	const bool bPresetRestored = UMMDOutlineLibrary::RestoreMMDOutline(Component.Get())
		&& Component->GetOverlayMaterial() == Parent.Get() && Component->MaterialSlotsOverlayMaterial == InitialOverlays
		&& Component->BoundsScale == InitialBoundsScale && !Component->GetAssetUserDataOfClass(UMMDOutlineState::StaticClass());
	Preset->bCharacterOutline = true;
	const bool bArtistApply = UMMDOutlineLibrary::ApplyMMDOutlinePreset(Component.Get(), Preset.Get());
	Component->SetOverlayMaterial(ImportedOutline.Get());
	Component->SetOverlayMaterial(Parent.Get(), true, 0);
	const bool bArtistPreserved = UMMDOutlineLibrary::RestoreMMDOutline(Component.Get()) && bArtistApply
		&& Component->GetOverlayMaterial() == ImportedOutline.Get() && Component->GetOverlayMaterial(true, 0) == Parent.Get()
		&& Component->GetOverlayMaterial(true, 1) == Parent.Get();
	const bool bPassed = Parent.IsValid() && IsMMDOutlineMaterialGraphValid(Parent.Get()) && bApplied
		&& bSlotMapping && bBoundsCoverOffset && bRepeat && bDisabled && bReapplied && bZeroThicknessCovered
		&& bReimportPaddingStable && bNoOutlineUnchanged && bMigrated && bMigrationIdempotent && bPresetApplied
		&& bPresetParameters && bPresetSlots && bPresetRepeat && bLegacySwitchOff && bLegacySwitchOn
		&& bPresetDisabled && bPresetRestored && bArtistPreserved;
	FlushRenderingCommands();
	Result->SetNumberField(TEXT("slot_count"), 2);
	Result->SetBoolField(TEXT("slot_mapping"), bSlotMapping);
	Result->SetBoolField(TEXT("nonuniform_bounds_cover_offset"), bBoundsCoverOffset);
	Result->SetBoolField(TEXT("repeat_idempotent"), bRepeat);
	Result->SetBoolField(TEXT("disable_clears_slots"), bDisabled);
	Result->SetBoolField(TEXT("reapply_updates_slots"), bReapplied);
	Result->SetBoolField(TEXT("zero_thickness_bounds_cover_offset"), bZeroThicknessCovered);
	Result->SetBoolField(TEXT("reimport_padding_stable"), bReimportPaddingStable);
	Result->SetBoolField(TEXT("non_outline_bounds_unchanged"), bNoOutlineUnchanged);
	Result->SetBoolField(TEXT("legacy_five_input_migrated"), bMigrated);
	Result->SetBoolField(TEXT("migration_idempotent"), bMigrationIdempotent);
	Result->SetBoolField(TEXT("preset_role_color_width_pmx_alpha_size"), bPresetParameters);
	Result->SetBoolField(TEXT("preset_disabled_slots_preserved"), bPresetSlots);
	Result->SetBoolField(TEXT("preset_repeat_reuses_mid"), bPresetRepeat);
	Result->SetBoolField(TEXT("legacy_switch_preserves_preset"), bLegacySwitchOff && bLegacySwitchOn);
	Result->SetBoolField(TEXT("preset_switch_off"), bPresetDisabled);
	Result->SetBoolField(TEXT("preset_restores_overlay_and_bounds"), bPresetRestored);
	Result->SetBoolField(TEXT("restore_preserves_artist_overlay_edits"), bArtistPreserved);
	Result->SetBoolField(TEXT("success"), bPassed);
	Result->SetNumberField(TEXT("passed"), bPassed ? 1 : 0);
	Result->SetNumberField(TEXT("failed"), bPassed ? 0 : 1);
	UE_LOG(LogTemp, Display, TEXT("[MMDOutline] %s slot mapping=%d repeat=%d disable=%d reapply=%d"),
		bPassed ? TEXT("PASS") : TEXT("FAIL"), bSlotMapping, bRepeat, bDisabled, bReapplied);
	return Result;
}
