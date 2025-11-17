#include "MMDPhysicsSimulator.h"
#include "TPMXParser.h"
#include <btBulletDynamicsCommon.h>
// Debug draw
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarMMDPhysDebug(TEXT("mmd.PhysDebug"),0,TEXT("Draw MMD Bullet rigid bodies and joints in UE space. 0:off, 1:on"),ECVF_Default);
static TAutoConsoleVariable<float> CVarMMDPhysDebugLife(TEXT("mmd.PhysDebugLife"),0.033f,TEXT("Life time (seconds) for MMD physics debug primitives. >0 to keep visible for given time each frame."),ECVF_Default);

#pragma region  工具函数
static inline btMatrix3x3 Bullet_C_YUp(){ return btMatrix3x3(0,0,1,-1,0,0,0,1,0); }

// 核心骨（从不做刚体→骨骼回写）的黑名单（JP/EN常见写法）
static const TArray<FString>& GetNeverWriteBackNames()
{
    static TArray<FString> Names = {
        TEXT("センター"), TEXT("センタ"), TEXT("全ての親"), TEXT("センター位置"),
        TEXT("下半身"), TEXT("上半身"), TEXT("上半身2"), TEXT("上半身3"),
        TEXT("首"), TEXT("頭"), TEXT("頭頂部"), TEXT("腰"),
        TEXT("Center"), TEXT("Root"), TEXT("Parent"), TEXT("AllParent"),
        TEXT("UpperBody"), TEXT("UpperBody2"), TEXT("UpperBody3"), TEXT("LowerBody"),
        TEXT("Neck"), TEXT("Head"), TEXT("Waist"), TEXT("Groove"), TEXT("グルーブ")
    }; return Names;
}
static bool IsNeverWriteBackBone(const FString& NameJP, const FString& NameEN)
{
    auto& L = GetNeverWriteBackNames();
    for (const FString& N : L)
    {
        if (NameJP.Equals(N, ESearchCase::IgnoreCase) || NameEN.Equals(N, ESearchCase::IgnoreCase)) return true;
    }
    return false;
}

// 将 PMX 骨索引映射到 UE 骨索引：优先按名称匹配，其次在名称吻合前提下允许 +1 映射
static int32 ResolveUEBoneIndexForPMX(const PMXDatas& PMXData, int32 PMXBoneIndex, const USkeletalMeshComponent* SkelComp)
{
    if (!SkelComp) return -1;
    if (PMXBoneIndex < 0) return -1;

    // 名称优先
    if (PMXData.ModelBones.IsValidIndex(PMXBoneIndex))
    {
        const PMXBone& PB = PMXData.ModelBones[PMXBoneIndex];
        const FName JPName(*PB.NameJP);
        const FName ENName(*PB.NameEN);
        int32 ByName = SkelComp->GetBoneIndex(JPName);
        if (ByName == INDEX_NONE && !PB.NameEN.IsEmpty())
        {
            ByName = SkelComp->GetBoneIndex(ENName);
        }
        if (ByName != INDEX_NONE)
        {
            return ByName;
        }
    }

    // 谨慎地尝试 +1：仅当范围合法，并且 UE 该索引名与 PMX 名称之一匹配
    const int32 Guess = PMXBoneIndex + 1; // MeshBuilder 可能在 0 插入 Root
    if (Guess >= 0 && SkelComp && Guess < SkelComp->GetNumBones() && PMXData.ModelBones.IsValidIndex(PMXBoneIndex))
    {
        const PMXBone& PB = PMXData.ModelBones[PMXBoneIndex];
        const FName UEName = SkelComp->GetBoneName(Guess);
        if (UEName.IsEqual(FName(*PB.NameJP), ENameCase::IgnoreCase, true) || (!PB.NameEN.IsEmpty() && UEName.IsEqual(FName(*PB.NameEN), ENameCase::IgnoreCase, true)))
        {
            return Guess;
        }
    }

    return -1;
}

static inline btTransform UEToBullet_Transform_YUp(const FTransform& TUE, float UnitScale)
{
    btTransform TB; TB.setIdentity();
    const FVector p = TUE.GetLocation();
    // UE(cm) -> Bullet/PMX : divide UnitScale
    TB.setOrigin(btVector3(-p.Y / UnitScale, p.Z / UnitScale, p.X / UnitScale));
    const FMatrix MUE = FRotationMatrix::Make(TUE.GetRotation());
    const btMatrix3x3 RUE(
        (btScalar)MUE.M[0][0], (btScalar)MUE.M[0][1], (btScalar)MUE.M[0][2],
        (btScalar)MUE.M[1][0], (btScalar)MUE.M[1][1], (btScalar)MUE.M[1][2],
        (btScalar)MUE.M[2][0], (btScalar)MUE.M[2][1], (btScalar)MUE.M[2][2]
    );
    const btMatrix3x3 C = Bullet_C_YUp();
    const btMatrix3x3 RB = C.transpose() * RUE * C; btQuaternion qB; RB.getRotation(qB); TB.setRotation(qB); return TB;
}

static inline FTransform BulletToUE_Transform_YUp(const btTransform& TB, float UnitScale)
{
    const btVector3 pB = TB.getOrigin();
    // Bullet/PMX -> UE(cm): multiply UnitScale
    const FVector pUE(pB.getZ() * UnitScale, -pB.getX() * UnitScale, pB.getY() * UnitScale);
    const btMatrix3x3 RB = TB.getBasis(); const btMatrix3x3 C = Bullet_C_YUp(); const btMatrix3x3 RUE = C * RB * C.transpose();
    FMatrix M=FMatrix::Identity; M.M[0][0]=(float)RUE[0][0]; M.M[0][1]=(float)RUE[0][1]; M.M[0][2]=(float)RUE[0][2]; M.M[1][0]=(float)RUE[1][0]; M.M[1][1]=(float)RUE[1][1]; M.M[1][2]=(float)RUE[1][2]; M.M[2][0]=(float)RUE[2][0]; M.M[2][1]=(float)RUE[2][1]; M.M[2][2]=(float)RUE[2][2];
    return FTransform(FQuat(M), pUE, FVector(1));
}
static inline FVector BulletToUE_Vector_YUp(const btVector3& vB, float UnitScale){ return FVector(vB.getZ() * UnitScale, -vB.getX() * UnitScale, vB.getY() * UnitScale);} 
static inline btVector3 UEToBullet_Vector_YUp(const FVector& vUE, float UnitScale){ return btVector3(-vUE.Y / UnitScale, vUE.Z / UnitScale, vUE.X / UnitScale);} 
static inline FVector BulletToUE_AngVel_YUp(const btVector3& wB){ return FVector(wB.getZ(), -wB.getX(), wB.getY()); }
static inline btVector3 UEToBullet_AngVel_YUp(const FVector& wUE){ return btVector3(-wUE.Y, wUE.Z, wUE.X); }

static inline FTransform ConvertPMXPositionRotationToUnrealFTransform(const FVector& PMXPosition,const FVector& PMXRotationRad,float UnitScale)
{
    // PMX -> UE(cm): multiply UnitScale
    const FVector UEPos(PMXPosition.X * UnitScale, -PMXPosition.Z * UnitScale, PMXPosition.Y * UnitScale);
    const double sx = FMath::Sin(PMXRotationRad.X), cx = FMath::Cos(PMXRotationRad.X);
    const double sy = FMath::Sin(PMXRotationRad.Y), cy = FMath::Cos(PMXRotationRad.Y);
    const double sz = FMath::Sin(PMXRotationRad.Z), cz = FMath::Cos(PMXRotationRad.Z);
    const double Rx[3][3]={{1,0,0},{0,cx,-sx},{0,sx,cx}}; const double Ry[3][3]={{cy,0,sy},{0,1,0},{-sy,0,cy}}; const double Rz[3][3]={{cz,-sz,0},{sz,cz,0},{0,0,1}};
    auto Mul33=[&](const double A[3][3],const double B[3][3],double O[3][3]){ for(int r=0;r<3;++r) for(int c=0;c<3;++c) O[r][c]=A[r][0]*B[0][c]+A[r][1]*B[1][c]+A[r][2]*B[2][c];};
    double Rzy[3][3]; Mul33(Rz,Ry,Rzy); double Rpmx[3][3]; Mul33(Rzy,Rx,Rpmx);
    const double C[3][3]={{1,0,0},{0,0,-1},{0,1,0}}; const double CT[3][3]={{1,0,0},{0,0,1},{0,-1,0}}; double T[3][3]; Mul33(C,Rpmx,T); double Rue[3][3]; Mul33(T,CT,Rue);
    FMatrix M=FMatrix::Identity; M.M[0][0]=(float)Rue[0][0]; M.M[0][1]=(float)Rue[0][1]; M.M[0][2]=(float)Rue[0][2]; M.M[1][0]=(float)Rue[1][0]; M.M[1][1]=(float)Rue[1][1]; M.M[1][2]=(float)Rue[1][2]; M.M[2][0]=(float)Rue[2][0]; M.M[2][1]=(float)Rue[2][1]; M.M[2][2]=(float)Rue[2][2];
    return FTransform(FQuat(M), UEPos, FVector(1));
}

// 按“Bullet 按 PMX 尺度运行”：形状尺寸直接使用 PMX 数值（不除 UnitScale）。
static inline btCollisionShape* CreateCollisionShape(const PMXRigid& Rigid, float UnitScale)
{
    const btScalar sx = (btScalar)(Rigid.Size.X);
    const btScalar sy = (btScalar)(Rigid.Size.Y);
    const btScalar sz = (btScalar)(Rigid.Size.Z);
    switch(Rigid.ShapeType)
    {
    case 0: return new btSphereShape(sx);
    case 1: return new btBoxShape(btVector3(sx, sy, sz)); // PMX box size 为半长，Bullet 传半长，故不再 *0.5
    case 2: return new btCapsuleShape(sx, sy);
    default: UE_LOG(LogTemp, Error, TEXT("未知形状类型: %d"),Rigid.ShapeType); return new btBoxShape(btVector3(0.05f,0.05f,0.05f)); }
}
static inline btTransform MakeBulletStartTransformFromPMX(const PMXRigid& Rigid, float UnitScale){ const FTransform LocalUE=ConvertPMXPositionRotationToUnrealFTransform(Rigid.Position,Rigid.Rotation,UnitScale); return UEToBullet_Transform_YUp(LocalUE,UnitScale);} 
static inline btDefaultMotionState* CreateMotionState(const PMXRigid& Rigid,float UnitScale){ const btTransform StartB=MakeBulletStartTransformFromPMX(Rigid,UnitScale); return new btDefaultMotionState(StartB);} 
static inline btRigidBody& GetBulletFixedBody(){ static btRigidBody sFixedBody(0.f,nullptr,nullptr); return sFixedBody; }
#pragma endregion

bool FMMDPhysicsSimulator::InitializeFromPMX(const PMXDatas& PMXData, USkeletalMeshComponent* InSkelComp, float InUnitScale, int32 InMaxSubSteps, float InFixedTimeStep)
{
    if(!InSkelComp){ UE_LOG(LogTemp, Error, TEXT("InitializeFromPMX failed: SkeletalMeshComponent null")); return false; }
    if(bInitialized){ UE_LOG(LogTemp, Warning, TEXT("InitializeFromPMX skipped: already initialized")); return true; }
    UnitScale=InUnitScale; MaxSubSteps=InMaxSubSteps; FixedTimeStep=InFixedTimeStep; OwnerSkelComp=InSkelComp;
    InitializeBulletWorld(); InitializeRigidBody(PMXData); InitializeJoints(PMXData); bInitialized=true; return true;
}

void FMMDPhysicsSimulator::InitializeBulletWorld()
{
    CollisionConfiguration=new btDefaultCollisionConfiguration();
    Dispatcher=new btCollisionDispatcher(CollisionConfiguration);
    Broadphase=new btDbvtBroadphase();
    Solver=new btSequentialImpulseConstraintSolver();
    DynamicsWorld=new btDiscreteDynamicsWorld(Dispatcher,Broadphase,Solver,CollisionConfiguration);
    // Gravity: use -9.81 m/s^2 converted to PMX units (0.08m or 0.1m per unit depending on UnitScale)
    // Our mesh builder uses 8 cm per unit => 0.08m per unit, so -9.81/0.08 ~= -122.625
    const float MetersPerUnit = (UnitScale/100.0f); // 8 cm -> 0.08 m
    const float GravityUnitsPerSec2 = -9.81f / FMath::Max(1e-3f, MetersPerUnit);
    DynamicsWorld->setGravity(btVector3(0.f, GravityUnitsPerSec2, 0.f));
    btContactSolverInfo& SI=DynamicsWorld->getSolverInfo();
    SI.m_numIterations=8;
    SI.m_splitImpulse=1; SI.m_splitImpulsePenetrationThreshold=-0.02f; SI.m_erp=0.2f; SI.m_erp2=0.1f; SI.m_globalCfm=0.f;
    SI.m_solverMode|=SOLVER_USE_WARMSTARTING|SOLVER_USE_2_FRICTION_DIRECTIONS|SOLVER_ENABLE_FRICTION_DIRECTION_CACHING;
}

void FMMDPhysicsSimulator::InitializeRigidBody(const PMXDatas& PMXData)
{
    if(!DynamicsWorld) return; int i=0; 
    USkeletalMeshComponent* SkelComp = OwnerSkelComp.Get();

    for(const PMXRigid& Rigid: PMXData.ModelRigids)
    {
        BulletRigidBody NewRigidBody; NewRigidBody.Shape=CreateCollisionShape(Rigid,UnitScale); NewRigidBody.MotionState=CreateMotionState(Rigid,UnitScale);
        NewRigidBody.PhysicsMode = Rigid.PhysicsMode;

        // Metadata
        NewRigidBody.PMXBoneIndex = Rigid.RelatedBoneIndex;
        if (PMXData.ModelBones.IsValidIndex(Rigid.RelatedBoneIndex))
        {
            NewRigidBody.PMXBoneNameJP = PMXData.ModelBones[Rigid.RelatedBoneIndex].NameJP;
            NewRigidBody.PMXBoneNameEN = PMXData.ModelBones[Rigid.RelatedBoneIndex].NameEN;
        }

        // Mapping: 名称优先，+1 次之（需名校验）
        NewRigidBody.RelatedBoneIndex = ResolveUEBoneIndexForPMX(PMXData, Rigid.RelatedBoneIndex, SkelComp);

        const bool bBoneFollow = (Rigid.PhysicsMode==0);
        const bool bDynamic = (Rigid.PhysicsMode!=0);

        btScalar Mass = bDynamic ? btScalar(FMath::Max(Rigid.Mass,0.0001f)) : btScalar(0);
        btVector3 Inertia(0,0,0); if(Mass>0) NewRigidBody.Shape->calculateLocalInertia(Mass,Inertia);
        btRigidBody::btRigidBodyConstructionInfo CI(Mass,NewRigidBody.MotionState,NewRigidBody.Shape,Inertia); 
        CI.m_friction=(btScalar)Rigid.Friction; CI.m_restitution=(btScalar)Rigid.Restitution; CI.m_linearDamping=(btScalar)Rigid.LinearDamping; CI.m_angularDamping=(btScalar)Rigid.AngularDamping;
        NewRigidBody.Body=new btRigidBody(CI);
        NewRigidBody.Body->setSleepingThresholds(0,0);
        NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);

        if(bBoneFollow)
        {
            // Kinematic follower, keep contact response for body collision
            NewRigidBody.Body->setCollisionFlags(NewRigidBody.Body->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT);
        }

        const unsigned short GroupBit = (unsigned short)(1u << (Rigid.Group & 0xF));
        const unsigned short MaskBits  = (unsigned short)(Rigid.CollisionMask);
        NewRigidBody.CollisionGroup = GroupBit; NewRigidBody.CollisionMask = MaskBits;
        DynamicsWorld->addRigidBody(NewRigidBody.Body, NewRigidBody.CollisionGroup, NewRigidBody.CollisionMask);

        btTransform StartB; if(NewRigidBody.Body->getMotionState()) NewRigidBody.Body->getMotionState()->getWorldTransform(StartB); else StartB=NewRigidBody.Body->getWorldTransform();
        NewRigidBody.BulletWorldTransform=StartB; NewRigidBody.PrevBulletWorldTransform=StartB; NewRigidBody.UEWorldTransform=BulletToUE_Transform_YUp(StartB,UnitScale); NewRigidBody.PrevUEWorldTransform=NewRigidBody.UEWorldTransform;

        // 计算 BoneToRigid/ RigidToBone：明确使用 组件->世界
        if (SkelComp && NewRigidBody.RelatedBoneIndex >= 0 && SkelComp->GetNumBones() > NewRigidBody.RelatedBoneIndex)
        {
            const int32 B = NewRigidBody.RelatedBoneIndex;
            const FTransform BoneCS = SkelComp->GetBoneTransform(B);
            const FTransform C2W = SkelComp->GetComponentTransform();
            const FTransform BoneWorld = BoneCS * C2W;
            NewRigidBody.BoneToRigid = NewRigidBody.UEWorldTransform.GetRelativeTransform(BoneWorld);
            NewRigidBody.RigidToBone = NewRigidBody.BoneToRigid.Inverse();
        }
        else
        {
            if (SkelComp && (NewRigidBody.RelatedBoneIndex<0 || NewRigidBody.RelatedBoneIndex>=SkelComp->GetNumBones()))
            {
                UE_LOG(LogTemp, Warning, TEXT("[MMDPhys] Rigid %d mapped bone invalid (PMX=%d -> UE=%d, NumBones=%d)"), i, Rigid.RelatedBoneIndex, NewRigidBody.RelatedBoneIndex, SkelComp->GetNumBones());
            }
            NewRigidBody.BoneToRigid=ConvertPMXPositionRotationToUnrealFTransform(Rigid.Position,Rigid.Rotation,UnitScale); NewRigidBody.RigidToBone=NewRigidBody.BoneToRigid.Inverse();
        }

        // PMX Bone Flag: TransformAfterPhysics (0x1000)
        bool bAfter = false;
        if (PMXData.ModelBones.IsValidIndex(NewRigidBody.PMXBoneIndex))
        {
            bAfter = (PMXData.ModelBones[NewRigidBody.PMXBoneIndex].Flags & 0x1000) != 0;
        }

        // 决定是否允许刚体→骨骼回写：PMX AfterPhysics 且动态体，且不在核心黑名单
        const bool bNever = IsNeverWriteBackBone(NewRigidBody.PMXBoneNameJP, NewRigidBody.PMXBoneNameEN);
        NewRigidBody.bTransformAfterPhysics = (bDynamic && bAfter && !bNever);

        // 日志：刚体→骨映射
        const FString UEBoneName = (SkelComp && NewRigidBody.RelatedBoneIndex>=0 && NewRigidBody.RelatedBoneIndex<SkelComp->GetNumBones()) ? SkelComp->GetBoneName(NewRigidBody.RelatedBoneIndex).ToString() : TEXT("<None>");
        UE_LOG(LogTemp, Log, TEXT("[MMDPhys] Rigid[%d] PMXBone(%d:%s/%s) -> UEBone(%d:%s), Mode=%d, AfterPhysFlag=%s, WriteBack=%s"),
            i,
            NewRigidBody.PMXBoneIndex,
            *NewRigidBody.PMXBoneNameJP,
            *NewRigidBody.PMXBoneNameEN,
            NewRigidBody.RelatedBoneIndex,
            *UEBoneName,
            (int32)Rigid.PhysicsMode,
            bAfter?TEXT("On"):TEXT("Off"),
            NewRigidBody.bTransformAfterPhysics?TEXT("Yes"):TEXT("No"));

        NewRigidBody.DebugID=i++;
        BulletRigidBodies.Add(NewRigidBody);
    }
}

void FMMDPhysicsSimulator::InitializeJoints(const PMXDatas& PMXData)
{
    if(!DynamicsWorld) return; auto GetBodyByIndex=[&](int32 Index)->btRigidBody*{ if(Index<0) return &GetBulletFixedBody(); if(!BulletRigidBodies.IsValidIndex(Index)) return nullptr; return BulletRigidBodies[Index].Body; };
    for(const PMXJoint& Joint: PMXData.ModelJoints)
    {
        btRigidBody* BodyA=GetBodyByIndex(Joint.RigidA); btRigidBody* BodyB=GetBodyByIndex(Joint.RigidB); if(!BodyA||!BodyB){ continue; }
        // 按 PMX 尺度：joint 位置直接用 PMX 数值（坐标轴转换）
        const FTransform JointUE=ConvertPMXPositionRotationToUnrealFTransform(Joint.Position,Joint.Rotation,UnitScale); const btTransform JointB=UEToBullet_Transform_YUp(JointUE,UnitScale);
        const btTransform AWorld=BodyA->getWorldTransform(); const btTransform BWorld=BodyB->getWorldTransform(); const btTransform FrameInA=AWorld.inverse()*JointB; const btTransform FrameInB=BWorld.inverse()*JointB;
        auto* Constraint=new btGeneric6DofSpring2Constraint(*BodyA,*BodyB,FrameInA,FrameInB,RO_XYZ);

        // 线性限位：直接用 PMX 数值（不缩放）
        btVector3 LinLo(Joint.LimitPosLower.X,Joint.LimitPosLower.Y,Joint.LimitPosLower.Z);
        btVector3 LinHi(Joint.LimitPosUpper.X,Joint.LimitPosUpper.Y,Joint.LimitPosUpper.Z);
        Constraint->setLinearLowerLimit(LinLo); Constraint->setLinearUpperLimit(LinHi);

        // 角度限位：弧度直接使用
        btVector3 AngLo(Joint.LimitRotLower.X,Joint.LimitRotLower.Y,Joint.LimitRotLower.Z); btVector3 AngHi(Joint.LimitRotUpper.X,Joint.LimitRotUpper.Y,Joint.LimitRotUpper.Z);
        Constraint->setAngularLowerLimit(AngLo); Constraint->setAngularUpperLimit(AngHi);

        for(int axis=0;axis<3;++axis){ const float KLin=Joint.SpringPos[axis]; Constraint->enableSpring(axis,KLin>0.f); if(KLin>0.f){ Constraint->setStiffness(axis,KLin);} const int AngAxis=3+axis; const float KAng=Joint.SpringRot[axis]; Constraint->enableSpring(AngAxis,KAng>0.f); if(KAng>0.f){ Constraint->setStiffness(AngAxis,KAng);} }
        Constraint->setEquilibriumPoint(); DynamicsWorld->addConstraint(Constraint,true); BulletJoints.Add(Constraint);
    }
}

void FMMDPhysicsSimulator::PreSyncKinematicFromBones(const TArray<FTransform>& BoneWorldUE)
{
    if(!DynamicsWorld) return; for(BulletRigidBody& RB: BulletRigidBodies){ if(!RB.Body) continue; const int32 BoneIdx=RB.RelatedBoneIndex; if(BoneIdx<0||!BoneWorldUE.IsValidIndex(BoneIdx)) continue;
        const bool bIsKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0;
        if(bIsKinematic){ const FTransform TargetUE=BoneWorldUE[BoneIdx]*RB.BoneToRigid; const btTransform TargetB=UEToBullet_Transform_YUp(TargetUE,UnitScale); const btTransform PrevB=RB.Body->getWorldTransform(); RB.Body->setWorldTransform(TargetB); if(auto* MS=RB.Body->getMotionState()) MS->setWorldTransform(TargetB); RB.Body->setInterpolationWorldTransform(PrevB); RB.Body->setLinearVelocity(btVector3(0,0,0)); RB.Body->setAngularVelocity(btVector3(0,0,0)); continue; }
        if(RB.PhysicsMode==2){ const FTransform TargetUE=BoneWorldUE[BoneIdx]*RB.BoneToRigid; const btTransform TargetB=UEToBullet_Transform_YUp(TargetUE,UnitScale); const btTransform CurB=RB.Body->getWorldTransform();
            btVector3 posErr = TargetB.getOrigin() - CurB.getOrigin(); const float PosGain = 6.f; const float PosDamp = 2.5f; btVector3 vel = RB.Body->getLinearVelocity(); btVector3 correctiveVel = posErr * PosGain - vel * PosDamp; RB.Body->setLinearVelocity(vel + correctiveVel);
            btQuaternion qa,qb; CurB.getBasis().getRotation(qa); TargetB.getBasis().getRotation(qb); btQuaternion dq=qb*qa.inverse(); dq.normalize(); btScalar ang = dq.getAngle(); if(ang>SIMD_EPSILON){ btVector3 axis(dq.getX(),dq.getY(),dq.getZ()); axis.normalize(); btVector3 angVel=RB.Body->getAngularVelocity(); const float RotGain=6.f; const float RotDamp=2.0f; btVector3 correctiveAngVel=axis*(ang*RotGain) - angVel*RotDamp; RB.Body->setAngularVelocity(angVel + correctiveAngVel); } RB.Body->activate(true);} }
}

void FMMDPhysicsSimulator::StepSimulationMMD(float DeltaSeconds){ if(!DynamicsWorld) return; DynamicsWorld->stepSimulation(DeltaSeconds,MaxSubSteps,FixedTimeStep); }

void FMMDPhysicsSimulator::PostSyncBonesFromPhysics(TArray<FTransform>& InOutBoneWorldUE)
{
    if(!DynamicsWorld) return; for(const BulletRigidBody& RB: BulletRigidBodies){ if(!RB.Body) continue; if(!RB.bTransformAfterPhysics) continue; const int32 BoneIdx=RB.RelatedBoneIndex; if(BoneIdx<0||!InOutBoneWorldUE.IsValidIndex(BoneIdx)) continue; const bool bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; if(bKinematic) continue; const btTransform WorldB=RB.Body->getWorldTransform(); const FTransform WorldUE=BulletToUE_Transform_YUp(WorldB,UnitScale); const FTransform BoneUE=WorldUE*RB.RigidToBone; InOutBoneWorldUE[BoneIdx]=BoneUE; }
}

void FMMDPhysicsSimulator::TickMMDPhysics(float DeltaSeconds, TArray<FTransform>& InOutBoneWorldUE)
{
    if(!bFirstSyncDone){ for(BulletRigidBody& RB: BulletRigidBodies){ if(!RB.Body) continue; const int32 BoneIdx=RB.RelatedBoneIndex; if(BoneIdx<0||!InOutBoneWorldUE.IsValidIndex(BoneIdx)) continue; const FTransform TargetUE=InOutBoneWorldUE[BoneIdx]*RB.BoneToRigid; const btTransform TargetB=UEToBullet_Transform_YUp(TargetUE,UnitScale); RB.Body->setWorldTransform(TargetB); if(auto* MS=RB.Body->getMotionState()) MS->setWorldTransform(TargetB); RB.Body->clearForces(); RB.Body->setLinearVelocity(btVector3(0,0,0)); RB.Body->setAngularVelocity(btVector3(0,0,0)); RB.Body->activate(true);} bFirstSyncDone=true; }
    PreSyncKinematicFromBones(InOutBoneWorldUE); StepSimulationMMD(DeltaSeconds); PostSyncBonesFromPhysics(InOutBoneWorldUE);
    // DebugDraw must be called on GameThread. The anim node schedules it; avoid calling here (AnyThread).
}

void FMMDPhysicsSimulator::DebugDraw()
{
    if (CVarMMDPhysDebug->GetInt() == 0) return; USkeletalMeshComponent* SkelComp = OwnerSkelComp.Get(); if (!SkelComp) return; UWorld* World = SkelComp->GetWorld(); if (!World) return;
    const float LifeTime = FMath::Max(0.f, CVarMMDPhysDebugLife->GetFloat());
    const bool bPersistent = LifeTime > 0.f; const float Thickness = 1.0f;
    for (const BulletRigidBody& RB : BulletRigidBodies){ if (!RB.Body) continue; const btCollisionShape* Shape = RB.Body->getCollisionShape(); if (!Shape) continue; const btTransform WorldB = RB.Body->getWorldTransform(); const FTransform WorldUE = BulletToUE_Transform_YUp(WorldB, UnitScale); const FVector Center = WorldUE.GetLocation(); const FQuat Rot = WorldUE.GetRotation(); const bool bKinematic = (RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; const bool bDynamic = RB.Body->getInvMass() > 0; const FColor Color = bKinematic ? FColor(255,128,0) : (bDynamic ? FColor(0,255,255) : FColor(200,200,200)); switch (Shape->getShapeType()){ case SPHERE_SHAPE_PROXYTYPE:{ const btSphereShape* S = static_cast<const btSphereShape*>(Shape); const float R = (float)S->getRadius() * UnitScale; DrawDebugSphere(World, Center, R, 16, Color, bPersistent, LifeTime, 0, Thickness); break;} case BOX_SHAPE_PROXYTYPE:{ const btBoxShape* B = static_cast<const btBoxShape*>(Shape); const btVector3 He = B->getHalfExtentsWithMargin(); const FVector Extents((float)He.getX() * UnitScale, (float)He.getY() * UnitScale, (float)He.getZ() * UnitScale); DrawDebugBox(World, Center, Extents, Rot, Color, bPersistent, LifeTime, 0, Thickness); break;} case CAPSULE_SHAPE_PROXYTYPE:{ const btCapsuleShape* C = static_cast<const btCapsuleShape*>(Shape); const float Radius = (float)C->getRadius() * UnitScale; const float HalfH = (float)C->getHalfHeight() * UnitScale; DrawDebugCapsule(World, Center, HalfH, Radius, Rot, Color, bPersistent, LifeTime, 0, Thickness); break;} default:{ DrawDebugCoordinateSystem(World, Center, Rot.Rotator(), 6.f*UnitScale, bPersistent, LifeTime, 0, Thickness); break;} }
        if (SkelComp && RB.RelatedBoneIndex >=0 && RB.RelatedBoneIndex < SkelComp->GetNumBones())
        {
            const FString Label = FString::Printf(TEXT("Rigid%d -> Bone%d:%s%s"), RB.DebugID, RB.RelatedBoneIndex, *SkelComp->GetBoneName(RB.RelatedBoneIndex).ToString(), RB.bTransformAfterPhysics?TEXT(" [WB]"):TEXT(""));
            DrawDebugString(World, Center + FVector(0,0,UnitScale*0.5f), Label, nullptr, Color, LifeTime, bPersistent);
        }
    }
    for (btTypedConstraint* TC : BulletJoints){ if(!TC) continue; auto* C = static_cast<btGeneric6DofSpring2Constraint*>(TC); const btTransform AWorld = C->getRigidBodyA().getWorldTransform(); const btTransform BWorld = C->getRigidBodyB().getWorldTransform(); const btTransform FA = C->getFrameOffsetA(); const btTransform FB = C->getFrameOffsetB(); const FTransform AnchorA_UE = BulletToUE_Transform_YUp(AWorld*FA,UnitScale); const FTransform AnchorB_UE = BulletToUE_Transform_YUp(BWorld*FB,UnitScale); DrawDebugLine(World, AnchorA_UE.GetLocation(), AnchorB_UE.GetLocation(), FColor::Green, bPersistent, LifeTime, 0, Thickness); }
}

void FMMDPhysicsSimulator::CaptureSnapshot(FMMDPhysicsSimSnapshot& OutSnapshot) const
{
    OutSnapshot.UnitScale=UnitScale; OutSnapshot.MaxSubSteps=MaxSubSteps; OutSnapshot.FixedTimeStep=FixedTimeStep; OutSnapshot.Bodies.Reset(BulletRigidBodies.Num());
    for(int32 i=0;i<BulletRigidBodies.Num();++i){ const BulletRigidBody& RB=BulletRigidBodies[i]; if(!RB.Body) continue; FMMDPhysicsBodyState S; S.BodyIndex=i; const btTransform WorldB=RB.Body->getWorldTransform(); S.WorldUE=BulletToUE_Transform_YUp(WorldB,UnitScale); const btVector3 vB=RB.Body->getLinearVelocity(); const btVector3 wB=RB.Body->getAngularVelocity(); S.LinearVelocityUE=BulletToUE_Vector_YUp(vB,UnitScale); S.AngularVelocityUE=BulletToUE_AngVel_YUp(wB); S.bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; S.bSleeping=RB.Body->getActivationState()==ISLAND_SLEEPING; OutSnapshot.Bodies.Add(MoveTemp(S)); }
}

bool FMMDPhysicsSimulator::ApplySnapshot(const FMMDPhysicsSimSnapshot& InSnapshot, bool bRespectKinematic)
{
    if(!DynamicsWorld) return false; if(InSnapshot.Bodies.Num()<=0) return false; const int32 N=FMath::Min(BulletRigidBodies.Num(),InSnapshot.Bodies.Num());
    for(int32 i=0;i<N;++i){ const FMMDPhysicsBodyState& S=InSnapshot.Bodies[i]; if(!BulletRigidBodies.IsValidIndex(S.BodyIndex)) continue; BulletRigidBody& RB=BulletRigidBodies[S.BodyIndex]; if(!RB.Body) continue; const bool bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; if(bRespectKinematic && bKinematic) continue; const btTransform TargetB=UEToBullet_Transform_YUp(S.WorldUE,UnitScale); RB.Body->setWorldTransform(TargetB); if(auto* MS=RB.Body->getMotionState()) MS->setWorldTransform(TargetB); const btVector3 vB=UEToBullet_Vector_YUp(S.LinearVelocityUE,UnitScale); const btVector3 wB=UEToBullet_AngVel_YUp(S.AngularVelocityUE); RB.Body->setLinearVelocity(vB); RB.Body->setAngularVelocity(wB); if(S.bSleeping){ RB.Body->setActivationState(ISLAND_SLEEPING);} else { RB.Body->activate(true);} }
    return true;
}

void FMMDPhysicsSimulator::Shutdown()
{
    if(DynamicsWorld){ for(btGeneric6DofSpring2Constraint* C: BulletJoints){ if(C){ DynamicsWorld->removeConstraint(C); delete C; }} }
    BulletJoints.Empty();
    if(DynamicsWorld){ for(BulletRigidBody& RB: BulletRigidBodies){ if(RB.Body){ DynamicsWorld->removeRigidBody(RB.Body); delete RB.Body; } if(RB.MotionState){ delete RB.MotionState; } if(RB.Shape){ delete RB.Shape; } } }
    BulletRigidBodies.Empty();
    if(DynamicsWorld){ delete DynamicsWorld; DynamicsWorld=nullptr; }
    if(Solver){ delete Solver; Solver=nullptr; }
    if(Broadphase){ delete Broadphase; Broadphase=nullptr; }
    if(Dispatcher){ delete Dispatcher; Dispatcher=nullptr; }
    if(CollisionConfiguration){ delete CollisionConfiguration; CollisionConfiguration=nullptr; }
    bInitialized=false; bFirstSyncDone=false;
}
