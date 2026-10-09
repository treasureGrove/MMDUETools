#include "Rendering/UMMDHeadBoneSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Import/MMDModelDataAsset.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

void UMMDHeadBoneSubsystem::Deinitialize()
{
	HeadFrames.Reset();
	Super::Deinitialize();
}

void UMMDHeadBoneSubsystem::BuildReferenceFrame(USkeletalMesh* Mesh, FHeadFrameCache& Frame)
{
	Frame = FHeadFrameCache();
	Frame.Mesh = Mesh;
	if (!Mesh) return;
	const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
	for (const TCHAR* Name : { TEXT("頭"), TEXT("Head"), TEXT("head"), TEXT("C_HEAD"), TEXT("head_top"), TEXT("HeadTop") })
	{
		Frame.HeadIndex = Skeleton.FindBoneIndex(FName(Name));
		if (Frame.HeadIndex != INDEX_NONE) break;
	}
	if (Frame.HeadIndex == INDEX_NONE) return;
	Frame.bMMD = Mesh->GetAssetUserData<UMMDSkeletalMeshUserData>() != nullptr
		|| Skeleton.GetBoneName(Frame.HeadIndex) == FName(TEXT("頭"));
	TArray<FTransform> ReferenceTransforms = Skeleton.GetRefBonePose();
	for (int32 Index = 0; Index < ReferenceTransforms.Num(); ++Index)
	{
		const int32 Parent = Skeleton.GetParentIndex(Index);
		if (Parent != INDEX_NONE) ReferenceTransforms[Index] *= ReferenceTransforms[Parent];
	}
	const FTransform& HeadTransform = ReferenceTransforms[Frame.HeadIndex];
	Frame.ReferenceHeadRotation = HeadTransform.GetRotation().GetNormalized();
	Frame.ReferenceRadius = Mesh->GetBounds().BoxExtent.Z * 0.14f;
	int32 LeftEye = INDEX_NONE;
	int32 RightEye = INDEX_NONE;
	for (const TCHAR* Name : { TEXT("左目"), TEXT("左眼"), TEXT("LeftEye"), TEXT("eye_l"), TEXT("Eye_L"), TEXT("eye.L"), TEXT("EyeLeft") })
	{
		LeftEye = Skeleton.FindBoneIndex(FName(Name));
		if (LeftEye != INDEX_NONE) break;
	}
	for (const TCHAR* Name : { TEXT("右目"), TEXT("右眼"), TEXT("RightEye"), TEXT("eye_r"), TEXT("Eye_R"), TEXT("eye.R"), TEXT("EyeRight") })
	{
		RightEye = Skeleton.FindBoneIndex(FName(Name));
		if (RightEye != INDEX_NONE) break;
	}
	Frame.bHasEyes = LeftEye != INDEX_NONE && RightEye != INDEX_NONE;
	if (Frame.bHasEyes)
	{
		const FVector EyeCenter = (ReferenceTransforms[LeftEye].GetLocation() + ReferenceTransforms[RightEye].GetLocation()) * 0.5;
		Frame.EyeOffset = HeadTransform.InverseTransformPosition(EyeCenter);
	}
}

bool UMMDHeadBoneSubsystem::ApplyHeadFrame(USkeletalMeshComponent* Component, FHeadFrameCache& Frame)
{
	USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
	if (Frame.Mesh.Get() != Mesh) BuildReferenceFrame(Mesh, Frame);
	FLinearColor Forward = FLinearColor::Transparent;
	FLinearColor Right = FLinearColor::Transparent;
	FLinearColor PositionRadius = FLinearColor::Transparent;
	bool bValid = Mesh && Frame.HeadIndex != INDEX_NONE
		&& Component->GetComponentSpaceTransforms().IsValidIndex(Frame.HeadIndex);
	if (bValid)
	{
		const FTransform HeadTransform = Component->GetBoneTransform(Frame.HeadIndex);
		const FQuat Rotation = (HeadTransform.GetRotation() * Frame.ReferenceHeadRotation.Inverse()).GetNormalized();
		const FVector F = Rotation.RotateVector(Frame.bMMD ? FVector::YAxisVector : FVector::XAxisVector).GetSafeNormal();
		const FVector U = Rotation.RotateVector(FVector::UpVector).GetSafeNormal();
		const FVector R = FVector::CrossProduct(F, U).GetSafeNormal();
		const FVector Scale = Component->GetComponentTransform().GetScale3D().GetAbs();
		const float Radius = Frame.ReferenceRadius * FMath::Max3(Scale.X, Scale.Y, Scale.Z);
		const FVector Center = Frame.bHasEyes
			? HeadTransform.TransformPosition(Frame.EyeOffset) - U * (Radius * 0.18) - F * (Radius * 0.10)
			: HeadTransform.GetLocation() + U * (Radius * 0.45);
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