#include "MMDAnimInstance.h"
#include "MMDPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

UMMDAnimInstance::UMMDAnimInstance()
{
}

void UMMDAnimInstance::NativeInitializeAnimation()
{
    Super::NativeInitializeAnimation();

    // Get skeletal mesh component
    SkeletalMeshComponent = GetSkelMeshComponent();
    
    if (SkeletalMeshComponent)
    {
        // Find MMD physics component on the owner actor
        if (AActor* Owner = SkeletalMeshComponent->GetOwner())
        {
            PhysicsComponent = Owner->FindComponentByClass<UMMDPhysicsComponent>();
            
            if (PhysicsComponent)
            {
                UE_LOG(LogTemp, Log, TEXT("MMDAnimInstance: Found MMDPhysicsComponent"));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("MMDAnimInstance: No MMDPhysicsComponent found on owner"));
            }
        }
    }
}

void UMMDAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    if (bPhysicsEnabled && PhysicsComponent && PhysicsComponent->IsPhysicsEnabled())
    {
        UpdatePhysicsBones(DeltaSeconds);
        ApplyPhysicsToAnimation(DeltaSeconds);
    }
}

void UMMDAnimInstance::SetPhysicsBlendWeight(float Weight)
{
    PhysicsBlendWeight = FMath::Clamp(Weight, 0.0f, 1.0f);
}

void UMMDAnimInstance::EnablePhysicsSimulation(bool bEnable)
{
    bPhysicsEnabled = bEnable;
    
    if (PhysicsComponent)
    {
        PhysicsComponent->SetPhysicsEnabled(bEnable);
    }
}

void UMMDAnimInstance::UpdatePhysicsBones(float DeltaSeconds)
{
    // Physics bones are updated by the physics component itself
    // This function can be used for additional processing if needed
}

void UMMDAnimInstance::ApplyPhysicsToAnimation(float DeltaSeconds)
{
    if (!PhysicsComponent || !SkeletalMeshComponent)
    {
        return;
    }

    // Get physics bones
    const TArray<FMMDPhysicsBone>& PhysicsBones = PhysicsComponent->GetPhysicsBones();
    
    // TODO: Apply physics bone transforms to skeletal mesh bones
    // This requires modifying bone transforms in the animation pose
    // For now, this is a placeholder for future implementation
    // In a full implementation, we would:
    // 1. Get the current animation pose
    // 2. For each physics bone, blend its physics-simulated transform with the animation transform
    // 3. Apply the blended transform back to the pose
    // 4. Handle bone space conversion (component space vs local space)
}
