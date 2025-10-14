#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "MMDAnimInstance.generated.h"

class UMMDPhysicsComponent;

/**
 * MMD Animation Instance for integration with Animation Blueprints
 * Provides physics bone updates and MMD-specific animation features
 */
UCLASS(Blueprintable, BlueprintType)
class UE5MMDTOOLS_API UMMDAnimInstance : public UAnimInstance
{
    GENERATED_BODY()

public:
    UMMDAnimInstance();

    virtual void NativeInitializeAnimation() override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;

    // Blueprint-callable functions
    UFUNCTION(BlueprintCallable, Category = "MMD Animation")
    void SetPhysicsBlendWeight(float Weight);

    UFUNCTION(BlueprintPure, Category = "MMD Animation")
    float GetPhysicsBlendWeight() const { return PhysicsBlendWeight; }

    UFUNCTION(BlueprintCallable, Category = "MMD Animation")
    void EnablePhysicsSimulation(bool bEnable);

    UFUNCTION(BlueprintPure, Category = "MMD Animation")
    bool IsPhysicsSimulationEnabled() const { return bPhysicsEnabled; }

protected:
    // Update physics bones from physics component
    void UpdatePhysicsBones(float DeltaSeconds);

    // Apply physics results to animation
    void ApplyPhysicsToAnimation(float DeltaSeconds);

private:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Animation", meta = (AllowPrivateAccess = "true"))
    float PhysicsBlendWeight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMD Animation", meta = (AllowPrivateAccess = "true"))
    bool bPhysicsEnabled = true;

    UPROPERTY()
    UMMDPhysicsComponent* PhysicsComponent = nullptr;

    // Cached skeletal mesh component
    UPROPERTY()
    class USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
};
