#include "MMDPhysicsDebugDraw.h"
#include "MMDPhysicsComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Actor.h"

UMMDPhysicsDebugDraw::UMMDPhysicsDebugDraw()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UMMDPhysicsDebugDraw::BeginPlay()
{
    Super::BeginPlay();

    // Find physics component on the owner
    if (AActor* Owner = GetOwner())
    {
        PhysicsComponent = Owner->FindComponentByClass<UMMDPhysicsComponent>();
        
        if (!PhysicsComponent)
        {
            UE_LOG(LogTemp, Warning, TEXT("MMDPhysicsDebugDraw: No MMDPhysicsComponent found on owner"));
        }
    }
}

void UMMDPhysicsDebugDraw::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bDebugDrawEnabled || !PhysicsComponent)
    {
        return;
    }

    if (bDrawPhysicsBones)
    {
        DrawPhysicsBones();
    }

    if (bDrawConstraints)
    {
        DrawConstraints();
    }

    if (bDrawVelocities)
    {
        DrawVelocities();
    }
}

void UMMDPhysicsDebugDraw::DrawPhysicsBones()
{
    if (!PhysicsComponent)
    {
        return;
    }

    const TArray<FMMDPhysicsBone>& PhysicsBones = PhysicsComponent->GetPhysicsBones();
    UWorld* World = GetWorld();
    
    if (!World)
    {
        return;
    }

    for (const FMMDPhysicsBone& Bone : PhysicsBones)
    {
        FColor Color = GetColorForPhysicsMode(Bone.PhysicsMode);
        
        if (bDrawCollisionGroups)
        {
            Color = GetColorForCollisionGroup(Bone.CollisionGroup);
        }

        FVector WorldPosition = GetOwner()->GetActorTransform().TransformPosition(Bone.CurrentPosition);

        switch (Bone.ShapeType)
        {
            case EMMDShapeType::Sphere:
                DrawSphere(WorldPosition, Bone.Size.X, Color);
                break;
                
            case EMMDShapeType::Box:
                DrawBox(WorldPosition, Bone.Size, Bone.Rotation, Color);
                break;
                
            case EMMDShapeType::Capsule:
                DrawCapsule(WorldPosition, Bone.Size.X, Bone.Size.Y * 0.5f, Bone.Rotation, Color);
                break;
        }

        // Draw bone name
        DrawDebugString(World, WorldPosition, Bone.BoneName, nullptr, FColor::White, 0.0f, false);
    }
}

void UMMDPhysicsDebugDraw::DrawConstraints()
{
    if (!PhysicsComponent)
    {
        return;
    }

    const TArray<FMMDPhysicsConstraint>& Constraints = PhysicsComponent->GetPhysicsConstraints();
    const TArray<FMMDPhysicsBone>& PhysicsBones = PhysicsComponent->GetPhysicsBones();
    UWorld* World = GetWorld();
    
    if (!World)
    {
        return;
    }

    for (const FMMDPhysicsConstraint& Constraint : Constraints)
    {
        if (!PhysicsBones.IsValidIndex(Constraint.RigidBodyA) || 
            !PhysicsBones.IsValidIndex(Constraint.RigidBodyB))
        {
            continue;
        }

        const FMMDPhysicsBone& BoneA = PhysicsBones[Constraint.RigidBodyA];
        const FMMDPhysicsBone& BoneB = PhysicsBones[Constraint.RigidBodyB];

        FVector WorldPosA = GetOwner()->GetActorTransform().TransformPosition(BoneA.CurrentPosition);
        FVector WorldPosB = GetOwner()->GetActorTransform().TransformPosition(BoneB.CurrentPosition);

        // Draw line between constrained bodies
        DrawDebugLine(World, WorldPosA, WorldPosB, FColor::Yellow, false, 0.0f, 0, LineThickness);

        // Draw constraint position
        FVector ConstraintWorldPos = GetOwner()->GetActorTransform().TransformPosition(Constraint.Position);
        DrawDebugPoint(World, ConstraintWorldPos, 5.0f, FColor::Red, false, 0.0f);
    }
}

void UMMDPhysicsDebugDraw::DrawVelocities()
{
    if (!PhysicsComponent)
    {
        return;
    }

    const TArray<FMMDPhysicsBone>& PhysicsBones = PhysicsComponent->GetPhysicsBones();
    UWorld* World = GetWorld();
    
    if (!World)
    {
        return;
    }

    for (const FMMDPhysicsBone& Bone : PhysicsBones)
    {
        if (Bone.Velocity.IsNearlyZero())
        {
            continue;
        }

        FVector WorldPosition = GetOwner()->GetActorTransform().TransformPosition(Bone.CurrentPosition);
        FVector WorldVelocity = GetOwner()->GetActorTransform().TransformVector(Bone.Velocity * VelocityScale);
        FVector EndPosition = WorldPosition + WorldVelocity;

        DrawDebugDirectionalArrow(World, WorldPosition, EndPosition, 5.0f, FColor::Cyan, false, 0.0f, 0, LineThickness);
    }
}

void UMMDPhysicsDebugDraw::DrawSphere(const FVector& Center, float Radius, const FColor& Color)
{
    UWorld* World = GetWorld();
    if (World)
    {
        DrawDebugSphere(World, Center, Radius, 12, Color, false, 0.0f, 0, LineThickness);
    }
}

void UMMDPhysicsDebugDraw::DrawBox(const FVector& Center, const FVector& Extent, const FRotator& Rotation, const FColor& Color)
{
    UWorld* World = GetWorld();
    if (World)
    {
        FTransform Transform(Rotation, Center);
        DrawDebugBox(World, Center, Extent, Rotation.Quaternion(), Color, false, 0.0f, 0, LineThickness);
    }
}

void UMMDPhysicsDebugDraw::DrawCapsule(const FVector& Center, float Radius, float HalfHeight, const FRotator& Rotation, const FColor& Color)
{
    UWorld* World = GetWorld();
    if (World)
    {
        DrawDebugCapsule(World, Center, HalfHeight, Radius, Rotation.Quaternion(), Color, false, 0.0f, 0, LineThickness);
    }
}

FColor UMMDPhysicsDebugDraw::GetColorForCollisionGroup(uint8 Group) const
{
    static const TArray<FColor> GroupColors = {
        FColor::Red,
        FColor::Green,
        FColor::Blue,
        FColor::Yellow,
        FColor::Cyan,
        FColor::Magenta,
        FColor::Orange,
        FColor::Purple,
        FColor::White,
        FColor::Black,
        FColor::Emerald,
        FColor::Silver,
        FColor::Turquoise,
        FColor(255, 128, 0),  // Orange
        FColor(128, 0, 255),  // Purple
        FColor(255, 0, 128)   // Pink
    };

    return GroupColors[Group % GroupColors.Num()];
}

FColor UMMDPhysicsDebugDraw::GetColorForPhysicsMode(EMMDPhysicsMode Mode) const
{
    switch (Mode)
    {
        case EMMDPhysicsMode::Static:
            return FColor::Red;
        case EMMDPhysicsMode::Dynamic:
            return FColor::Green;
        case EMMDPhysicsMode::BoneTracked:
            return FColor::Blue;
        default:
            return FColor::White;
    }
}
