#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "TPMXParser.h"
#include "TVMDParser.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "AGN_MMDSkeletalControl.h"
#include "AMMDActor.generated.h"


/** SetupComponents 异步流程结束（成功或失败）后广播（游戏线程）。 */
DECLARE_MULTICAST_DELEGATE_OneParam(FOnMMDActorSetupFinished, AMMDActor* /*Actor*/);

UCLASS()
class UE5MMDTOOLS_API AMMDActor : public AActor
{
    GENERATED_BODY()
public:

	AMMDActor();

	/** 原动画蓝图保持可用；显式播放 VMD 动画时默认带 MMD 物理。 */
	UFUNCTION(BlueprintCallable, Category = "MMD|Animation")
	bool PlayMMDAnimation(class UAnimationAsset* Animation, bool bLoop = true, bool bEnablePhysics = true);
	UFUNCTION(BlueprintCallable, Category = "MMD|Animation")
	void SetMMDAnimationPlaying(bool bPlaying);
	UFUNCTION(BlueprintCallable, Category = "MMD|Physics")
	void SetMMDPhysicsEnabled(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "MMD|Physics")
	void ResetMMDPhysics();

    UFUNCTION(BlueprintCallable, Category = "MMD")
    USkeletalMeshComponent* GetMeshComponent() const { return SkeletalMeshComponent; }


    void SetupComponents(const FString& FilePath);

	/** 异步导入完成后的收尾（组件/动画蓝图/IK 资产）。BuiltMesh 为 null 表示导入失败。 */
	void FinishSetupComponents(const FString& FilePath, USkeletalMesh* BuiltMesh, const PMXDatas* PMXData);

	/** 订阅导入完成事件（游戏线程广播）。 */
	FOnMMDActorSetupFinished& OnSetupFinished() { return SetupFinishedDelegate; }

	/*void InitializeMMDPhysics(UAnimGraphNode_MMDSkeletalControl* MMDNode,const PMXDatas& PMXData);*/
    UPROPERTY(EditAnywhere, Category = "MMD")
    FString SourcePMXFilePath;

protected:

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

#if WITH_EDITOR
    virtual void OnConstruction(const FTransform& Transform) override;
    void InitSimulatorForPreviewIfNeeded();
#endif
private:
    //
    UPROPERTY(VisibleAnywhere, Category = "MMD")
    USkeletalMeshComponent* SkeletalMeshComponent = nullptr;

    UPROPERTY(VisibleAnywhere, Category = "MMD")
	class UCapsuleComponent* CapsuleComponent = nullptr;
    UPROPERTY(VisibleAnywhere, Category = "MMD")
	USceneComponent* RootSceneComponent = nullptr;


	FOnMMDActorSetupFinished SetupFinishedDelegate;
};
