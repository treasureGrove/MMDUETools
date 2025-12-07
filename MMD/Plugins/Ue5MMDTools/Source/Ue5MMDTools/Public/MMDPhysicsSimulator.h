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

    btTransform ShapeOffset =btTransform::getIdentity();

    int32 DebugID = -1;
};

class FMMDMotionState : public btMotionState
{
public:
    // 当前刚体的UE世界变换（cm）
    FTransform UEWorldTransform;

    // UE(cm) 与 Bullet(m) 转换比例： 1cm = 0.01m
    static constexpr float UEToBullet = 0.01f;   // cm → m
    static constexpr float BulletToUE = 100.0f;  // m → cm

    FMMDMotionState(const FTransform& InUETransform)
        : UEWorldTransform(InUETransform)
    {
    }

    // Bullet → UE（物理更新 → 动画用）
    virtual void getWorldTransform(btTransform& worldTrans) const override
    {
        FVector UEPos = UEWorldTransform.GetLocation();      // cm
        FVector BulletPos = UEPos * UEToBullet;              // -> m

        FQuat UEQuat = UEWorldTransform.GetRotation();       // UE左手
        btQuaternion BulletQuat(UEQuat.X, UEQuat.Y, UEQuat.Z, UEQuat.W);

        worldTrans.setOrigin(btVector3(BulletPos.X, BulletPos.Y, BulletPos.Z));
        worldTrans.setRotation(BulletQuat);
    }

    // Bullet → UE（物理驱动骨骼时）
    virtual void setWorldTransform(const btTransform& worldTrans) override
    {
        btVector3 P = worldTrans.getOrigin();  // m
        btQuaternion Q = worldTrans.getRotation();

        FVector UEPos = FVector(P.x(), P.y(), P.z()) * BulletToUE; // m->cm
        FQuat UEQuat(Q.x(), Q.y(), Q.z(), Q.w());

        UEWorldTransform.SetLocation(UEPos);
        UEWorldTransform.SetRotation(UEQuat);
    }
};

class FMMDPhysicsSimulator
{
public:
    FMMDPhysicsSimulator() = default;
    ~FMMDPhysicsSimulator() { Shutdown(); }

    bool InitializeFromPMX(const FPMXDatas& PMXData,
        USkeletalMeshComponent* InSkelComp);
  /*  void GameThreadTick(float DeltaSeconds);*/

    void InitializeBulletWorld();
    void InitializeRigidBody(const FPMXDatas& PMXData);
    void InitializeJoints(const FPMXDatas& PMXData);
    void StepSimulationMMD(float DeltaSeconds);

    // MMD Tick 流程
    void PreSyncKinematicFromBones(FComponentSpacePoseContext& InPose, TArray<FBoneTransform>& OutBoneTransforms);
    void PostSyncBonesFromPhysics(FComponentSpacePoseContext& InPose, TArray<FBoneTransform>& OutBoneTransforms);
    void TickMMDPhysics(FComponentSpacePoseContext& InPose, TArray<FBoneTransform>& OutBoneTransforms);

    //void CaptureSnapshot(FMMDPhysicsSimSnapshot& OutSnapshot) const;
    //bool ApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot, bool bRespectKinematic = true);

    //bool ForceApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot)
    //{
    //    bFirstSyncDone = true;
    //    return ApplySnapshot(InSnapshot, /*bRespectKinematic*/false);
    //}

    //void DebugDraw();
    void SetDebugEnabled(bool bEnable);

    //USkeletalMeshComponent* GetOwnerSkelComp() const { return OwnerSkelComp.Get(); }
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
    
    bool bInitialized = false;
    static constexpr float UnitScale = 8.f;          // UE cm per PMX unit (mesh builder uses 8)
    static constexpr int32 MaxSubSteps = 1;           // baseline
    static constexpr float FixedTimeStep = 1.f / 60.f;// baseline

    bool bFirstSyncDone = false;

    // PMX->UE bone index global offset (handles extra Root added by mesh builder).
    int32 BoneIndexOffset = 0;
	USkeletalMeshComponent* OwnerSkelComp = nullptr;
};