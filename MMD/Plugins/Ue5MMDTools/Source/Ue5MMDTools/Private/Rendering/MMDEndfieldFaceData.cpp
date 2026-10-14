#include "Rendering/MMDEndfieldFaceData.h"

#include "Engine/Texture2D.h"

UTexture2D* UMMDEndfieldFaceData::FindTexture(int32 MaterialIndex, FName MaterialSlotName) const
{
	if (Version != CurrentVersion) return nullptr;
	for (const FMMDGeneratedFaceSdfSlot& Slot : Slots)
	{
		if (Slot.MaterialIndex == MaterialIndex && Slot.MaterialSlotName == MaterialSlotName && IsValid(Slot.Texture))
			return Slot.Texture;
	}
	return nullptr;
}
