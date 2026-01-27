#pragma once
#include "CoreMinimal.h"
#include "btBulletDynamicsCommon.h"    
#include "TPMXParser.h"
#include "AGN_MMDSkeletalControl.h"

class FMMDMotionState : public btMotionState
{
public:
    // ��ǰ�����UE����任��cm��
    FTransform UEWorldTransform;

    static constexpr float UEToBullet = 0.01f;   // cm �� m
    static constexpr float BulletToUE = 100.0f;  // m �� cm

    FMMDMotionState(const FTransform& InUETransform)
        : UEWorldTransform(InUETransform)
    {
    }

    virtual void getWorldTransform(btTransform& worldTrans) const override
    {
		FVector UEPos = UEWorldTransform.GetLocation(); // cm
		btVector3 BtPos = btVector3(UEPos.X * UEToBullet, UEPos.Z * UEToBullet, -UEPos.Y * UEToBullet); // m

		worldTrans.setOrigin(BtPos);

		FQuat UEQuat = UEWorldTransform.GetRotation();
        worldTrans.setRotation(btQuaternion(UEQuat.X, UEQuat.Z, -UEQuat.Y, UEQuat.W));
    }

    virtual void setWorldTransform(const btTransform& worldTrans) override
    {
		btVector3 BtPos = worldTrans.getOrigin(); // m
		FVector UEPos(BtPos.x() * BulletToUE, -BtPos.z() * BulletToUE, BtPos.y() * BulletToUE); // cm
		UEWorldTransform.SetLocation(UEPos);
        
        btQuaternion BulletQuat = worldTrans.getRotation();
        FQuat UEQuat(BulletQuat.x(), -BulletQuat.z(), BulletQuat.y(), BulletQuat.w());
        UEQuat.Normalize();
        UEWorldTransform.SetRotation(UEQuat);
    }
};
struct BulletMMDRigidRuntime
{
    btCollisionShape* Shape = nullptr;
    btRigidBody* Body = nullptr;
    FMMDMotionState* MotionState = nullptr;
    int RelatedBoneIndex = -1;
    btTransform ShapeOffset;
    int32 PhysicsMode = 0; // 0=Kinematic, 1=Dynamic, 2=BoneTracked
    int CollisionGroup = -1;
    int CollisionMask = -1;
};

class FMMDPhysicsSimulator
{
public:
    FMMDPhysicsSimulator() = default;
    ~FMMDPhysicsSimulator() { }

	bool InitializeFromPMX(const TArray<FMMDPhysicsRigidBodyData>& SaveRigid, const TArray<FMMDPhysicsJointData>& SaveJoint,
        USkeletalMeshComponent* InSkelComp);
  /*  void GameThreadTick(float DeltaSeconds);*/

    void InitializeBulletWorld();
    void InitializeRigidBody(const TArray<FMMDPhysicsRigidBodyData>& SaveRigid);
    void InitializeJoints(const TArray<FMMDPhysicsJointData>& SaveJoint);
    void StepSimulationMMD(float DeltaSeconds);

    // MMD Tick ����
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

    //void Shutdown();
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
    //TArray<BulletMMDJointsRuntime>
    // PMX->UE bone index global offset (handles extra Root added by mesh builder).
    int32 BoneIndexOffset = 0;
	USkeletalMeshComponent* OwnerSkelComp = nullptr;
};