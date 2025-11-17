#pragma once
#include "CoreMinimal.h"
#include "btBulletDynamicsCommon.h"    
#include "TPMXParser.h"
#include "MMDPhysicsSimulator.generated.h"

// Snapshot of one rigid body state for persistence
USTRUCT(BlueprintType)
struct FMMDPhysicsBodyState
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 BodyIndex = INDEX_NONE; // stable index matching PMX order

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FTransform WorldUE = FTransform::Identity; // UE-space world transform

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector LinearVelocityUE = FVector::ZeroVector; // UE-space linear velocity

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    FVector AngularVelocityUE = FVector::ZeroVector; // UE-space angular velocity (rad/s)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bKinematic = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    bool bSleeping = false;
};

USTRUCT(BlueprintType)
struct FMMDPhysicsSimSnapshot
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float UnitScale = 10.f; // Bullet unit = 0.1m, UE cm -> /10

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 MaxSubSteps = 5; // baseline

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float FixedTimeStep = 1.f / 60.f; // baseline

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FMMDPhysicsBodyState> Bodies;
};

struct BulletRigidBody
{
    btCollisionShape* Shape=nullptr;
    btMotionState* MotionState=nullptr;
    btRigidBody* Body=nullptr;

    short CollisionGroup = 0;
    short CollisionMask = -1;

    FTransform UEWorldTransform;
    btTransform BulletWorldTransform;

    FTransform PrevUEWorldTransform;
    btTransform PrevBulletWorldTransform;

    int32 RelatedBoneIndex = -1; // UE bone index

    // PMX PhysicsMode: 0=Static(BoneFollow), 1=Dynamic, 2=Dynamic+BoneTracking
    uint8 PhysicsMode = 0;

    FTransform BoneToRigid = FTransform::Identity;
    FTransform RigidToBone = FTransform::Identity;

    // Metadata from PMX for safer decisions/logging
    int32 PMXBoneIndex = -1;           // PMX bone index bound to this rigid
    FString PMXBoneNameJP;             // PMX bone name (JP)
    FString PMXBoneNameEN;             // PMX bone name (EN)
    bool bTransformAfterPhysics = false; // whether this bone should be written back from physics

    int32 DebugID = -1;
};


class FMMDPhysicsSimulator
{
public:
    FMMDPhysicsSimulator() = default;
    ~FMMDPhysicsSimulator() { Shutdown(); }

    bool InitializeFromPMX(const PMXDatas& PMXData,
        USkeletalMeshComponent* InSkelComp,
        float InUnitScale = 8.f,         // match mesh builder scaling (UE cm per PMX unit)
        int32 InMaxSubSteps = 5,
        float InFixedTimeStep = 1.f / 60.f);
    void GameThreadTick(float DeltaSeconds);

    void InitializeBulletWorld();
    void InitializeRigidBody(const PMXDatas& PMXData);
    void InitializeJoints(const PMXDatas& PMXData);
    void StepSimulationMMD(float DeltaSeconds);

    // MMD Tick Á÷³Ì
    void PreSyncKinematicFromBones(const TArray<FTransform>& BoneWorldUE);
    void PostSyncBonesFromPhysics(TArray<FTransform>& InOutBoneWorldUE);
    void TickMMDPhysics(float DeltaSeconds, TArray<FTransform>& InOutBoneWorldUE);

    void CaptureSnapshot(FMMDPhysicsSimSnapshot& OutSnapshot) const;
    bool ApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot, bool bRespectKinematic = true);

    bool ForceApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot)
    {
        bFirstSyncDone = true;
        return ApplySnapshot(InSnapshot, /*bRespectKinematic*/false);
    }

    void DebugDraw();

    USkeletalMeshComponent* GetOwnerSkelComp() const { return OwnerSkelComp.Get(); }
    bool IsInitialized() const { return bInitialized; }

    void Shutdown();
private:
    btDefaultCollisionConfiguration* CollisionConfiguration = nullptr;
    btCollisionDispatcher* Dispatcher = nullptr;
    btDbvtBroadphase* Broadphase = nullptr;
    btSequentialImpulseConstraintSolver* Solver = nullptr;
    btDiscreteDynamicsWorld* DynamicsWorld = nullptr;

    TArray<BulletRigidBody> BulletRigidBodies;
    TArray<btGeneric6DofSpring2Constraint*> BulletJoints;
    TWeakObjectPtr<USkeletalMeshComponent> OwnerSkelComp;
    bool bInitialized = false;
    float UnitScale = 8.f;          // UE cm per PMX unit (mesh builder uses 8)
    int32 MaxSubSteps = 5;           // baseline
    float FixedTimeStep = 1.f / 60.f;// baseline

    bool bFirstSyncDone = false;

};