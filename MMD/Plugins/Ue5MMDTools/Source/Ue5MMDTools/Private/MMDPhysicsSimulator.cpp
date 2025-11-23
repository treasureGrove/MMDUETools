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
static btCollisionShape* CreateCollisionShape(const PMXRigid& Rigid)
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

bool FMMDPhysicsSimulator::InitializeFromPMX(const PMXDatas& PMXData, USkeletalMeshComponent* InSkelComp)
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

//    // 3. 求解器参数（增强稳定性）
//    btContactSolverInfo& si = DynamicsWorld->getSolverInfo();
//    si.m_numIterations = 24; // 推荐提高到 20~32，比原 12 更稳（可按场景调）
//
//    // Split impulse 防止深度穿透引发爆能
//    si.m_splitImpulse = 1;
//    si.m_splitImpulsePenetrationThreshold = -0.03f; // -0.02~-0.04 间调节
//
//    // 关节误差修正系数（ERP）与次级 ERP
//    si.m_erp = 0.2f;
//    si.m_erp2 = 0.1f;
//
//    // 全局 CFM：保持 0，如链条过硬引发振铃可改 1e-5 ~ 1e-4
//    si.m_globalCfm = 0.0f;
//
//    // 4. Solver 标志（去重 + 添加 interleave）
//    int flags =
//        SOLVER_USE_WARMSTARTING
//        | SOLVER_USE_2_FRICTION_DIRECTIONS
//        | SOLVER_ENABLE_FRICTION_DIRECTION_CACHING
//        | SOLVER_INTERLEAVE_CONTACT_AND_FRICTION_CONSTRAINTS;
//#ifdef BT_USE_SSE // 视编译选项而定
//    flags |= SOLVER_SIMD;
//#endif
//    si.m_solverMode |= flags;
}

// 修正 physicsMode 语义：
// 0 = FollowBone (Kinematic，参与碰撞，质量设为0，添加到世界，保留接触响应)
// 1 = Physics (Dynamic)
// 2 = PhysicsWithBone (Hybrid：动态 + 骨骼纠正力)
void FMMDPhysicsSimulator::InitializeRigidBody(const PMXDatas& PMXData)
{
    if(!DynamicsWorld) return;


    for(const PMXRigid& Rigid: PMXData.ModelRigids)
    {
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
		FMMDMotionState* MotionState = new FMMDMotionState(ShapeOffset);


        
        //FMMDMotionState* Motion = new FMMDMotionState(UEInit);
        BulletRigidBody NewRigidBody;
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
            btRigidBody::btRigidBodyConstructionInfo CI(Mass, NewRigidBody.MotionState, NewRigidBody.Shape, Inertia);
            CI.m_friction = (btScalar)Rigid.Friction;
            CI.m_restitution = (btScalar)Rigid.Restitution;
            CI.m_linearDamping = (btScalar)FMath::Clamp(Rigid.LinearDamping, 0.f, 0.99f);
            CI.m_angularDamping = (btScalar)FMath::Clamp(Rigid.AngularDamping, 0.f, 0.99f);
            NewRigidBody.Body = new btRigidBody(CI);
            NewRigidBody.Body = new btRigidBody(CI);
            NewRigidBody.Body->setCollisionFlags(NewRigidBody.Body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
			NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);
        case 1:
			Mass = Rigid.Mass;
			NewRigidBody.Shape->calculateLocalInertia(Mass, Inertia);
			btRigidBody::btRigidBodyConstructionInfo CI(Mass, NewRigidBody.MotionState, NewRigidBody.Shape, Inertia);
            CI.m_friction = (btScalar)Rigid.Friction;
            CI.m_restitution = (btScalar)Rigid.Restitution;
            CI.m_linearDamping = (btScalar)FMath::Clamp(Rigid.LinearDamping, 0.f, 0.99f);
            CI.m_angularDamping = (btScalar)FMath::Clamp(Rigid.AngularDamping, 0.f, 0.99f);
            NewRigidBody.Body = new btRigidBody(CI);
			NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);
            break;
        case 2:
            Mass = Rigid.Mass;
			NewRigidBody.Shape->calculateLocalInertia(Mass, Inertia);
			btRigidBody::btRigidBodyConstructionInfo CI(Mass, NewRigidBody.MotionState, NewRigidBody.Shape, Inertia);
            CI.m_friction      = (btScalar)Rigid.Friction;
            CI.m_restitution   = (btScalar)Rigid.Restitution;
            CI.m_linearDamping = (btScalar)FMath::Clamp(Rigid.LinearDamping, 0.f, 0.99f);
            CI.m_angularDamping= (btScalar)FMath::Clamp(Rigid.AngularDamping, 0.f, 0.99f);
            NewRigidBody.Body = new btRigidBody(CI);
            NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);
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

void FMMDPhysicsSimulator::InitializeJoints(const PMXDatas& PMXData)
{

    for (const PMXJoint& Joint : PMXData.ModelJoints) {
        auto* BodyA = BulletRigidBodies[Joint.RigidA].Body;
        auto* BodyB = BulletRigidBodies[Joint.RigidB].Body;
        if (!BodyA || !BodyB) {
            UE_LOG(LogTemp, Error, TEXT("InitializeJoints: Invalid rigid body index for joint, skipping"));
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
    }
}

void FMMDPhysicsSimulator::PreSyncKinematicFromBones(const FCompactPose& InPose,const FBoneContainer& BoneContainer,TArray<BulletRigidBody>& RigidBodys)
{
    if(!DynamicsWorld) return;
    FCSPose<FCompactPose> CSPose;
    for (int i = 0; i < BulletRigidBodies.Num(); ++i) {
        if (BulletRigidBodies[i].PhysicsMode != 0) {
			UE_LOG(LogTemp, Warning, TEXT("PreSyncKinematicFromBones: Rigid body %d is not kinematic, skipping"), i);
            continue;
        }
        int32 BoneIdx = BulletRigidBodies[i].RelatedBoneIndex + 1;
        FCompactPoseBoneIndex CompactIndex = BoneContainer.GetCompactPoseIndexFromSkeletonIndex(BoneIdx);
        if (CompactIndex == INDEX_NONE) {
            UE_LOG(LogTemp, Error, TEXT("PreSyncKinematicFromBones: Invalid bone index %d for rigid body %d"), BoneIdx, i);
			continue;
        }




    }

}

void FMMDPhysicsSimulator::PostSyncBonesFromPhysics(TArray<FTransform>& InOutBoneWorldUE)
{
    if(!DynamicsWorld) return;
    if (GPhysCfg.bNoWriteback) return;

    USkeletalMeshComponent* SkelComp = OwnerSkelComp.Get();
    const FTransform C2W = SkelComp ? SkelComp->GetComponentTransform() : FTransform::Identity;
    const FTransform W2C = C2W.Inverse();

    // 调试期建议关闭过滤，确认写回是否生效
    const bool bHairOnly     = false;            // 原: GPhysCfg.bHairOnly
    const bool bUseWhitelist = false;            // 原: GPhysCfg.bUseWhitelist
    const bool bBlockStructural = false;         // 原: GPhysCfg.bBlockStructural

    const float MaxRotDeg   = FMath::Clamp(GPhysCfg.MaxRotAngle, 10.f, 180.f);
    const bool  bLogDiscard = GPhysCfg.bLogDiscard;

    TArray<FTransform> RefWorld;
    BuildRefWorldTransforms(SkelComp, RefWorld);

    for(const BulletRigidBody& RB: BulletRigidBodies)
    {
        if(!RB.Body) continue;

        // 修改：仅跳过 mode=0；允许 mode=1 与 mode=2 写回（让“物理带骨骼动起来”）
        if (RB.PhysicsMode == 0) continue;

        const int32 BoneIdx = RB.RelatedBoneIndex;
        if(BoneIdx<0 || !InOutBoneWorldUE.IsValidIndex(BoneIdx)) continue;
        if(!RefWorld.IsValidIndex(BoneIdx)) continue;

        if (SkelComp && BoneIdx < SkelComp->GetNumBones())
        {
            const FName BN = SkelComp->GetBoneName(BoneIdx);
            if(bBlockStructural && IsStructuralBone(SkelComp,BoneIdx)) continue;
            if ((bHairOnly || bUseWhitelist) && !IsPhysicsWhitelist(BN)) continue;
            if (IsUEBoneNameBlacklisted(BN)) continue;
        }

        const btTransform WorldB = RB.Body->getWorldTransform();
        const FTransform CurRigidWorldUE = BulletToUE_WorldTransform_YUp(WorldB,UnitScale);
        if (CurRigidWorldUE.ContainsNaN() || !CurRigidWorldUE.GetRotation().IsNormalized()) continue;

        const FTransform CurRigidCS   = CurRigidWorldUE * W2C;
        const FTransform RefBoneWorld = RefWorld[BoneIdx];
        const FTransform RefBoneCS    = RefBoneWorld * W2C;
        const FTransform RefRigidCS   = RefBoneCS * RB.BoneToRigid;

        const FTransform DeltaRigidCS    = CurRigidCS.GetRelativeTransform(RefRigidCS);
        const FTransform TargetBoneCS    = DeltaRigidCS * RefBoneCS;
        const FTransform TargetBoneWorld = TargetBoneCS * C2W;
        if (TargetBoneWorld.ContainsNaN() || !TargetBoneWorld.GetRotation().IsNormalized()) continue;

        const FTransform Original = InOutBoneWorldUE[BoneIdx];
        const float RotDeltaDeg = FMath::RadiansToDegrees(Original.GetRotation().AngularDistance(TargetBoneWorld.GetRotation()));
        if (RotDeltaDeg > MaxRotDeg)
        {
            if (bLogDiscard && SkelComp)
            {
                UE_LOG(LogTemp, Verbose, TEXT("[MMDPhys] Discard rot writeback Bone=%s RotDelta=%.1f"),
                    *SkelComp->GetBoneName(BoneIdx).ToString(), RotDeltaDeg);
            }
            continue;
        }

        FTransform Out = Original;
        Out.SetRotation(TargetBoneWorld.GetRotation());
        if (GPhysCfg.bAllowTrans) // 可选写回位移
        {
            Out.SetLocation(TargetBoneWorld.GetLocation());
        }
        InOutBoneWorldUE[BoneIdx] = Out;

        if (GPhysCfg.bLogWB && SkelComp)
        {
            UE_LOG(LogTemp, Verbose, TEXT("[MMDPhys][WB] Bone=%s RotDelta=%.2f deg"),
                *SkelComp->GetBoneName(BoneIdx).ToString(), RotDeltaDeg);
        }
    }
}

void FMMDPhysicsSimulator::TickMMDPhysics(float DeltaSeconds, FComponentSpacePoseContext& InPose,TArray<FTransform>& InOutBoneWorldUE)
{
    USkeletalMeshComponent* SkelComp = OwnerSkelComp.Get(); if (SkelComp == nullptr) { return; } const FTransform C2W = SkelComp ? SkelComp->GetComponentTransform() : FTransform::Identity; const FTransform W2C = C2W.Inverse();

    // 先同步输入姿势，保证首次同步使用正确骨骼位置
    //if (InPose.AnimInstanceProxy) {
    //    const FBoneContainer& BoneContainer = InPose.AnimInstanceProxy->GetRequiredBones();
    //    const int32 NumCompact = BoneContainer.GetCompactPoseNumBones();
    //    const TArray<FBoneIndexType>& CPToMesh = BoneContainer.GetBoneIndicesArray();
    //    const int32 NumMeshBones = SkelComp->GetNumBones();
    //    if (InOutBoneWorldUE.Num() < NumMeshBones) {
    //        InOutBoneWorldUE.SetNum(NumMeshBones, /*bAllowShrinking*/false);
    //    }
    //    for (int32 cpIdx = 0; cpIdx < NumCompact; ++cpIdx)
    //    {
    //        const FCompactPoseBoneIndex CP(cpIdx);
    //        const int32 MeshBoneIdx = CPToMesh[cpIdx];
    //        if (MeshBoneIdx < 0 || MeshBoneIdx >= NumMeshBones) continue;
    //        const FTransform BoneCS = InPose.Pose.GetComponentSpaceTransform(CP);
    //        const FTransform BoneUE = BoneCS * C2W; // world
    //        InOutBoneWorldUE[MeshBoneIdx] = BoneUE;
    //    }
    //} else {
    //    UE_LOG(LogTemp, Error, TEXT("FMMDPhysicsSimulator::TickMMDPhysics: AnimInstanceProxy is null."));
    //}

    // 首次同步：现在骨骼数据已填充
    if(!bFirstSyncDone){
        for(BulletRigidBody& RB: BulletRigidBodies){ if(!RB.Body) continue; const int32 BoneIdx=RB.RelatedBoneIndex; if(BoneIdx<0||!InOutBoneWorldUE.IsValidIndex(BoneIdx)) continue; const FTransform BoneCS = InOutBoneWorldUE[BoneIdx] * W2C; const FTransform TargetRigidCS = BoneCS * RB.BoneToRigid; const FTransform TargetRigidWorld = TargetRigidCS * C2W; const btTransform TargetB=UEToBullet_Transform_YUp(TargetRigidWorld,UnitScale); RB.Body->setWorldTransform(TargetB); if(auto* MS=RB.Body->getMotionState()) MS->setWorldTransform(TargetB); RB.Body->clearForces(); RB.Body->setLinearVelocity(btVector3(0,0,0)); RB.Body->setAngularVelocity(btVector3(0,0,0)); RB.Body->activate(true); RB.PrevBulletWorldTransform = RB.BulletWorldTransform = TargetB; RB.PrevUEWorldTransform = RB.UEWorldTransform = TargetRigidWorld; if(GPhysCfg.bLogMap && SkelComp){ const float Dist = FVector::Distance(TargetRigidWorld.GetLocation(), InOutBoneWorldUE[BoneIdx].GetLocation()); UE_LOG(LogTemp, Verbose, TEXT("[MMDPhys][FirstSync] Rigid %d Bone %d (%s) Dist=%.2f"), RB.DebugID, BoneIdx, *SkelComp->GetBoneName(BoneIdx).ToString(), Dist); } }
        bFirstSyncDone=true;
    }

    PreSyncKinematicFromBones(InOutBoneWorldUE);
    StepSimulationMMD(DeltaSeconds);
    PostSyncBonesFromPhysics(InOutBoneWorldUE);
}

void FMMDPhysicsSimulator::DebugDraw()
{
    if (!GPhysCfg.bDebug) return; USkeletalMeshComponent* SkelComp = OwnerSkelComp.Get(); if (!SkelComp) return; UWorld* World = SkelComp->GetWorld(); if (!World) return; static uint32 FrameCounter = 0; ++FrameCounter; const int32 EveryN = FMath::Max(1, GPhysCfg.DebugEveryN); if ((FrameCounter % (uint32)EveryN) != 0) return; const float LifeTime = FMath::Max(0.f, GPhysCfg.DebugLife); const bool bPersistent = LifeTime > 0.f; const float Thickness = 1.5f; const bool bSimple = GPhysCfg.bDebugSimple; const int32 MaxCount = FMath::Max(1, GPhysCfg.DebugMax); const FTransform C2W = SkelComp->GetComponentTransform(); int32 Drawn = 0; for (const BulletRigidBody& RB : BulletRigidBodies){ if (!RB.Body) continue; if (Drawn >= MaxCount) break; ++Drawn; const btTransform WorldB = RB.Body->getWorldTransform(); const FTransform WorldUE = BulletToUE_WorldTransform_YUp(WorldB, UnitScale); const FVector Center = WorldUE.GetLocation(); const FQuat Rot = WorldUE.GetRotation(); const bool bKinematic = (RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; const bool bDynamic = RB.Body->getInvMass() > 0; const FColor Color = bKinematic ? FColor(255,128,0) : (bDynamic ? FColor(0,255,255) : FColor(200,200,200)); if (bSimple){ DrawDebugSphere(World, Center, 2.5f*UnitScale*0.1f, 8, Color, bPersistent, LifeTime, 0, Thickness); } else { DrawDebugCoordinateSystem(World, Center, Rot.Rotator(), 5.f*UnitScale*0.1f, bPersistent, LifeTime, 0, Thickness); DrawDebugSphere(World, Center, 2.0f*UnitScale*0.1f, 8, Color, bPersistent, LifeTime, 0, Thickness); } }
    Drawn = 0; for (btTypedConstraint* TC : BulletJoints){ if(!TC) continue; if (Drawn >= MaxCount) break; ++Drawn; auto* C = static_cast<btGeneric6DofSpring2Constraint*>(TC); const btTransform AWorld = C->getRigidBodyA().getWorldTransform(); const btTransform BWorld = C->getRigidBodyB().getWorldTransform(); const btTransform FA = C->getFrameOffsetA(); const btTransform FB = C->getFrameOffsetB(); const FTransform AnchorA_UE = BulletToUE_WorldTransform_YUp(AWorld*FA,UnitScale); const FTransform AnchorB_UE = BulletToUE_WorldTransform_YUp(BWorld*FB,UnitScale); DrawDebugLine(World, AnchorA_UE.GetLocation(), AnchorB_UE.GetLocation(), FColor::Green, bPersistent, LifeTime, 0, Thickness); }
}

void FMMDPhysicsSimulator::CaptureSnapshot(FMMDPhysicsSimSnapshot& OutSnapshot) const
{
    OutSnapshot.UnitScale=UnitScale; OutSnapshot.MaxSubSteps=MaxSubSteps; OutSnapshot.FixedTimeStep=FixedTimeStep; OutSnapshot.Bodies.Reset(BulletRigidBodies.Num()); for(int32 i=0;i<BulletRigidBodies.Num();++i){ const BulletRigidBody& RB=BulletRigidBodies[i]; if(!RB.Body) continue; FMMDPhysicsBodyState S; S.BodyIndex=i; const btTransform WorldB=RB.Body->getWorldTransform(); S.WorldUE=BulletToUE_WorldTransform_YUp(WorldB,UnitScale); const btVector3 vB=RB.Body->getLinearVelocity(); const btVector3 wB=RB.Body->getAngularVelocity(); S.LinearVelocityUE=BulletToUE_Vector_YUp(vB,UnitScale); S.AngularVelocityUE = BulletToUE_AngVel_YUp(wB); S.bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; S.bSleeping=RB.Body->getActivationState()==ISLAND_SLEEPING; OutSnapshot.Bodies.Add(MoveTemp(S)); }
}

bool FMMDPhysicsSimulator::ApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot, bool bRespectKinematic)
{
    if(!DynamicsWorld) return false;
    if(InSnapshot.Bodies.Num()<=0) return false;
    const int32 N=FMath::Min(BulletRigidBodies.Num(),InSnapshot.Bodies.Num());
    for(int32 i=0;i<N;++i){
        const FMMDPhysicsBodyState& S=InSnapshot.Bodies[i];
        if(!BulletRigidBodies.IsValidIndex(S.BodyIndex)) continue;
        BulletRigidBody& RB=BulletRigidBodies[S.BodyIndex];
        if(!RB.Body) continue;
        const bool bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0;
        if(bRespectKinematic && bKinematic) continue;

        const btTransform TargetB=UEToBullet_Transform_YUp(S.WorldUE,UnitScale);
        RB.Body->setWorldTransform(TargetB);
        if(auto* MS=RB.Body->getMotionState()) MS->setWorldTransform(TargetB);

        const btVector3 vB=UEToBullet_Vector_YUp(S.LinearVelocityUE,UnitScale);
        const btVector3 wB=UEToBullet_AngVel_YUp(S.AngularVelocityUE); // 修正：去掉 UnitScale
        RB.Body->setLinearVelocity(vB);
        RB.Body->setAngularVelocity(wB);

        if(S.bSleeping){ RB.Body->setActivationState(ISLAND_SLEEPING);} else { RB.Body->activate(true);}
    }
    return true;
}

void FMMDPhysicsSimulator::Shutdown()
{
    if(DynamicsWorld){ for(btGeneric6DofSpring2Constraint* C: BulletJoints){ if(C){ DynamicsWorld->removeConstraint(C); delete C; }} }
    BulletJoints.Empty(); if(DynamicsWorld){ for(BulletRigidBody& RB: BulletRigidBodies){ if(RB.Body){ DynamicsWorld->removeRigidBody(RB.Body); delete RB.Body; } if(RB.MotionState){ delete RB.MotionState; } if(RB.Shape){ delete RB.Shape; } } }
    BulletRigidBodies.Empty(); if(DynamicsWorld){ delete DynamicsWorld; DynamicsWorld=nullptr; } if(Solver){ delete Solver; Solver=nullptr; } if(Broadphase){ delete Broadphase; Broadphase=nullptr; } if(Dispatcher){ delete Dispatcher; Dispatcher=nullptr; } if(CollisionConfiguration){ delete CollisionConfiguration; CollisionConfiguration=nullptr; } bInitialized=false; bFirstSyncDone=false;
}

void FMMDPhysicsSimulator::StepSimulationMMD(float DeltaSeconds)
{
    if (!DynamicsWorld) return; DynamicsWorld->stepSimulation(DeltaSeconds, MaxSubSteps, FixedTimeStep);
}

void FMMDMotionState::getWorldTransform(btTransform& worldTrans) const
{
    worldTrans = UEToBullet(UEWorldTransform, UnitScale);
}

void FMMDMotionState::setWorldTransform(const btTransform& worldTrans)
{
    UEWorldTransform = BulletToUE(worldTrans, UnitScale);
}
