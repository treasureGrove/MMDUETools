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
    float UnitScale = 8.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    int32 MaxSubSteps = 3;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    float FixedTimeStep = 1.f / 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame)
    TArray<FMMDPhysicsBodyState> Bodies;
};

struct BulletRigidBody
{
    btCollisionShape* Shape=nullptr;
    btMotionState* MotionState=nullptr;
    btRigidBody* Body=nullptr;
    //刚体物理属性

    //碰撞组和掩码
    short CollisionGroup = 0;//刚体的碰撞组，用于指定刚体所属的组
    short CollisionMask = -1;//刚体的碰撞掩码,用于指定刚体可以与哪些组发生碰撞

    FTransform UEWorldTransform;//UE坐标系下的变换
    btTransform BulletWorldTransform;//Bullet坐标系下的变换

    FTransform PrevUEWorldTransform;//上一次更新时的UE坐标系下的变换
    btTransform PrevBulletWorldTransform;//上一次更新时的Bullet坐标系下的变换

    int32 RelatedBoneIndex = -1;//关联的骨骼索引

    FTransform BoneToRigid = FTransform::Identity;//刚体相对于骨骼的偏移变换
    FTransform RigidToBone = FTransform::Identity;//骨骼相对于刚体的偏移变换

    int32 DebugID = -1;//用于调试的ID
};


class FMMDPhysicsSimulator
{
public:
    FMMDPhysicsSimulator() = default;
    ~FMMDPhysicsSimulator() { Shutdown(); }

    bool InitializeFromPMX(const PMXDatas& PMXData,
        USkeletalMeshComponent* InSkelComp,
        float InUnitScale = 8.f,
        int32 InMaxSubSteps = 3,
        float InFixedTimeStep = 1.f / 60.f);
    void GameThreadTick(float DeltaSeconds);

    void InitializeBulletWorld();
    void InitializeRigidBody(const PMXDatas PMXData);
    void InitializeJoints(const PMXDatas PMXData);
    void StepSimulationMMD(float DeltaSeconds);

    // MMD Tick 流程
    void PreSyncKinematicFromBones(const TArray<FTransform>& BoneWorldUE);
    void PostSyncBonesFromPhysics(TArray<FTransform>& InOutBoneWorldUE);
    void TickMMDPhysics(float DeltaSeconds, TArray<FTransform>& InOutBoneWorldUE);

    // Snapshot API (game thread only)
    void CaptureSnapshot(FMMDPhysicsSimSnapshot& OutSnapshot) const;
    bool ApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot, bool bRespectKinematic = true);

    // 强制应用快照并跳过首帧对齐（用于从持久化恢复）
    bool ForceApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot)
    {
        bFirstSyncDone = true; // 避免第一次 Tick 覆盖快照结果
        return ApplySnapshot(InSnapshot, /*bRespectKinematic*/false);
    }

    // Game-thread only: draw debug bodies and joints in current world
    void DebugDraw();

    USkeletalMeshComponent* GetOwnerSkelComp() const { return OwnerSkelComp.Get(); }
    bool IsInitialized() const { return bInitialized; }

    // 释放 Bullet 资源，可重复调用安全
    void Shutdown();
private:
    // Bullet 物理世界相关
    btDefaultCollisionConfiguration* CollisionConfiguration = nullptr;
    btCollisionDispatcher* Dispatcher = nullptr;
    btDbvtBroadphase* Broadphase = nullptr;
    btSequentialImpulseConstraintSolver* Solver = nullptr;
    btDiscreteDynamicsWorld* DynamicsWorld = nullptr;

    //Bullet 刚体列表
    TArray<BulletRigidBody> BulletRigidBodies;
    TArray<btGeneric6DofSpring2Constraint*> BulletJoints;
    TWeakObjectPtr<USkeletalMeshComponent> OwnerSkelComp;
    bool bInitialized = false;
    float UnitScale = 8.f;
    int32 MaxSubSteps = 3;
    float FixedTimeStep = 1.f / 60.f;

    bool bFirstSyncDone = false; // 新增：首帧骨骼对齐

};