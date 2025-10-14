#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MMDPhysicsComponent.h"
#include "MMDPhysicsDebugDraw.generated.h"

/**
 * Debug visualization component for MMD physics system
 * Draws physics shapes, constraints, and collision groups
 */
UCLASS(ClassGroup = (MMD), meta = (BlueprintSpawnableComponent))
class UE5MMDTOOLS_API UMMDPhysicsDebugDraw : public UActorComponent
{
    GENERATED_BODY()

public:
    UMMDPhysicsDebugDraw();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void BeginPlay() override;

    // Enable/disable debug drawing
    UFUNCTION(BlueprintCallable, Category = "MMD Physics Debug")
    void SetDebugDrawEnabled(bool bEnabled) { bDebugDrawEnabled = bEnabled; }

    UFUNCTION(BlueprintPure, Category = "MMD Physics Debug")
    bool IsDebugDrawEnabled() const { return bDebugDrawEnabled; }

    // Draw settings
    UFUNCTION(BlueprintCallable, Category = "MMD Physics Debug")
    void SetDrawPhysicsBones(bool bDraw) { bDrawPhysicsBones = bDraw; }

    UFUNCTION(BlueprintCallable, Category = "MMD Physics Debug")
    void SetDrawConstraints(bool bDraw) { bDrawConstraints = bDraw; }

    UFUNCTION(BlueprintCallable, Category = "MMD Physics Debug")
    void SetDrawCollisionGroups(bool bDraw) { bDrawCollisionGroups = bDraw; }

    UFUNCTION(BlueprintCallable, Category = "MMD Physics Debug")
    void SetDrawVelocities(bool bDraw) { bDrawVelocities = bDraw; }

protected:
    void DrawPhysicsBones();
    void DrawConstraints();
    void DrawVelocities();
    
    void DrawSphere(const FVector& Center, float Radius, const FColor& Color);
    void DrawBox(const FVector& Center, const FVector& Extent, const FRotator& Rotation, const FColor& Color);
    void DrawCapsule(const FVector& Center, float Radius, float HalfHeight, const FRotator& Rotation, const FColor& Color);
    
    FColor GetColorForCollisionGroup(uint8 Group) const;
    FColor GetColorForPhysicsMode(EMMDPhysicsMode Mode) const;

private:
    UPROPERTY(EditAnywhere, Category = "MMD Physics Debug")
    bool bDebugDrawEnabled = false;

    UPROPERTY(EditAnywhere, Category = "MMD Physics Debug")
    bool bDrawPhysicsBones = true;

    UPROPERTY(EditAnywhere, Category = "MMD Physics Debug")
    bool bDrawConstraints = true;

    UPROPERTY(EditAnywhere, Category = "MMD Physics Debug")
    bool bDrawCollisionGroups = false;

    UPROPERTY(EditAnywhere, Category = "MMD Physics Debug")
    bool bDrawVelocities = false;

    UPROPERTY(EditAnywhere, Category = "MMD Physics Debug", meta = (ClampMin = "0.1", ClampMax = "5.0"))
    float LineThickness = 1.0f;

    UPROPERTY(EditAnywhere, Category = "MMD Physics Debug", meta = (ClampMin = "0.1", ClampMax = "100.0"))
    float VelocityScale = 10.0f;

    UPROPERTY()
    UMMDPhysicsComponent* PhysicsComponent = nullptr;
};
