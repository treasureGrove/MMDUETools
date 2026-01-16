#pragma once
#include "CoreMinimal.h"
#include "btBulletDynamicsCommon.h"    
#include "TPMXParser.h"
#include "AGN_MMDSkeletalControl.h"



struct BulletMMDRigidRuntime
{
	btCollisionShape* Shape = nullptr;
	btRigidBody* Body = nullptr;
    int RelatedBoneIndex = -1;
    btTransform ShapeOffset;
	int32 PhysicsMode = 0; // 0=Kinematic, 1=Dynamic, 2=BoneTracked
	int CollisionGroup = -1;
	int CollisionMask = -1;

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

	bool InitializeFromPMX(const TArray<FMMDPhysicsRigidBodyData>& SaveRigid, const TArray<FMMDPhysicsJointData>& SaveJoint,
        USkeletalMeshComponent* InSkelComp);
  /*  void GameThreadTick(float DeltaSeconds);*/

    void InitializeBulletWorld();
    void InitializeRigidBody(const TArray<FMMDPhysicsRigidBodyData>& SaveRigid);
    void InitializeJoints(const TArray<FMMDPhysicsJointData>& SaveJoint);
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

    TArray<btGeneric6DofSpring2Constraint*> BulletJoints;
    
    bool bInitialized = false;
    static constexpr float UnitScale = 8.f;          // UE cm per PMX unit (mesh builder uses 8)
    static constexpr int32 MaxSubSteps = 1;           // baseline
    static constexpr float FixedTimeStep = 1.f / 60.f;// baseline

    bool bFirstSyncDone = false;

	TArray<BulletMMDRigidRuntime> BulletRigidsRuntime;
    // PMX->UE bone index global offset (handles extra Root added by mesh builder).
    int32 BoneIndexOffset = 0;
	USkeletalMeshComponent* OwnerSkelComp = nullptr;
};