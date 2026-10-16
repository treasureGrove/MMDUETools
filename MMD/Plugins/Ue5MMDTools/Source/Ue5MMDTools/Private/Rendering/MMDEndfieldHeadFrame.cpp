#include "Rendering/MMDEndfieldHeadFrame.h"

#include "Engine/SkeletalMesh.h"
#include "Import/MMDModelDataAsset.h"
#include "Misc/SecureHash.h"
#include "Rendering/MMDEndfieldFaceData.h"

namespace
{
	struct FMMDEndfieldHeadSkeleton
	{
		const FReferenceSkeleton* Skeleton = nullptr;
		TArray<FTransform> ReferenceTransforms;
		int32 Head = INDEX_NONE;
		int32 LeftEye = INDEX_NONE;
		int32 RightEye = INDEX_NONE;
		bool bMMD = false;
	};

	int32 MMDEndfieldFindHeadBone(const FReferenceSkeleton& Skeleton, TConstArrayView<const TCHAR*> Names)
	{
		for (const TCHAR* Name : Names)
		{
			const int32 Index = Skeleton.FindBoneIndex(FName(Name));
			if (Index != INDEX_NONE) return Index;
		}
		return INDEX_NONE;
	}

	bool MMDEndfieldReadHeadSkeleton(const USkeletalMesh* Mesh, FName PreferredHead,
		FMMDEndfieldHeadSkeleton& Out, FString& OutMessage)
	{
		Out = FMMDEndfieldHeadSkeleton();
		if (!IsValid(Mesh))
		{
			OutMessage = TEXT("头部框架缺少有效骨骼网格");
			return false;
		}
		const FReferenceSkeleton& Skeleton = Mesh->GetRefSkeleton();
		const TCHAR* HeadNames[] = { TEXT("頭"), TEXT("Head"), TEXT("head"), TEXT("C_HEAD"), TEXT("head_top"), TEXT("HeadTop") };
		Out.Head = PreferredHead.IsNone() ? MMDEndfieldFindHeadBone(Skeleton, HeadNames) : Skeleton.FindBoneIndex(PreferredHead);
		if (Out.Head == INDEX_NONE)
		{
			OutMessage = PreferredHead.IsNone() ? TEXT("未找到有效头骨，保持通用 Toon")
				: FString::Printf(TEXT("指定头骨不存在：%s"), *PreferredHead.ToString());
			return false;
		}
		Out.ReferenceTransforms = Skeleton.GetRefBonePose();
		if (!Out.ReferenceTransforms.IsValidIndex(Out.Head) || Out.ReferenceTransforms.Num() != Skeleton.GetNum())
		{
			OutMessage = TEXT("头骨缺少参考姿势");
			return false;
		}
		for (int32 Index = 0; Index < Out.ReferenceTransforms.Num(); ++Index)
		{
			const int32 Parent = Skeleton.GetParentIndex(Index);
			if (Out.ReferenceTransforms[Index].ContainsNaN() || (Parent != INDEX_NONE && (Parent < 0 || Parent >= Index)))
			{
				OutMessage = TEXT("头部参考骨骼变换或层级无效");
				return false;
			}
			if (Parent != INDEX_NONE) Out.ReferenceTransforms[Index] *= Out.ReferenceTransforms[Parent];
			if (Out.ReferenceTransforms[Index].ContainsNaN())
			{
				OutMessage = TEXT("头部组件空间参考变换无效");
				return false;
			}
		}
		const TCHAR* LeftNames[] = { TEXT("左目"), TEXT("左眼"), TEXT("LeftEye"), TEXT("eye_l"), TEXT("Eye_L"), TEXT("eye.L"), TEXT("EyeLeft") };
		const TCHAR* RightNames[] = { TEXT("右目"), TEXT("右眼"), TEXT("RightEye"), TEXT("eye_r"), TEXT("Eye_R"), TEXT("eye.R"), TEXT("EyeRight") };
		Out.LeftEye = MMDEndfieldFindHeadBone(Skeleton, LeftNames);
		Out.RightEye = MMDEndfieldFindHeadBone(Skeleton, RightNames);
		Out.Skeleton = &Skeleton;
		Out.bMMD = const_cast<USkeletalMesh*>(Mesh)->GetAssetUserData<UMMDSkeletalMeshUserData>() != nullptr
			|| Skeleton.GetBoneName(Out.Head) == FName(TEXT("頭"));
		return true;
	}

	FString MMDEndfieldHeadSkeletonSignature(const FMMDEndfieldHeadSkeleton& Source)
	{
		FString Text = FString::Printf(TEXT("mmd.head-frame.1|mmd=%d"), Source.bMMD ? 1 : 0);
		const int32 Parent = Source.Skeleton->GetParentIndex(Source.Head);
		for (const int32 Index : { Source.Head, Source.LeftEye, Source.RightEye, Parent })
		{
			if (!Source.ReferenceTransforms.IsValidIndex(Index))
			{
				Text += TEXT("|missing");
				continue;
			}
			const FTransform& Transform = Source.ReferenceTransforms[Index];
			const FVector P = Transform.GetLocation();
			const FQuat Q = Transform.GetRotation().GetNormalized();
			const FVector S = Transform.GetScale3D();
			Text += FString::Printf(TEXT("|%d:"), Index) + Source.Skeleton->GetBoneName(Index).ToString();
			Text.Appendf(TEXT("|%.17g,%.17g,%.17g|%.17g,%.17g,%.17g,%.17g|%.17g,%.17g,%.17g"),
				P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W, S.X, S.Y, S.Z);
		}
		const FTCHARToUTF8 Utf8(*Text);
		return FSHA1::HashBuffer(Utf8.Get(), Utf8.Length()).ToString().ToUpper();
	}

	bool MMDEndfieldInitializeHeadFrame(const FMMDEndfieldHeadSkeleton& Source,
		FMMDReferenceHeadFrame& OutFrame, FString& OutMessage)
	{
		const FTransform& Head = Source.ReferenceTransforms[Source.Head];
		const FVector Scale = Head.GetScale3D().GetAbs();
		if (Head.ContainsNaN() || Scale.GetMin() <= UE_SMALL_NUMBER)
		{
			OutMessage = TEXT("参考头骨变换退化，无法建立头部框架");
			return false;
		}
		OutFrame.BoneName = Source.Skeleton->GetBoneName(Source.Head);
		OutFrame.HeadIndex = Source.Head;
		OutFrame.ReferenceHeadRotation = Head.GetRotation().GetNormalized();
		OutFrame.ReferenceHeadScale = Head.GetScale3D();
		OutFrame.ForwardRef = Source.bMMD ? FVector::YAxisVector : FVector::XAxisVector;
		OutFrame.UpRef = FVector::UpVector;
		OutFrame.RightRef = FVector::CrossProduct(OutFrame.ForwardRef, OutFrame.UpRef).GetSafeNormal();
		return true;
	}

	bool MMDEndfieldMakeSkeletonFrame(const FMMDEndfieldHeadSkeleton& Source,
		FMMDReferenceHeadFrame& OutFrame, FString& OutMessage)
	{
		if (!MMDEndfieldInitializeHeadFrame(Source, OutFrame, OutMessage)) return false;
		const FVector Head = Source.ReferenceTransforms[Source.Head].GetLocation();
		double Radius = 0.0;
		if (Source.LeftEye != INDEX_NONE && Source.RightEye != INDEX_NONE)
		{
			const FVector Left = Source.ReferenceTransforms[Source.LeftEye].GetLocation();
			const FVector Right = Source.ReferenceTransforms[Source.RightEye].GetLocation();
			Radius = 1.6 * FMath::Max(FVector::Distance(Left, Right), FVector::Distance((Left + Right) * 0.5, Head));
		}
		else if (Source.LeftEye != INDEX_NONE || Source.RightEye != INDEX_NONE)
		{
			const int32 Eye = Source.LeftEye != INDEX_NONE ? Source.LeftEye : Source.RightEye;
			Radius = 1.6 * FVector::Distance(Source.ReferenceTransforms[Eye].GetLocation(), Head);
		}
		if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER)
		{
			const int32 Parent = Source.Skeleton->GetParentIndex(Source.Head);
			if (Source.ReferenceTransforms.IsValidIndex(Parent))
				Radius = 1.2 * FVector::Distance(Head, Source.ReferenceTransforms[Parent].GetLocation());
		}
		if (!FMath::IsFinite(Radius) || Radius <= UE_SMALL_NUMBER || Radius > MAX_flt)
		{
			OutMessage = TEXT("头骨及眼骨没有可靠尺度，保持通用 Toon");
			return false;
		}
		// 尺度与中心解耦；缩小半径不能沿旧眼骨偏移把代理中心继续推向脸面。
		OutFrame.CenterHeadLocal = FVector::ZeroVector;
		OutFrame.Radius = static_cast<float>(Radius);
		OutFrame.bValid = true;
		return true;
	}
}

FString MMDEndfieldHeadFrame::ComputeSkeletonSignature(const USkeletalMesh* Mesh, FName PreferredHead)
{
	FMMDEndfieldHeadSkeleton Source;
	FString Message;
	return MMDEndfieldReadHeadSkeleton(Mesh, PreferredHead, Source, Message)
		? MMDEndfieldHeadSkeletonSignature(Source) : FString();
}

bool MMDEndfieldHeadFrame::BuildSkeletonFrame(const USkeletalMesh* Mesh, FName PreferredHead,
	FMMDReferenceHeadFrame& OutFrame, FString& OutMessage)
{
	OutFrame = FMMDReferenceHeadFrame();
	OutMessage.Reset();
	FMMDEndfieldHeadSkeleton Source;
	return MMDEndfieldReadHeadSkeleton(Mesh, PreferredHead, Source, OutMessage)
		&& MMDEndfieldMakeSkeletonFrame(Source, OutFrame, OutMessage);
}

bool MMDEndfieldHeadFrame::BuildReferenceFrame(const USkeletalMesh* Mesh, FName PreferredHead,
	FMMDReferenceHeadFrame& OutFrame, FString& OutMessage)
{
	OutFrame = FMMDReferenceHeadFrame();
	OutMessage.Reset();
	const UMMDEndfieldFaceData* Data = IsValid(Mesh)
		? const_cast<USkeletalMesh*>(Mesh)->GetAssetUserData<UMMDEndfieldFaceData>() : nullptr;
	if (Data && Data->Version == UMMDEndfieldFaceData::CurrentVersion && Data->FrameVersion == CurrentFrameVersion
		&& (PreferredHead.IsNone() || PreferredHead == Data->HeadBoneName))
	{
		FMMDEndfieldHeadSkeleton Source;
		FString CacheMessage;
		if (!Data->HeadBoneName.IsNone() && MMDEndfieldReadHeadSkeleton(Mesh, Data->HeadBoneName, Source, CacheMessage)
			&& !Data->HeadSkeletonSignature.IsEmpty()
			&& Data->HeadSkeletonSignature.Equals(MMDEndfieldHeadSkeletonSignature(Source), ESearchCase::IgnoreCase)
			&& !Data->HeadCenterHeadLocal.ContainsNaN() && FMath::IsFinite(Data->HeadRadius) && Data->HeadRadius > UE_SMALL_NUMBER
			&& !Data->HeadReferenceRotation.ContainsNaN() && Data->HeadReferenceRotation.SizeSquared() > UE_SMALL_NUMBER
			&& Data->HeadReferenceRotation.GetNormalized().Equals(Source.ReferenceTransforms[Source.Head].GetRotation().GetNormalized(), 0.0001)
			&& MMDEndfieldInitializeHeadFrame(Source, OutFrame, CacheMessage))
		{
			OutFrame.CenterHeadLocal = Data->HeadCenterHeadLocal;
			OutFrame.Radius = Data->HeadRadius;
			OutFrame.ReferenceHeadRotation = Data->HeadReferenceRotation.GetNormalized();
			OutFrame.bValid = true;
			OutFrame.bFromFaceData = true;
			return true;
		}
		OutMessage = TEXT("持久化头部框架已过期或无效，使用纯骨骼兜底");
	}
	FString FallbackMessage;
	FName FallbackHead = PreferredHead;
	if (FallbackHead.IsNone() && Data && !Data->HeadBoneName.IsNone()
		&& Mesh->GetRefSkeleton().FindBoneIndex(Data->HeadBoneName) != INDEX_NONE)
		FallbackHead = Data->HeadBoneName;
	const bool bValid = BuildSkeletonFrame(Mesh, FallbackHead, OutFrame, FallbackMessage);
	if (!FallbackMessage.IsEmpty())
		OutMessage = OutMessage.IsEmpty() ? FallbackMessage : OutMessage + TEXT("；") + FallbackMessage;
	return bValid;
}
