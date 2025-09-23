#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "AMMDPreviewActor.generated.h"

class UMMDPhysicsComponent;
class UMMDAnimationComponent;

UCLASS()
class UE5MMDTOOLS_API AAMMDPreviewActor : public AActor
{
	GENERATED_BODY()
public:
	AAMMDPreviewActor();

	void BuildFromPMXData(const PMXDatas& PMXInfo, const FString& PMXFilePath);


	void PlayVMDAnimation(const VMDData& VMDInfo);


	USkeletalMeshComponent* GetMMDMeshComponent() const { return MMDSkeletalMeshComponent; };


	void PlayAnimation(bool bPlay = true);

	UFUNCTION(BlueprintCallable, Category = "MMD")
	void SetAnimationTime(float Time);

protected:

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD Components")
	USkeletalMeshComponent* MMDSkeletalMeshComponent;
private:
	PMXDatas LoadedPMXData;

	FString LoadedPMXPath;

	bool bIsPlaying = false;
	float CurrentAnimTime = 0.0f;

	void SetupMMDComponents();
	void CleanupPreviousModel();
};
