#include "MMDPhysicsSimulator.h"
#include "TPMXParser.h"
#include <btBulletDynamicsCommon.h>
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimTypes.h"

// Internal config (previously console vars). Adjust defaults here instead of console commands.
struct FMMDPhysConfig
{
    bool  bDebug = true;
    float DebugLife = 0.0f;
    int   DebugEveryN = 1;
    bool  bDebugSimple = true;
    int   DebugMax = 512;
    bool  bLogMap = true;
    bool  bNoWriteback = false;
    bool  bGuessMap = true;
    bool  bUseWhitelist = false; // 原 true
    bool  bBlockStructural = false; // 原 true
    int   WritebackMode = 1;
    float MaxPosDelta = 50.f;
    float MaxRotAngle = 120.f;
    bool  bLogDiscard = false;
    bool  bSafeMode = true;
    int   MinDepth = 25;
    bool  bAllowTrans = false;
    bool  bHairOnly = false; // 原 true
    bool  bLogPre = true;
    float FollowWarn = 3.0f;
    bool  bLogWB = false;
    int   IndexOffset = 1;
};
static FMMDPhysConfig GPhysCfg;

void FMMDPhysicsSimulator::SetDebugEnabled(bool bEnable){ GPhysCfg.bDebug = bEnable; }

#pragma region  工具函数
static constexpr float MMD_SCALE = 0.08f; // PMX->真实米缩放 (1 PMX unit -> 0.08 m)
static constexpr float UE_CM_PER_M = 100.f;
static constexpr float UNIT_SCALE = MMD_SCALE * UE_CM_PER_M;
static FORCEINLINE FTransform BulletToUE(const btTransform& Bt, float UnitScale = UNIT_SCALE)
{
    const btVector3 P = Bt.getOrigin();
    // Bullet: (Xb, Yb, Zb)  -> UE: (Xu, Yu, Zu)
    // 我们用映射： Bullet(x, y, z) -> UE( x, -z, y ) * UnitScale (cm)
    FVector UEPos(P.x() * UnitScale, -P.z() * UnitScale, P.y() * UnitScale);

    const btQuaternion Q = Bt.getRotation();
    // 对应的四元数分量映射（axes map X->X, Y->Z, Z->-Y）
    FQuat UEQuat(Q.x(), -Q.z(), Q.y(), Q.w());
    if (!UEQuat.IsNormalized()) UEQuat.Normalize();

    return FTransform(UEQuat, UEPos, FVector(1.f));
}
static FORCEINLINE btTransform UEToBullet(const FTransform& UE, float UnitScale = UNIT_SCALE)
{
    const FVector P = UE.GetLocation();
    // 逆变换： UE(X, Y, Z) -> Bullet( X, Z, -Y ) / UnitScale (because Bullet units = PMX units)
    btVector3 BtPos(P.X / UnitScale, P.Z / UnitScale, -P.Y / UnitScale);

    const FQuat Q = UE.GetRotation();
    btQuaternion BtQuat(Q.X, Q.Z, -Q.Y, Q.W);

    btTransform Out;
    Out.setOrigin(BtPos);
    Out.setRotation(BtQuat);
    return Out;
}
static FTransform PMXDataToUETransform(const FVector& Position, const FVector& Rotation)
{
    // PMX Position → UE
    FVector UEPos = FVector(
        Position.X,
        -Position.Z,
        Position.Y
    )*UNIT_SCALE;

    FRotator Rot(
        Rotation.X,
        Rotation.Z,
        -Rotation.Y
    );

    return FTransform(Rot.Quaternion(), UEPos);
}
btVector3 PMXToBulletPos(const FVector& PMXPos)
{
    return btVector3(
        PMXPos.X * MMD_SCALE,
        PMXPos.Z * MMD_SCALE,
        -PMXPos.Y * MMD_SCALE
    );
}
static FTransform PMXToUETransform(const FVector& P, const FVector& RotRad)
{
    FVector PosUE(
        P.X * MMD_SCALE,
        -P.Z * MMD_SCALE,
        P.Y * MMD_SCALE
    );

    FRotator Rot(
        RotRad.X,
        RotRad.Z,
        -RotRad.Y
    );

    return FTransform(Rot.Quaternion(), PosUE);
}
static btCollisionShape* CreateCollisionShape(const FPMXRigid& Rigid)
{
    btCollisionShape* Shape = nullptr;
    switch (Rigid.ShapeType)
    {
    case 0: // Sphere
    {
        const float Radius = Rigid.Size.X* MMD_SCALE;
        Shape = new btSphereShape(Radius);
        break;
    }

    case 1: // Box
    {
        // PMX Size = full width/height/depth
        // Bullet box expects half-extents !
        const btVector3 HalfExtents(
            (Rigid.Size.X * 0.5f* MMD_SCALE),
            (Rigid.Size.Y * 0.5f* MMD_SCALE),
            (Rigid.Size.Z * 0.5f* MMD_SCALE)
        );

        Shape = new btBoxShape(HalfExtents);
        break;
    }

    case 2: // Capsule
    {
        const float Radius = Rigid.Size.X* MMD_SCALE;
        const float Height = Rigid.Size.Y* MMD_SCALE;

        // 在 MMD/Bullet 中：胶囊默认沿 Y 轴
        // 在 UE 中：胶囊沿 Z 轴，但这里我们是创建 Bullet 碰撞体，使用 Bullet 规范即可。

        Shape = new btCapsuleShape(Radius, Height);
        break;
    }

    default:
        // 如果 PMX 数据非法，给个默认球体避免崩溃
        UE_LOG(LogTemp, Error, TEXT("CreateCollisionShape: Unknown shape type %d, using default sphere"), Rigid.ShapeType);
        Shape = new btSphereShape(1.0f);
        break;
    }

    return Shape;
}
#pragma endregion

bool FMMDPhysicsSimulator::InitializeFromPMX(const FPMXDatas& PMXData, USkeletalMeshComponent* InSkelComp)
{
    if(!InSkelComp){ UE_LOG(LogTemp, Error, TEXT("InitializeFromPMX failed: SkeletalMeshComponent null")); return false; }
    if(bInitialized){ UE_LOG(LogTemp, Warning, TEXT("InitializeFromPMX skipped: already initialized")); return true; }
    OwnerSkelComp=InSkelComp;
    InitializeBulletWorld(); InitializeRigidBody(PMXData); InitializeJoints(PMXData); bInitialized=true; return true;
}

void FMMDPhysicsSimulator::InitializeBulletWorld()
{
    CollisionConfiguration = new btDefaultCollisionConfiguration();
    Dispatcher = new btCollisionDispatcher(CollisionConfiguration);
    Broadphase = new btDbvtBroadphase();
    Solver = new btSequentialImpulseConstraintSolver();

    DynamicsWorld = new btDiscreteDynamicsWorld(
        Dispatcher,
        Broadphase,
        Solver,
        CollisionConfiguration
    );

    DynamicsWorld->setGravity(btVector3(0.f, -9.8f, 0.f));
}
void FMMDPhysicsSimulator::InitializeRigidBody(const FPMXDatas& PMXData)
{
    if(!DynamicsWorld) return;
    if (PMXData.ModelRigids.Num() <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("InitializeRigidBody: No rigid bodies in PMX data, skipping initialization"));
        return;
    }

    for(const FPMXRigid& Rigid: PMXData.ModelRigids)
    {
        BulletRigidBody NewRigidBody;
        NewRigidBody.Shape = CreateCollisionShape(Rigid);
		NewRigidBody.RelatedBoneIndex = Rigid.RelatedBoneIndex;
        //计算刚体偏移骨骼位置
        FTransform BoneWS=OwnerSkelComp->GetBoneTransform(Rigid.RelatedBoneIndex+1)*OwnerSkelComp->GetComponentTransform();
        const FVector Rot = Rigid.Rotation; // 弧度
        FQuat Qz = FQuat(FVector::UpVector, Rot.Z);
        FQuat Qx = FQuat(FVector::RightVector, Rot.X);
        FQuat Qy = FQuat(FVector::ForwardVector, Rot.Y);
        FQuat ShapeRot = Qy * Qx * Qz;
        FVector ShapePos = FVector(
            Rigid.Position.X,
            -Rigid.Position.Z,
            Rigid.Position.Y
		) * 8.0f;
		FTransform ShapeOffset = FTransform(ShapeRot, ShapePos)* BoneWS;
		btTransform boneBT = UEToBullet(ShapeOffset, UE_CM_PER_M);
        NewRigidBody.ShapeOffset = boneBT;
		FMMDMotionState* MotionState = new FMMDMotionState(ShapeOffset);


        
        //FMMDMotionState* Motion = new FMMDMotionState(UEInit);

        //NewRigidBody.Shape=CreateCollisionShape(Rigid);
        //NewRigidBody.MotionState = new  FMMDMotionState(UEBoneTransform, UNIT_SCALE);

        // 初始模式
        NewRigidBody.PhysicsMode = Rigid.PhysicsMode;

        btVector3 Inertia(0,0,0);

        btScalar Mass = btScalar(Rigid.Mass);
        switch (Rigid.PhysicsMode)
        {
        case 0:
            Mass = 0.f; // Kinematic 刚体质量设为0
            {
                btRigidBody::btRigidBodyConstructionInfo CI(Mass, MotionState, NewRigidBody.Shape, Inertia);
                CI.m_friction = (btScalar)Rigid.Friction;
                CI.m_restitution = (btScalar)Rigid.Restitution;
                CI.m_linearDamping = (btScalar)FMath::Clamp(Rigid.LinearDamping, 0.f, 0.99f);
                CI.m_angularDamping = (btScalar)FMath::Clamp(Rigid.AngularDamping, 0.f, 0.99f);
                NewRigidBody.Body = new btRigidBody(CI);
                NewRigidBody.Body->setCollisionFlags(NewRigidBody.Body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);
            }
            break;

        case 1:
            Mass = Rigid.Mass;
            NewRigidBody.Shape->calculateLocalInertia(Mass, Inertia);
            {
                btRigidBody::btRigidBodyConstructionInfo CI(Mass, MotionState, NewRigidBody.Shape, Inertia);
                CI.m_friction = (btScalar)Rigid.Friction;
                CI.m_restitution = (btScalar)Rigid.Restitution;
                CI.m_linearDamping = (btScalar)FMath::Clamp(Rigid.LinearDamping, 0.f, 0.99f);
                CI.m_angularDamping = (btScalar)FMath::Clamp(Rigid.AngularDamping, 0.f, 0.99f);
                NewRigidBody.Body = new btRigidBody(CI);
                NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);
            }
            break;

        case 2:
            Mass = Rigid.Mass;
            NewRigidBody.Shape->calculateLocalInertia(Mass, Inertia);
            {
                btRigidBody::btRigidBodyConstructionInfo CI(Mass, MotionState, NewRigidBody.Shape, Inertia);
                CI.m_friction = (btScalar)Rigid.Friction;
                CI.m_restitution = (btScalar)Rigid.Restitution;
                CI.m_linearDamping = (btScalar)FMath::Clamp(Rigid.LinearDamping, 0.f, 0.99f);
                CI.m_angularDamping = (btScalar)FMath::Clamp(Rigid.AngularDamping, 0.f, 0.99f);
                NewRigidBody.Body = new btRigidBody(CI);
                NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);
            }
            break;

        default:
            UE_LOG(LogTemp, Error, TEXT("InitializeRigidBody: Unknown PhysicsMode %d, defaulting to Physics (1)"), Rigid.PhysicsMode);
            break;
        }
        // 碰撞组与掩码
        const int BulletGroup = 1 << Rigid.Group;
        const int BulletMask = Rigid.CollisionMask;
        NewRigidBody.CollisionGroup = BulletGroup;
        NewRigidBody.CollisionMask  = BulletMask;

        // 添加到世界（所有模式都添加，方便统一接触检测）
        DynamicsWorld->addRigidBody(NewRigidBody.Body, NewRigidBody.CollisionGroup, NewRigidBody.CollisionMask);

        // 起始 Bullet 世界 -> UE 世界
        
        
        BulletRigidBodies.Add(NewRigidBody);
    }
}

void FMMDPhysicsSimulator::InitializeJoints(const FPMXDatas& PMXData)
{
    if (PMXData.ModelRigids.Num() <= 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("InitializeRigidBody: No rigid bodies in PMX data, skipping initialization"));
        return;
    }
    for (const FPMXJoint& Joint : PMXData.ModelJoints) {

        if (Joint.RigidA < 0 || Joint.RigidA >= BulletRigidBodies.Num() ||
            Joint.RigidB < 0 || Joint.RigidB >= BulletRigidBodies.Num()) {
            UE_LOG(LogTemp, Error, TEXT("InitializeJoints: Rigid body index out of bounds (RigidA=%d, RigidB=%d, Max=%d), skipping"),
                Joint.RigidA, Joint.RigidB, BulletRigidBodies.Num());
            continue;
        }

        auto* BodyA = BulletRigidBodies[Joint.RigidA].Body;
        auto* BodyB = BulletRigidBodies[Joint.RigidB].Body;
        if (!BodyA || !BodyB) {
            UE_LOG(LogTemp, Error, TEXT("InitializeJoints: Invalid rigid body pointer for joint (RigidA=%d, RigidB=%d), skipping"),
                Joint.RigidA, Joint.RigidB);
            continue;
        }
        btVector3 JPos = btVector3(Joint.Position.X, Joint.Position.Y, -Joint.Position.Z) * MMD_SCALE;
        btQuaternion JRot;
        JRot.setEuler(Joint.Rotation.X, Joint.Rotation.Y, Joint.Rotation.Z);
        JRot.setX(-JRot.x());
        JRot.setY(-JRot.y()); 
        btTransform JointTr(JRot, JPos);
        btTransform FrameA = BodyA->getWorldTransform().inverse() * JointTr;
        btTransform FrameB = BodyB->getWorldTransform().inverse() * JointTr;

        btGeneric6DofSpringConstraint* Constraint = new btGeneric6DofSpringConstraint(*BodyA, *BodyB, FrameA, FrameB, true);
        Constraint->setLinearLowerLimit(btVector3(
            Joint.LimitPosLower.X * MMD_SCALE,
            Joint.LimitPosLower.Y * MMD_SCALE,
            -Joint.LimitPosLower.Z * MMD_SCALE
        ));
        Constraint->setLinearUpperLimit(btVector3(
            Joint.LimitPosUpper.X * MMD_SCALE,
            Joint.LimitPosUpper.Y * MMD_SCALE,
            -Joint.LimitPosUpper.Z * MMD_SCALE
        ));
        Constraint->setAngularLowerLimit(btVector3(Joint.LimitRotLower.X, Joint.LimitRotLower.Y, Joint.LimitRotLower.Z));
        Constraint->setAngularUpperLimit(btVector3(Joint.LimitRotUpper.X, Joint.LimitRotUpper.Y, Joint.LimitRotUpper.Z));
        for (int i = 0; i < 3; i++) {
            if (Joint.SpringPos[i] > 0) {
                Constraint->enableSpring(i, true);
                Constraint->setStiffness(i, Joint.SpringPos[i]);
            }
            if (Joint.SpringRot[i] > 0) {
                int rotIndex = i + 3;
                Constraint->enableSpring(rotIndex, true);
                Constraint->setStiffness(rotIndex, Joint.SpringRot[i]);
            }

        }
		DynamicsWorld->addConstraint(Constraint, true);
		//BulletJoints.Add(Constraint);

    }
}

void FMMDPhysicsSimulator::PreSyncKinematicFromBones(FComponentSpacePoseContext& InPose, TArray<FBoneTransform>& OutBoneTransforms)
{
	const FBoneContainer& BoneContainer = InPose.AnimInstanceProxy->GetRequiredBones();
    if(!DynamicsWorld) return;

    for (BulletRigidBody& RB : BulletRigidBodies) {
        if (!RB.Body || !RB.Body->getMotionState()) continue;
        if (RB.PhysicsMode != 0) continue;
        const int32 BoneIdx = RB.RelatedBoneIndex+1;
        if (RB.RelatedBoneIndex < 0) continue;
        
        FCompactPoseBoneIndex CPIndex = BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIdx));
        if (!CPIndex.IsValid())continue;

        const FTransform BoneCS = InPose.Pose.GetComponentSpaceTransform(CPIndex);
        const FTransform C2W = InPose.AnimInstanceProxy->GetComponentTransform();
        //ue世界的骨骼坐标
        const FTransform BoneWorld = BoneCS * C2W;
        const btTransform BoneWorldBT = UEToBullet(BoneWorld);
        const btTransform RigidWorldBT = BoneWorldBT * RB.ShapeOffset;

        RB.Body->setWorldTransform(RigidWorldBT);
        RB.Body->getMotionState()->setWorldTransform(RigidWorldBT);
    }
}

void FMMDPhysicsSimulator::PostSyncBonesFromPhysics(FComponentSpacePoseContext& InPose, TArray<FBoneTransform>& OutBoneTransforms)
{
    if(!DynamicsWorld) return;

    const FBoneContainer& BoneContainer = InPose.AnimInstanceProxy->GetRequiredBones();

	const float MaxRotDeg = FMath::Clamp(GPhysCfg.MaxRotAngle, 10.f, 180.f);

    for (const BulletRigidBody& RB : BulletRigidBodies)
    {
		if (!RB.Body || !RB.Body->getMotionState()) continue;

		if (RB.PhysicsMode == 0) continue; // Kinematic 刚体跳过

		const int32 BoneIdx = RB.RelatedBoneIndex + 1;
        if (BoneIdx < 0) continue;

        const FCompactPoseBoneIndex CompactIdx=BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(BoneIdx));
        if (!CompactIdx.IsValid()) continue;

		const btTransform RigidWorldBT = RB.Body->getWorldTransform();

        btTransform BoneWorldBT = RigidWorldBT;
        if (RB.ShapeOffset.getBasis().determinant() != 0) // 简单防御
        {
            btTransform InvOffset = RB.ShapeOffset.inverse();
            BoneWorldBT = RigidWorldBT * InvOffset;
        }
		const FTransform BoneWorldUE = BulletToUE(BoneWorldBT);

		const FTransform C2W = InPose.AnimInstanceProxy->GetComponentTransform();
        FTransform TargetCS=BoneWorldUE * C2W.Inverse();

		OutBoneTransforms.Add(FBoneTransform(CompactIdx, TargetCS));
    }
}

void FMMDPhysicsSimulator::TickMMDPhysics(FComponentSpacePoseContext& InPose, TArray<FBoneTransform>& OutBoneTransforms)
{
    if (!DynamicsWorld) return;

    PreSyncKinematicFromBones(InPose, OutBoneTransforms);

    const float DeltaTime = InPose.AnimInstanceProxy->GetDeltaSeconds();
    StepSimulationMMD(DeltaTime);

    PostSyncBonesFromPhysics(InPose, OutBoneTransforms);
}

//void FMMDPhysicsSimulator::DebugDraw()
//{
//    if (!GPhysCfg.bDebug) return; USkeletalMeshComponent* SkelComp = OwnerSkelComp.Get(); if (!SkelComp) return; UWorld* World = SkelComp->GetWorld(); if (!World) return; static uint32 FrameCounter = 0; ++FrameCounter; const int32 EveryN = FMath::Max(1, GPhysCfg.DebugEveryN); if ((FrameCounter % (uint32)EveryN) != 0) return; const float LifeTime = FMath::Max(0.f, GPhysCfg.DebugLife); const bool bPersistent = LifeTime > 0.f; const float Thickness = 1.5f; const bool bSimple = GPhysCfg.bDebugSimple; const int32 MaxCount = FMath::Max(1, GPhysCfg.DebugMax); const FTransform C2W = SkelComp->GetComponentTransform(); int32 Drawn = 0; for (const BulletRigidBody& RB : BulletRigidBodies){ if (!RB.Body) continue; if (Drawn >= MaxCount) break; ++Drawn; const btTransform WorldB = RB.Body->getWorldTransform(); const FTransform WorldUE = BulletToUE_WorldTransform_YUp(WorldB, UnitScale); const FVector Center = WorldUE.GetLocation(); const FQuat Rot = WorldUE.GetRotation(); const bool bKinematic = (RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; const bool bDynamic = RB.Body->getInvMass() > 0; const FColor Color = bKinematic ? FColor(255,128,0) : (bDynamic ? FColor(0,255,255) : FColor(200,200,200)); if (bSimple){ DrawDebugSphere(World, Center, 2.5f*UnitScale*0.1f, 8, Color, bPersistent, LifeTime, 0, Thickness); } else { DrawDebugCoordinateSystem(World, Center, Rot.Rotator(), 5.f*UnitScale*0.1f, bPersistent, LifeTime, 0, Thickness); DrawDebugSphere(World, Center, 2.0f*UnitScale*0.1f, 8, Color, bPersistent, LifeTime, 0, Thickness); } }
//    Drawn = 0; for (btTypedConstraint* TC : BulletJoints){ if(!TC) continue; if (Drawn >= MaxCount) break; ++Drawn; auto* C = static_cast<btGeneric6DofSpring2Constraint*>(TC); const btTransform AWorld = C->getRigidBodyA().getWorldTransform(); const btTransform BWorld = C->getRigidBodyB().getWorldTransform(); const btTransform FA = C->getFrameOffsetA(); const btTransform FB = C->getFrameOffsetB(); const FTransform AnchorA_UE = BulletToUE_WorldTransform_YUp(AWorld*FA,UnitScale); const FTransform AnchorB_UE = BulletToUE_WorldTransform_YUp(BWorld*FB,UnitScale); DrawDebugLine(World, AnchorA_UE.GetLocation(), AnchorB_UE.GetLocation(), FColor::Green, bPersistent, LifeTime, 0, Thickness); }
//}

//void FMMDPhysicsSimulator::CaptureSnapshot(FMMDPhysicsSimSnapshot& OutSnapshot) const
//{
//    OutSnapshot.UnitScale=UnitScale; OutSnapshot.MaxSubSteps=MaxSubSteps; OutSnapshot.FixedTimeStep=FixedTimeStep; OutSnapshot.Bodies.Reset(BulletRigidBodies.Num()); for(int32 i=0;i<BulletRigidBodies.Num();++i){ const BulletRigidBody& RB=BulletRigidBodies[i]; if(!RB.Body) continue; FMMDPhysicsBodyState S; S.BodyIndex=i; const btTransform WorldB=RB.Body->getWorldTransform(); S.WorldUE=BulletToUE_WorldTransform_YUp(WorldB,UnitScale); const btVector3 vB=RB.Body->getLinearVelocity(); const btVector3 wB=RB.Body->getAngularVelocity(); S.LinearVelocityUE=BulletToUE_Vector_YUp(vB,UnitScale); S.AngularVelocityUE = BulletToUE_AngVel_YUp(wB); S.bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; S.bSleeping=RB.Body->getActivationState()==ISLAND_SLEEPING; OutSnapshot.Bodies.Add(MoveTemp(S)); }
//}

//bool FMMDPhysicsSimulator::ApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot, bool bRespectKinematic)
//{
//    if(!DynamicsWorld) return false;
//    if(InSnapshot.Bodies.Num()<=0) return false;
//    const int32 N=FMath::Min(BulletRigidBodies.Num(),InSnapshot.Bodies.Num());
//    for(int32 i=0;i<N;++i){
//        const FMMDPhysicsBodyState& S=InSnapshot.Bodies[i];
//        if(!BulletRigidBodies.IsValidIndex(S.BodyIndex)) continue;
//        BulletRigidBody& RB=BulletRigidBodies[S.BodyIndex];
//        if(!RB.Body) continue;
//        const bool bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0;
//        if(bRespectKinematic && bKinematic) continue;
//
//        const btTransform TargetB=UEToBullet_Transform_YUp(S.WorldUE,UnitScale);
//        RB.Body->setWorldTransform(TargetB);
//        if(auto* MS=RB.Body->getMotionState()) MS->setWorldTransform(TargetB);
//
//        const btVector3 vB=UEToBullet_Vector_YUp(S.LinearVelocityUE,UnitScale);
//        const btVector3 wB=UEToBullet_AngVel_YUp(S.AngularVelocityUE); // 修正：去掉 UnitScale
//        RB.Body->setLinearVelocity(vB);
//        RB.Body->setAngularVelocity(wB);
//
//        if(S.bSleeping){ RB.Body->setActivationState(ISLAND_SLEEPING);} else { RB.Body->activate(true);}
//    }
//    return true;
//}

void FMMDPhysicsSimulator::Shutdown()
{
    if(DynamicsWorld){ for(btGeneric6DofSpring2Constraint* C: BulletJoints){ if(C){ DynamicsWorld->removeConstraint(C); delete C; }} }
    BulletJoints.Empty(); if(DynamicsWorld){ for(BulletRigidBody& RB: BulletRigidBodies){ if(RB.Body){ DynamicsWorld->removeRigidBody(RB.Body); delete RB.Body; } if(RB.MotionState){ delete RB.MotionState; } if(RB.Shape){ delete RB.Shape; } } }
    BulletRigidBodies.Empty(); if(DynamicsWorld){ delete DynamicsWorld; DynamicsWorld=nullptr; } if(Solver){ delete Solver; Solver=nullptr; } if(Broadphase){ delete Broadphase; Broadphase=nullptr; } if(Dispatcher){ delete Dispatcher; Dispatcher=nullptr; } if(CollisionConfiguration){ delete CollisionConfiguration; CollisionConfiguration=nullptr; } bInitialized=false; bFirstSyncDone=false;
}

void FMMDPhysicsSimulator::StepSimulationMMD(float DeltaSeconds)
{
    if (!DynamicsWorld) return; DynamicsWorld->stepSimulation(DeltaSeconds, MaxSubSteps, FixedTimeStep);
}////////