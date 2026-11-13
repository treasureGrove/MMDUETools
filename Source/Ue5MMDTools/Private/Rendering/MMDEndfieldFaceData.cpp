#include "Rendering/MMDEndfieldFaceData.h"

#include "Engine/Texture2D.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/MMDEndfieldHeadFrame.h"

UTexture2D* UMMDEndfieldFaceData::FindTexture(int32 MaterialIndex, FName MaterialSlotName) const
{
	if (Version != LegacyThresholdVersion) return nullptr;
	for (const FMMDGeneratedFaceSdfSlot& Slot : Slots)
	{
		if (Slot.MaterialIndex == MaterialIndex && Slot.MaterialSlotName == MaterialSlotName && IsValid(Slot.Texture))
			return Slot.Texture;
	}
	return nullptr;
}

UTexture2D* UMMDEndfieldFaceData::FindDirectionTexture(int32 MaterialIndex, FName MaterialSlotName) const
{
	const FMMDGeneratedFaceSdfSlot* Slot = FindDirectionSlot(MaterialIndex, MaterialSlotName);
	return Slot ? Slot->DirectionTexture.Get() : nullptr;
}

const FMMDGeneratedFaceSdfSlot* UMMDEndfieldFaceData::FindDirectionSlot(int32 MaterialIndex, FName MaterialSlotName) const
{
	if (Version != CurrentVersion || FrameVersion != MMDEndfieldHeadFrame::CurrentFrameVersion) return nullptr;
	const USkeletalMesh* Mesh = GetTypedOuter<USkeletalMesh>();
	if (!Mesh || !Mesh->GetMaterials().IsValidIndex(MaterialIndex)
		|| Mesh->GetMaterials()[MaterialIndex].MaterialSlotName != MaterialSlotName) return nullptr;
	FMMDReferenceHeadFrame Frame;
	FString Message;
	if (!MMDEndfieldHeadFrame::BuildReferenceFrame(Mesh, HeadBoneName, Frame, Message)
		|| !Frame.bFromFaceData) return nullptr;
	for (const FMMDGeneratedFaceSdfSlot& Slot : Slots)
	{
		if (Slot.MaterialIndex == MaterialIndex && Slot.MaterialSlotName == MaterialSlotName
			&& Slot.DirectionVersion == CurrentVersion && Slot.UVChannel == -1 && IsValid(Slot.DirectionTexture))
		{
			if (!Slot.bForwardOverlay) return &Slot;
			if (Slot.HostMaterialIndex == MaterialIndex || !Mesh->GetMaterials().IsValidIndex(Slot.HostMaterialIndex)
				|| Mesh->GetMaterials()[Slot.HostMaterialIndex].MaterialSlotName != Slot.HostSlotName) return nullptr;
			for (const FMMDGeneratedFaceSdfSlot& Host : Slots)
			{
				if (Host.MaterialIndex == Slot.HostMaterialIndex && Host.MaterialSlotName == Slot.HostSlotName
					&& !Host.bForwardOverlay && Host.DirectionVersion == CurrentVersion && Host.UVChannel == -1
					&& IsValid(Host.DirectionTexture) && Host.DirectionTexture == Slot.DirectionTexture)
					return &Slot;
			}
			return nullptr;
		}
	}
	return nullptr;
}

uint32 UMMDEndfieldFaceData::ComputeBindingSignature() const
{
	uint32 Signature = HashCombineFast(GetTypeHash(Version), GetTypeHash(Slots.Num()));
	const USkeletalMesh* Mesh = GetTypedOuter<USkeletalMesh>();
	for (const FMMDGeneratedFaceSdfSlot& Slot : Slots)
	{
		Signature = HashCombineFast(Signature, GetTypeHash(Slot.MaterialIndex));
		Signature = HashCombineFast(Signature, GetTypeHash(Slot.MaterialSlotName));
		Signature = HashCombineFast(Signature, PointerHash(Slot.DirectionTexture.Get()));
		Signature = HashCombineFast(Signature, GetTypeHash(Slot.DirectionVersion));
		Signature = HashCombineFast(Signature, GetTypeHash(Slot.UVChannel));
		Signature = HashCombineFast(Signature, GetTypeHash(Slot.HostMaterialIndex));
		Signature = HashCombineFast(Signature, GetTypeHash(Slot.HostSlotName));
		Signature = HashCombineFast(Signature, GetTypeHash(Slot.bForwardOverlay));
		// 网格原位重排槽名也必须令已缓存的 host 身份失效。
		Signature = HashCombineFast(Signature, GetTypeHash(Mesh && Mesh->GetMaterials().IsValidIndex(Slot.MaterialIndex)
			? Mesh->GetMaterials()[Slot.MaterialIndex].MaterialSlotName : NAME_None));
		Signature = HashCombineFast(Signature, GetTypeHash(Mesh && Mesh->GetMaterials().IsValidIndex(Slot.HostMaterialIndex)
			? Mesh->GetMaterials()[Slot.HostMaterialIndex].MaterialSlotName : NAME_None));
	}
	return Signature;
}
