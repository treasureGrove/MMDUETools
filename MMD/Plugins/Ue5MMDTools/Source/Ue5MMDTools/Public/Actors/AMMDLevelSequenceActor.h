#pragma once

#include "CoreMinimal.h"
#include "LevelSequenceActor.h"
#include "AMMDLevelSequenceActor.generated.h"

UCLASS()
class UE5MMDTOOLS_API AMMDLevelSequenceActor : public ALevelSequenceActor
{
	GENERATED_BODY()

public:
	AMMDLevelSequenceActor(const FObjectInitializer& ObjectInitializer);

	virtual void PostInitializeComponents() override;

#if WITH_EDITOR
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void ApplySelfTransformOrigin();
};
