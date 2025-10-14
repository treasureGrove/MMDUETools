#include "MMDPhysicsComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"

UMMDPhysicsComponent::UMMDPhysicsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UMMDPhysicsComponent::BeginPlay()
{
    Super::BeginPlay();

    // Find skeletal mesh component
    if (AActor* Owner = GetOwner())
    {
        SkeletalMeshComponent = Owner->FindComponentByClass<USkeletalMeshComponent>();
        if (!SkeletalMeshComponent)
        {
            UE_LOG(LogTemp, Warning, TEXT("MMDPhysicsComponent: No SkeletalMeshComponent found on owner"));
        }
    }
}

void UMMDPhysicsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (bPhysicsEnabled && PhysicsBones.Num() > 0)
    {
        UpdatePhysics(DeltaTime);
    }
}

void UMMDPhysicsComponent::InitializeFromPMXData(const PMXDatas& PMXData)
{
    PhysicsBones.Empty();
    PhysicsConstraints.Empty();

    // Convert PMX rigid bodies to physics bones
    for (int32 i = 0; i < PMXData.ModelRigidCount; ++i)
    {
        const PMXRigid& Rigid = PMXData.ModelRigids[i];
        FMMDPhysicsBone PhysicsBone;

        PhysicsBone.BoneName = Rigid.NameEN.IsEmpty() ? Rigid.NameJP : Rigid.NameEN;
        PhysicsBone.BoneIndex = Rigid.RelatedBoneIndex;
        PhysicsBone.PhysicsMode = static_cast<EMMDPhysicsMode>(Rigid.PhysicsMode);
        PhysicsBone.ShapeType = static_cast<EMMDShapeType>(Rigid.ShapeType);
        
        // Convert MMD coordinates to UE5
        PhysicsBone.Size = ConvertMMDToUE5Scale(Rigid.Size);
        PhysicsBone.Position = ConvertMMDToUE5Position(Rigid.Position);
        PhysicsBone.Rotation = ConvertMMDToUE5Rotation(Rigid.Rotation);

        PhysicsBone.Mass = Rigid.Mass;
        PhysicsBone.LinearDamping = Rigid.LinearDamping;
        PhysicsBone.AngularDamping = Rigid.AngularDamping;
        PhysicsBone.Restitution = Rigid.Restitution;
        PhysicsBone.Friction = Rigid.Friction;
        PhysicsBone.CollisionGroup = Rigid.Group;
        PhysicsBone.CollisionMask = Rigid.CollisionMask;

        // Initialize simulation state
        PhysicsBone.CurrentPosition = PhysicsBone.Position;
        PhysicsBone.PreviousPosition = PhysicsBone.Position;
        PhysicsBone.CurrentRotation = PhysicsBone.Rotation.Quaternion();
        PhysicsBone.PreviousRotation = PhysicsBone.CurrentRotation;

        PhysicsBones.Add(PhysicsBone);
    }

    // Convert PMX joints to physics constraints
    for (int32 i = 0; i < PMXData.ModelJointCount; ++i)
    {
        const PMXJoint& Joint = PMXData.ModelJoints[i];
        FMMDPhysicsConstraint Constraint;

        Constraint.ConstraintName = Joint.NameEN.IsEmpty() ? Joint.NameJP : Joint.NameEN;
        Constraint.RigidBodyA = Joint.RigidA;
        Constraint.RigidBodyB = Joint.RigidB;
        
        Constraint.Position = ConvertMMDToUE5Position(Joint.Position);
        Constraint.Rotation = ConvertMMDToUE5Rotation(Joint.Rotation);
        
        Constraint.LinearLowerLimit = ConvertMMDToUE5Position(Joint.LimitPosLower);
        Constraint.LinearUpperLimit = ConvertMMDToUE5Position(Joint.LimitPosUpper);
        Constraint.AngularLowerLimit = Joint.LimitRotLower;
        Constraint.AngularUpperLimit = Joint.LimitRotUpper;
        
        Constraint.SpringLinear = Joint.SpringPos;
        Constraint.SpringAngular = Joint.SpringRot;

        PhysicsConstraints.Add(Constraint);
    }

    UE_LOG(LogTemp, Log, TEXT("MMDPhysicsComponent: Initialized %d physics bones and %d constraints"), 
        PhysicsBones.Num(), PhysicsConstraints.Num());
}

void UMMDPhysicsComponent::SetPhysicsEnabled(bool bEnabled)
{
    bPhysicsEnabled = bEnabled;
}

void UMMDPhysicsComponent::SetGravity(FVector InGravity)
{
    Gravity = InGravity;
}

void UMMDPhysicsComponent::SetPhysicsBoneDamping(int32 BoneIndex, float LinearDamping, float AngularDamping)
{
    if (PhysicsBones.IsValidIndex(BoneIndex))
    {
        PhysicsBones[BoneIndex].LinearDamping = LinearDamping;
        PhysicsBones[BoneIndex].AngularDamping = AngularDamping;
    }
}

void UMMDPhysicsComponent::SetPhysicsBoneMass(int32 BoneIndex, float Mass)
{
    if (PhysicsBones.IsValidIndex(BoneIndex))
    {
        PhysicsBones[BoneIndex].Mass = Mass;
    }
}

void UMMDPhysicsComponent::UpdatePhysics(float DeltaTime)
{
    // Clamp delta time to avoid instability
    const float ClampedDeltaTime = FMath::Min(DeltaTime, 0.033f);

    // Verlet integration
    IntegrateVerlet(ClampedDeltaTime);

    // Solve constraints multiple times for stability
    for (int32 i = 0; i < ConstraintIterations; ++i)
    {
        SolveConstraints(ClampedDeltaTime);
    }

    // Apply damping
    ApplyDamping(ClampedDeltaTime);

    // Update bone transforms
    UpdateBoneTransforms();
}

void UMMDPhysicsComponent::IntegrateVerlet(float DeltaTime)
{
    const float DeltaTimeSquared = DeltaTime * DeltaTime;

    for (FMMDPhysicsBone& Bone : PhysicsBones)
    {
        // Skip static and bone-tracked bones for now
        if (Bone.PhysicsMode != EMMDPhysicsMode::Dynamic)
        {
            continue;
        }

        if (Bone.Mass <= 0.0f)
        {
            continue;
        }

        // Verlet integration: x(t+dt) = 2*x(t) - x(t-dt) + a*dt^2
        FVector CurrentPos = Bone.CurrentPosition;
        FVector Acceleration = Gravity / Bone.Mass;
        
        FVector NewPosition = 2.0f * CurrentPos - Bone.PreviousPosition + Acceleration * DeltaTimeSquared;

        Bone.PreviousPosition = CurrentPos;
        Bone.CurrentPosition = NewPosition;
        Bone.Velocity = (NewPosition - CurrentPos) / DeltaTime;
    }
}

void UMMDPhysicsComponent::SolveConstraints(float DeltaTime)
{
    for (const FMMDPhysicsConstraint& Constraint : PhysicsConstraints)
    {
        if (!PhysicsBones.IsValidIndex(Constraint.RigidBodyA) || 
            !PhysicsBones.IsValidIndex(Constraint.RigidBodyB))
        {
            continue;
        }

        FMMDPhysicsBone& BoneA = PhysicsBones[Constraint.RigidBodyA];
        FMMDPhysicsBone& BoneB = PhysicsBones[Constraint.RigidBodyB];

        // Skip if both are static
        if (BoneA.PhysicsMode == EMMDPhysicsMode::Static && 
            BoneB.PhysicsMode == EMMDPhysicsMode::Static)
        {
            continue;
        }

        // Simple spring constraint
        FVector Delta = BoneB.CurrentPosition - BoneA.CurrentPosition;
        FVector TargetDelta = Constraint.Position;
        FVector Correction = (TargetDelta - Delta) * 0.5f;

        // Apply spring forces
        float SpringStrength = (Constraint.SpringLinear.X + Constraint.SpringLinear.Y + Constraint.SpringLinear.Z) / 3.0f;
        Correction *= FMath::Clamp(SpringStrength, 0.0f, 1.0f);

        if (BoneA.PhysicsMode == EMMDPhysicsMode::Dynamic)
        {
            BoneA.CurrentPosition -= Correction * 0.5f;
        }
        if (BoneB.PhysicsMode == EMMDPhysicsMode::Dynamic)
        {
            BoneB.CurrentPosition += Correction * 0.5f;
        }
    }
}

void UMMDPhysicsComponent::ApplyDamping(float DeltaTime)
{
    for (FMMDPhysicsBone& Bone : PhysicsBones)
    {
        if (Bone.PhysicsMode != EMMDPhysicsMode::Dynamic)
        {
            continue;
        }

        // Linear damping
        float LinearDampingFactor = FMath::Pow(1.0f - Bone.LinearDamping, DeltaTime);
        Bone.Velocity *= LinearDampingFactor;

        // Angular damping
        float AngularDampingFactor = FMath::Pow(1.0f - Bone.AngularDamping, DeltaTime);
        Bone.AngularVelocity *= AngularDampingFactor;
    }
}

void UMMDPhysicsComponent::UpdateBoneTransforms()
{
    if (!SkeletalMeshComponent || !SkeletalMeshComponent->GetSkeletalMeshAsset())
    {
        return;
    }

    // TODO: Apply physics bone transforms to skeletal mesh bones
    // This requires integration with the animation system
    // For now, this is a placeholder for future implementation
}

FVector UMMDPhysicsComponent::ConvertMMDToUE5Position(const FVector& MMDPos) const
{
    // MMD: Right-handed, Y-up
    // UE5: Left-handed, Z-up
    // Conversion: X -> X, Y -> Z, Z -> -Y
    // Scale: MMD uses cm, UE5 uses cm (same scale)
    return FVector(MMDPos.X, -MMDPos.Z, MMDPos.Y);
}

FRotator UMMDPhysicsComponent::ConvertMMDToUE5Rotation(const FVector& MMDRot) const
{
    // Convert MMD Euler angles (radians) to UE5 rotator
    // MMD rotation order is typically X-Y-Z
    FRotator UE5Rot;
    UE5Rot.Pitch = FMath::RadiansToDegrees(MMDRot.X);
    UE5Rot.Yaw = FMath::RadiansToDegrees(-MMDRot.Y);
    UE5Rot.Roll = FMath::RadiansToDegrees(MMDRot.Z);
    return UE5Rot;
}

FVector UMMDPhysicsComponent::ConvertMMDToUE5Scale(const FVector& MMDScale) const
{
    // Scale conversion (same as position for proportions)
    return FVector(MMDScale.X, MMDScale.Z, MMDScale.Y);
}
