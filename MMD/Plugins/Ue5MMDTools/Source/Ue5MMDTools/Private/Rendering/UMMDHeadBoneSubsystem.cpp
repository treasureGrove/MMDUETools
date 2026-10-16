#include "Rendering/UMMDHeadBoneSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/MMDEndfieldFaceData.h"

void UMMDHeadBoneSubsystem::Deinitialize()
{
	HeadFrames.Reset();
	Super::Deinitialize();
}

void UMMDHeadBoneSubsystem::BuildReferenceFrame(USkeletalMesh* Mesh, FHeadFrameCache& Frame)
{
	Frame = FHeadFrameCache();
	Frame.Mesh = Mesh;
	Frame.bInitialized = true;
	const UMMDEndfieldFaceData* Data = Mesh ? Mesh->GetAssetUserData<UMMDEndfieldFaceData>() : nullptr;
	Frame.FaceData = const_cast<UMMDEndfieldFaceData*>(Data);
	if (Data)
	{
		Frame.DataVersion = Data->Version;
		Frame.BindingSignature = Data->ComputeBindingSignature();
		Frame.SkeletonSignature = Data->HeadSkeletonSignature;
		Frame.BoneName = Data->HeadBoneName;
		Frame.CenterHeadLocal = Data->HeadCenterHeadLocal;
		Frame.ReferenceRotation = Data->HeadReferenceRotation;
		Frame.Radius = Data->HeadRadius;
		Frame.FrameVersion = Data->FrameVersion;
	}
	FString Message;
	MMDEndfieldHeadFrame::BuildReferenceFrame(Mesh, NAME_None, Frame.Reference, Message);
	Frame.LiveSkeletonSignature = MMDEndfieldHeadFrame::ComputeSkeletonSignature(Mesh, Frame.Reference.BoneName);
}

bool UMMDHeadBoneSubsystem::ApplyHeadFrame(USkeletalMeshComponent* Component, FHeadFrameCache& Frame)
{
	USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
	const UMMDEndfieldFaceData* Data = Mesh ? Mesh->GetAssetUserData<UMMDEndfieldFaceData>() : nullptr;
	const bool bFrameChanged = Data && (Frame.DataVersion != Data->Version || Frame.BindingSignature != Data->ComputeBindingSignature()
		|| Frame.FrameVersion != Data->FrameVersion
		|| Frame.SkeletonSignature != Data->HeadSkeletonSignature || Frame.BoneName != Data->HeadBoneName
		|| Frame.CenterHeadLocal != Data->HeadCenterHeadLocal || Frame.Radius != Data->HeadRadius
		|| Frame.ReferenceRotation != Data->HeadReferenceRotation);
	bool bSkeletonChanged = Mesh && Frame.Reference.HeadIndex != INDEX_NONE
		&& (Frame.Reference.HeadIndex >= Mesh->GetRefSkeleton().GetNum()
			|| Mesh->GetRefSkeleton().GetBoneName(Frame.Reference.HeadIndex) != Frame.Reference.BoneName);
#if WITH_EDITOR
	// 编辑器可能原位重导同一个 Mesh；不依赖 FaceData 自己更新签名，也不订阅编辑器模块事件。
	if (Mesh && Frame.bInitialized && !bSkeletonChanged)
		bSkeletonChanged = Frame.LiveSkeletonSignature != MMDEndfieldHeadFrame::ComputeSkeletonSignature(Mesh, Frame.Reference.BoneName);
#endif
	const bool bCacheRebuilt = !Frame.bInitialized || Frame.Mesh.Get() != Mesh || Frame.FaceData.IsStale()
		|| Frame.FaceData.Get() != Data || bFrameChanged || bSkeletonChanged;
	if (bCacheRebuilt)
		BuildReferenceFrame(Mesh, Frame);
	FLinearColor Forward = FLinearColor::Transparent;
	FLinearColor Right = FLinearColor::Transparent;
	FLinearColor PositionRadius = FLinearColor::Transparent;
	const FMMDReferenceHeadFrame& Reference = Frame.Reference;
	bool bValid = Mesh && Reference.bValid && Reference.HeadIndex != INDEX_NONE
		&& Component->GetComponentSpaceTransforms().IsValidIndex(Reference.HeadIndex);
	if (bValid)
	{
		const FTransform HeadTransform = Component->GetBoneTransform(Reference.HeadIndex);
		const FQuat Rotation = (HeadTransform.GetRotation() * Reference.ReferenceHeadRotation.Inverse()).GetNormalized();
		const FVector F = Rotation.RotateVector(Reference.ForwardRef).GetSafeNormal();
		const FVector U = Rotation.RotateVector(Reference.UpRef).GetSafeNormal();
		const FVector R = FVector::CrossProduct(F, U).GetSafeNormal();
		// 头骨世界变换已包含组件及骨骼缩放；半径取相对参考的最大轴，避免重复乘组件缩放。
		const FVector Scale = HeadTransform.GetScale3D().GetAbs() / Reference.ReferenceHeadScale.GetAbs();
		const float Radius = Reference.Radius * FMath::Max3(Scale.X, Scale.Y, Scale.Z);
		const FVector Center = HeadTransform.TransformPosition(Reference.CenterHeadLocal);
		bValid = !F.ContainsNaN() && !R.ContainsNaN() && !Center.ContainsNaN()
			&& FMath::IsFinite(Radius) && Radius > UE_SMALL_NUMBER && !F.IsNearlyZero() && !R.IsNearlyZero();
		if (bValid)
		{
			Forward = FLinearColor(F.X, F.Y, F.Z, 0.0f);
			Right = FLinearColor(R.X, R.Y, R.Z, 0.0f);
			PositionRadius = FLinearColor(Center.X, Center.Y, Center.Z, Radius);
		}
	}
	bool bWritten = false;
	for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Material = Component->GetMaterial(Slot);
		if (!Material) continue;
		FLinearColor Value;
		const bool bForward = Material->GetVectorParameterValue(FMaterialParameterInfo(ParamHeadForward), Value);
		const bool bRight = Material->GetVectorParameterValue(FMaterialParameterInfo(ParamHeadRight), Value);
		const bool bPosition = Material->GetVectorParameterValue(FMaterialParameterInfo(ParamHeadPositionRadius), Value);
		if (!bForward && !bRight && !bPosition) continue;
		// 先确认参数存在，只为需要头部数据的槽创建 MID，复用已有角色 MID。
		UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material);
		if (!Dynamic) Dynamic = Component->CreateDynamicMaterialInstance(Slot, Material);
		if (!Dynamic) continue;
		if (bCacheRebuilt || Frame.BoundMaterials.FindRef(Slot).Get() != Dynamic)
		{
			Frame.BoundMaterials.Add(Slot, Dynamic);
			float PayloadEnabled = 0.0f, DirectionEnabled = 0.0f, DirectionVersion = 0.0f;
			UTexture* PreviousTexture = nullptr;
			const bool bHasPayload = Dynamic->GetScalarParameterValue(FMaterialParameterInfo(TEXT("ToonNativePayloadEnabled")), PayloadEnabled);
			const bool bHasDirection = Dynamic->GetScalarParameterValue(
				FMaterialParameterInfo(UMMDEndfieldFaceData::DirectionEnabledParameter), DirectionEnabled)
				&& Dynamic->GetTextureParameterValue(FMaterialParameterInfo(UMMDEndfieldFaceData::DirectionTextureParameter), PreviousTexture);
			Dynamic->GetScalarParameterValue(FMaterialParameterInfo(UMMDEndfieldFaceData::DirectionVersionParameter), DirectionVersion);
			// 只在框架失效/重建时同步 V5；保留手动 V4，以及普通无 payload 路径的参数。
			if (bHasPayload && PayloadEnabled > 0.0f && bHasDirection && DirectionVersion != 4.0f)
			{
				// 首帧骨骼未就绪只令头参数为零；可靠参考图仍可绑定，后续姿态就绪即自动生效。
				UTexture2D* Texture = Reference.bValid && Reference.bFromFaceData && Data && Mesh->GetMaterials().IsValidIndex(Slot)
					? Data->FindDirectionTexture(Slot, Mesh->GetMaterials()[Slot].MaterialSlotName) : nullptr;
				if (Texture)
				{
					Dynamic->SetTextureParameterValue(UMMDEndfieldFaceData::DirectionTextureParameter, Texture);
					Dynamic->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionVersionParameter,
						static_cast<float>(UMMDEndfieldFaceData::CurrentVersion));
				}
				Dynamic->SetScalarParameterValue(UMMDEndfieldFaceData::DirectionEnabledParameter, Texture ? 1.0f : 0.0f);
			}
		}
		if (bForward) Dynamic->SetVectorParameterValue(ParamHeadForward, Forward);
		if (bRight) Dynamic->SetVectorParameterValue(ParamHeadRight, Right);
		if (bPosition) Dynamic->SetVectorParameterValue(ParamHeadPositionRadius, PositionRadius);
		bWritten = true;
	}
	return bValid && bWritten;
}

bool UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(USkeletalMeshComponent* Component)
{
	if (!Component) return false;
	if (UWorld* World = Component->GetWorld())
	{
		if (UMMDHeadBoneSubsystem* Subsystem = World->GetSubsystem<UMMDHeadBoneSubsystem>())
		{
			FHeadFrameCache& Frame = Subsystem->HeadFrames.FindOrAdd(Component);
			return ApplyHeadFrame(Component, Frame);
		}
	}
	FHeadFrameCache Frame;
	return ApplyHeadFrame(Component, Frame);
}

void UMMDHeadBoneSubsystem::Tick(float DeltaTime)
{
	if (!GetWorld()) return;
	for (auto It = HeadFrames.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid()) It.RemoveCurrent();
	}
	for (TActorIterator<AActor> Actor(GetWorld()); Actor; ++Actor)
	{
		TInlineComponentArray<USkeletalMeshComponent*> Components(*Actor);
		for (USkeletalMeshComponent* Component : Components) UpdateComponentHeadFrame(Component);
	}
}
