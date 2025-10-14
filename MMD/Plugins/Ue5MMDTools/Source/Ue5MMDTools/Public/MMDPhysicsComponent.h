#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TPMXParser.h"
#include "MMDPhysicsComponent.generated.h"

// MMD Physics Mode
UENUM(BlueprintType)
enum class EMMDPhysicsMode : uint8
{
    Static = 0 UMETA(DisplayName = "Static (Kinematic)"),
    Dynamic = 1 UMETA(DisplayName = "Dynamic (Simulated)"),
    BoneTracked = 2 UMETA(DisplayName = "Bone Tracked (Hybrid)")
};

// MMD Shape Type
UENUM(BlueprintType)
enum class EMMDShapeType : uint8
{
    Sphere = 0 UMETA(DisplayName = "Sphere"),
    Box = 1 UMETA(DisplayName = "Box"),
    Capsule = 2 UMETA(DisplayName = "Capsule")
};

// Physics bone data structure
USTRUCT(BlueprintType)
struct FMMDPhysicsBone
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FString BoneName;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    int32 BoneIndex = -1;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    EMMDPhysicsMode PhysicsMode = EMMDPhysicsMode::Dynamic;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    EMMDShapeType ShapeType = EMMDShapeType::Sphere;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector Size = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector Position = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FRotator Rotation = FRotator::ZeroRotator;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    float Mass = 1.0f;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    float LinearDamping = 0.5f;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    float AngularDamping = 0.5f;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    float Restitution = 0.0f;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    float Friction = 0.5f;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    uint8 CollisionGroup = 0;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    uint16 CollisionMask = 0xFFFF;

    // Physics simulation state
    FVector Velocity = FVector::ZeroVector;
    FVector AngularVelocity = FVector::ZeroVector;
    FVector CurrentPosition = FVector::ZeroVector;
    FVector PreviousPosition = FVector::ZeroVector;
    FQuat CurrentRotation = FQuat::Identity;
    FQuat PreviousRotation = FQuat::Identity;
};

// Constraint data
USTRUCT(BlueprintType)
struct FMMDPhysicsConstraint
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FString ConstraintName;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    int32 RigidBodyA = -1;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    int32 RigidBodyB = -1;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector Position = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FRotator Rotation = FRotator::ZeroRotator;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector LinearLowerLimit = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector LinearUpperLimit = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector AngularLowerLimit = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector AngularUpperLimit = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector SpringLinear = FVector::ZeroVector;

    UPROPERTY(BlueprintReadWrite, Category = "MMD Physics")
    FVector SpringAngular = FVector::ZeroVector;
};

UCLASS(ClassGroup = (MMD), meta = (BlueprintSpawnableComponent))
class UE5MMDTOOLS_API UMMDPhysicsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UMMDPhysicsComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
    virtual void BeginPlay() override;

    // Initialize physics from PMX data
    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    void InitializeFromPMXData(const PMXDatas& PMXData);

    // Physics control
    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    void SetPhysicsEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    bool IsPhysicsEnabled() const { return bPhysicsEnabled; }

    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    void SetGravity(FVector InGravity);

    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    FVector GetGravity() const { return Gravity; }

    // Get physics bones
    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    const TArray<FMMDPhysicsBone>& GetPhysicsBones() const { return PhysicsBones; }

    // Get physics constraints
    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    const TArray<FMMDPhysicsConstraint>& GetPhysicsConstraints() const { return PhysicsConstraints; }

    // Physics parameter adjustment
    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    void SetPhysicsBoneDamping(int32 BoneIndex, float LinearDamping, float AngularDamping);

    UFUNCTION(BlueprintCallable, Category = "MMD Physics")
    void SetPhysicsBoneMass(int32 BoneIndex, float Mass);

protected:
    // Physics simulation
    void UpdatePhysics(float DeltaTime);
    void IntegrateVerlet(float DeltaTime);
    void SolveConstraints(float DeltaTime);
    void ApplyDamping(float DeltaTime);
    void UpdateBoneTransforms();

    // Coordinate conversion (MMD to UE5)
    FVector ConvertMMDToUE5Position(const FVector& MMDPos) const;
    FRotator ConvertMMDToUE5Rotation(const FVector& MMDRot) const;
    FVector ConvertMMDToUE5Scale(const FVector& MMDScale) const;

private:
    UPROPERTY(EditAnywhere, Category = "MMD Physics")
    bool bPhysicsEnabled = true;

    UPROPERTY(EditAnywhere, Category = "MMD Physics")
    FVector Gravity = FVector(0.0f, 0.0f, -980.0f);

    UPROPERTY(EditAnywhere, Category = "MMD Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float PhysicsBlendWeight = 1.0f;

    UPROPERTY(EditAnywhere, Category = "MMD Physics")
    int32 ConstraintIterations = 4;

    UPROPERTY(VisibleAnywhere, Category = "MMD Physics")
    TArray<FMMDPhysicsBone> PhysicsBones;

    UPROPERTY(VisibleAnywhere, Category = "MMD Physics")
    TArray<FMMDPhysicsConstraint> PhysicsConstraints;

    // Reference to skeletal mesh component
    UPROPERTY()
    class USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
};
