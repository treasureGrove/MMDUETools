#include "Rendering/MMDOutlineLibrary.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Import/MMDModelDataAsset.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/MMDOutlineState.h"
#include "Stage/MMDStageLibrary.h"
#include "Stage/MMDStageRenderPreset.h"

bool UMMDOutlineLibrary::ApplyMMDOutline(USkeletalMeshComponent* Component, bool bEnabled)
{
	if (!IsInGameThread() || !IsValid(Component))
	{
		return false;
	}

	USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
	UMMDOutlineState* PresetState = Cast<UMMDOutlineState>(Component->GetAssetUserDataOfClass(UMMDOutlineState::StaticClass()));
	if (PresetState && PresetState->SourceMesh != Mesh)
	{
		RestoreMMDOutline(Component);
		PresetState = nullptr;
	}
	else if (bEnabled && PresetState && PresetState->Preset)
	{
		// 预览面板等旧开关沿用当前预设，避免重新开启后退回 PMX 红/紫边线。
		return ApplyMMDOutlinePreset(Component, PresetState->Preset);
	}
	const UMMDSkeletalMeshUserData* UserData = Mesh ? Mesh->GetAssetUserData<UMMDSkeletalMeshUserData>() : nullptr;
	if (!UserData || UserData->OutlineMaterials.IsEmpty())
	{
		return false;
	}

	// 全局 Overlay 会填补空槽，因此必须先清空，才能保留 PMX 未启用描边的材质段。
	Component->SetOverlayMaterial(nullptr);
	const int32 SlotCount = Component->GetNumMaterials();
	bool bHasOutline = false;
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		UMaterialInterface* Material = bEnabled && UserData->OutlineMaterials.IsValidIndex(SlotIndex)
			? UserData->OutlineMaterials[SlotIndex].Get() : nullptr;
		Component->SetOverlayMaterial(Material, true, SlotIndex);
		if (!bEnabled && PresetState && PresetState->AppliedSlotOverlays.IsValidIndex(SlotIndex))
			PresetState->AppliedSlotOverlays[SlotIndex] = nullptr;
		bHasOutline |= Material != nullptr;
	}
	if (bHasOutline)
	{
		// WPO 为世界空间位移；按变换后最窄的非零包围盒轴覆盖 10cm，重复调用不会累积。
		const FVector WorldExtent = Mesh->GetBounds().TransformBy(Component->GetComponentTransform()).BoxExtent.GetAbs();
		double SmallestExtent = TNumericLimits<double>::Max();
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (WorldExtent[Axis] > UE_SMALL_NUMBER)
			{
				SmallestExtent = FMath::Min(SmallestExtent, WorldExtent[Axis]);
			}
		}
		if (SmallestExtent == TNumericLimits<double>::Max())
		{
			SmallestExtent = 1.0;
		}
		const float RequiredBoundsScale = static_cast<float>(1.0 + 10.0 / SmallestExtent);
		if (Component->BoundsScale < RequiredBoundsScale)
		{
			// 基类 SetBoundsScale 不清除骨骼网格缓存，否则首帧仍用扩张前的实际边界。
			Component->InvalidateCachedBounds();
			Component->SetBoundsScale(RequiredBoundsScale);
		}
	}
	return true;
}

bool UMMDOutlineLibrary::ApplyMMDOutlinePreset(USkeletalMeshComponent* Component, const UMMDStageRenderPreset* Preset)
{
	if (!IsInGameThread() || !IsValid(Component) || !IsValid(Preset)) return false;
	USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
	UMMDOutlineState* State = Cast<UMMDOutlineState>(Component->GetAssetUserDataOfClass(UMMDOutlineState::StaticClass()));
	if (State && State->SourceMesh != Mesh)
	{
		RestoreMMDOutline(Component);
		State = nullptr;
	}
	const UMMDSkeletalMeshUserData* UserData = Mesh ? Mesh->GetAssetUserData<UMMDSkeletalMeshUserData>() : nullptr;
	if (!UserData || UserData->OutlineMaterials.IsEmpty()) return false;
	if (!State)
	{
		State = NewObject<UMMDOutlineState>(Component, NAME_None, RF_Transient);
		State->SourceMesh = Mesh;
		State->SourceGlobalOverlay = Component->OverlayMaterial;
		State->SourceSlotOverlays = Component->MaterialSlotsOverlayMaterial;
		State->SourceBoundsScale = Component->BoundsScale;
		Component->AddAssetUserData(State);
	}
	State->Preset = const_cast<UMMDStageRenderPreset*>(Preset);
	const bool bEnabled = Preset->bCharacterOutline && FMath::IsFinite(Preset->OutlinePixelWidth) && Preset->OutlinePixelWidth > 0.0f;
	// 保留空槽；不通过 global Overlay 为未开启边线的部位补出描边。
	Component->SetOverlayMaterial(nullptr);
	const int32 SlotCount = Component->GetNumMaterials();
	State->AppliedSlotOverlays.SetNum(SlotCount);
	for (int32 SlotIndex = 0; SlotIndex < SlotCount; ++SlotIndex)
	{
		UMaterialInterface* Source = UserData->OutlineMaterials.IsValidIndex(SlotIndex)
			? UserData->OutlineMaterials[SlotIndex].Get() : nullptr;
		UMaterialInstanceDynamic* Outline = Cast<UMaterialInstanceDynamic>(State->AppliedSlotOverlays[SlotIndex]);
		if (bEnabled && Source)
		{
			if (!Outline || Outline->Parent != Source) Outline = UMaterialInstanceDynamic::Create(Source, Component);
			if (Outline)
			{
				const EMMDMaterialRole Role = UMMDStageLibrary::GetResolvedCharacterMaterialRole(Component, SlotIndex);
				FLinearColor Palette = Preset->OutlineColor;
				float WidthScale = 0.85f;
				switch (Role)
				{
				case EMMDMaterialRole::Skin: Palette = Preset->SkinOutlineColor; WidthScale = 0.65f; break;
				case EMMDMaterialRole::Hair: Palette = Preset->HairOutlineColor; WidthScale = 0.90f; break;
				case EMMDMaterialRole::Cloth: Palette = Preset->ClothOutlineColor; WidthScale = 1.0f; break;
				case EMMDMaterialRole::Eye: WidthScale = 0.45f; break;
				case EMMDMaterialRole::Metal: WidthScale = 0.80f; break;
				default: break;
				}
				FLinearColor Imported = FLinearColor::Black;
				Source->GetVectorParameterValue(FMaterialParameterInfo(TEXT("EdgeColor")), Imported);
				const float Blend = FMath::IsFinite(Preset->OutlinePaletteBlend) ? FMath::Clamp(Preset->OutlinePaletteBlend, 0.0f, 1.0f) : 1.0f;
				FLinearColor Color = FMath::Lerp(Imported, Palette, Blend);
				for (int32 Channel = 0; Channel < 3; ++Channel)
					Color.Component(Channel) = FMath::IsFinite(Color.Component(Channel)) ? FMath::Clamp(Color.Component(Channel), 0.0f, 1.0f) : 0.0f;
				Color.A = FMath::IsFinite(Imported.A) ? FMath::Clamp(Imported.A, 0.0f, 1.0f) : 0.0f;
				Outline->SetVectorParameterValue(TEXT("EdgeColor"), Color);
				Outline->SetScalarParameterValue(TEXT("OutlineScreenSpace"), 1.0f);
				Outline->SetScalarParameterValue(TEXT("OutlinePixelWidth"), FMath::Clamp(Preset->OutlinePixelWidth, 0.0f, 4.0f) * WidthScale);
				Outline->SetScalarParameterValue(TEXT("OutlineReferenceHeight"), 1080.0f);
			}
		}
		else Outline = nullptr;
		State->AppliedSlotOverlays[SlotIndex] = Outline;
		Component->SetOverlayMaterial(Outline, true, SlotIndex);
	}
	// 原有路径负责 WPO 上限的边界扩张；这里只沿用其幂等算法，不覆盖新 MID。
	if (bEnabled && State->AppliedSlotOverlays.ContainsByPredicate([](const TObjectPtr<UMaterialInterface>& Material) { return Material != nullptr; }))
	{
		const FVector Extent = Mesh->GetBounds().TransformBy(Component->GetComponentTransform()).BoxExtent.GetAbs();
		double SmallestExtent = TNumericLimits<double>::Max();
		for (int32 Axis = 0; Axis < 3; ++Axis) if (Extent[Axis] > UE_SMALL_NUMBER) SmallestExtent = FMath::Min(SmallestExtent, Extent[Axis]);
		if (SmallestExtent == TNumericLimits<double>::Max()) SmallestExtent = 1.0;
		const float RequiredScale = static_cast<float>(1.0 + 10.0 / SmallestExtent);
		if (Component->BoundsScale < RequiredScale)
		{
			Component->InvalidateCachedBounds();
			Component->SetBoundsScale(RequiredScale);
		}
	}
	State->AppliedBoundsScale = Component->BoundsScale;
	return true;
}

bool UMMDOutlineLibrary::RestoreMMDOutline(USkeletalMeshComponent* Component)
{
	if (!IsInGameThread() || !IsValid(Component)) return false;
	UMMDOutlineState* State = Cast<UMMDOutlineState>(Component->GetAssetUserDataOfClass(UMMDOutlineState::StaticClass()));
	if (!State) return false;
	const bool bSameMesh = State->SourceMesh == Component->GetSkeletalMeshAsset();
	bool bAllOwned = true;
	for (int32 Index = 0; Index < Component->MaterialSlotsOverlayMaterial.Num(); ++Index)
	{
		const bool bOwned = State->AppliedSlotOverlays.IsValidIndex(Index)
			&& Component->MaterialSlotsOverlayMaterial[Index] == State->AppliedSlotOverlays[Index];
		bAllOwned &= bOwned;
		if (bOwned) Component->SetOverlayMaterial(bSameMesh && State->SourceSlotOverlays.IsValidIndex(Index)
			? State->SourceSlotOverlays[Index].Get() : nullptr, true, Index);
	}
	if (bAllOwned)
	{
		Component->MaterialSlotsOverlayMaterial = bSameMesh ? State->SourceSlotOverlays : TArray<TObjectPtr<UMaterialInterface>>();
		Component->MarkRenderStateDirty();
	}
	if (!Component->OverlayMaterial && bSameMesh) Component->SetOverlayMaterial(State->SourceGlobalOverlay);
	if (Component->BoundsScale == State->AppliedBoundsScale)
	{
		Component->InvalidateCachedBounds();
		Component->SetBoundsScale(State->SourceBoundsScale);
	}
	Component->RemoveUserDataOfClass(UMMDOutlineState::StaticClass());
	return bSameMesh;
}

void UMMDOutlineLibrary::RefreshMMDOutlinePreset(USkeletalMeshComponent* Component)
{
	if (!IsInGameThread() || !IsValid(Component)) return;
	const UMMDOutlineState* State = Cast<UMMDOutlineState>(Component->GetAssetUserDataOfClass(UMMDOutlineState::StaticClass()));
	if (State && State->Preset) ApplyMMDOutlinePreset(Component, State->Preset);
}
