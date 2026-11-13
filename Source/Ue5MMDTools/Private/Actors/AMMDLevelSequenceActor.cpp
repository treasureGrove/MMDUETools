#include "Actors/AMMDLevelSequenceActor.h"

#include "DefaultLevelSequenceInstanceData.h"

AMMDLevelSequenceActor::AMMDLevelSequenceActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void AMMDLevelSequenceActor::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	ApplySelfTransformOrigin();
}

#if WITH_EDITOR
void AMMDLevelSequenceActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplySelfTransformOrigin();
}

void AMMDLevelSequenceActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	ApplySelfTransformOrigin();
}
#endif

void AMMDLevelSequenceActor::ApplySelfTransformOrigin()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		return;
	}

	bOverrideInstanceData = true;

	UDefaultLevelSequenceInstanceData* InstanceData = Cast<UDefaultLevelSequenceInstanceData>(DefaultInstanceData);
	if (!InstanceData)
	{
		InstanceData = NewObject<UDefaultLevelSequenceInstanceData>(this, TEXT("MMDTransformOriginInstanceData"), RF_Transactional);
		DefaultInstanceData = InstanceData;
	}

	if (InstanceData)
	{
		InstanceData->TransformOriginActor = this;
		InstanceData->TransformOrigin = FTransform::Identity;
	}
}
