#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MMDEndfieldNativeBackendValidationActor.generated.h"

class UCameraComponent;

/** 仅显式游戏运行验证使用；编辑器打开地图不会截图或退出。 */
UCLASS()
class UE5MMDTOOLS_API AMMDEndfieldNativeBackendValidationActor : public AActor
{
	GENERATED_BODY()

public:
	AMMDEndfieldNativeBackendValidationActor();
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MMD|Validation")
	TObjectPtr<UCameraComponent> ValidationCamera;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Validation")
	FVector CameraLocation = FVector(450.0, -650.0, 400.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Validation")
	FVector CameraLookAt = FVector(0.0, 0.0, 100.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Validation", meta = (ClampMin = "5.0", ClampMax = "170.0"))
	float CameraFieldOfView = 50.0f;

	/** 可用地图内已有相机替代自带相机。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD|Validation")
	TObjectPtr<AActor> CameraViewTarget;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void OnValidationScreenshot(int32 Width, int32 Height, const TArray<FColor>& Colors);
	bool ApplyValidationCamera();
	void RebindValidationProfiles();
	void FinishValidation(bool bSuccess);
	void ReleaseScreenshotDelegate();
	bool WriteValidationReport(bool bSuccess);

	bool bValidationEnabled = false;
	bool bValidationFinished = false;
	bool bCameraApplied = false;
	bool bScreenshotRequested = false;
	bool bScreenshotSaved = false;
	int32 StableFrameCount = 0;
	int32 ScreenshotRequestFrame = 0;
	int32 ScreenshotCapturedFrame = 0;
	int32 ScreenshotWidth = 0;
	int32 ScreenshotHeight = 0;
	int32 OriginalScreenshotDelegateValue = 1;
	bool bChangedScreenshotDelegate = false;
	int32 EligibleProfileMaterialCount = 0;
	int32 ReboundProfileMaterialCount = 0;
	int32 MatchedProfileMetadataCount = 0;
	TArray<FString> ReboundProfileReports;
	FString ProfileRebindReason;
	FDelegateHandle ScreenshotDelegateHandle;
	FString OutputDirectory;
	FString ScreenshotPath;
	FString ReportPath;
	FString FailureReason;
};
