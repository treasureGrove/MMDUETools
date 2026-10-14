#include "Rendering/MMDEndfieldFaceData.h"

#include "Engine/Texture2D.h"

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
	if (Version != CurrentVersion) return nullptr;
	for (const FMMDGeneratedFaceSdfSlot& Slot : Slots)
	{
		if (Slot.MaterialIndex == MaterialIndex && Slot.MaterialSlotName == MaterialSlotName
			&& Slot.DirectionVersion == CurrentVersion && Slot.UVChannel == 0 && IsValid(Slot.DirectionTexture))
			return Slot.DirectionTexture;
	}
	return nullptr;
}
