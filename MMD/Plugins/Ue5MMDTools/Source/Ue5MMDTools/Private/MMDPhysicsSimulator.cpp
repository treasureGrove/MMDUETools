#include "MMDPhysicsSimulator.h"
#include "TPMXParser.h"
#include <btBulletDynamicsCommon.h>
// Debug draw
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarMMDPhysDebug(TEXT("mmd.PhysDebug"),0,TEXT("Draw MMD Bullet rigid bodies and joints in UE space. 0:off, 1:on"),ECVF_Cheat);

#pragma region  工具函数
static inline btMatrix3x3 Bullet_C_YUp()
{
    // (x,y,z)_UE = (z, -x, y)  =>  C = [0 0 1; -1 0 0; 0 1 0]
    return btMatrix3x3(
        btScalar(0), btScalar(0), btScalar(1),
        btScalar(-1), btScalar(0), btScalar(0),
        btScalar(0), btScalar(1), btScalar(0)
    );
}

static inline btTransform UEToBullet_Transform_YUp(const FTransform& TUE, float UnitScale = 8.f)
{
    btTransform TB; TB.setIdentity();
    const FVector p = TUE.GetLocation();
    TB.setOrigin(btVector3(-p.Y / UnitScale, p.Z / UnitScale, p.X / UnitScale));
    const FMatrix MUE = FRotationMatrix::Make(TUE.GetRotation());
    const btMatrix3x3 RUE(
        (btScalar)MUE.M[0][0], (btScalar)MUE.M[0][1], (btScalar)MUE.M[0][2],
        (btScalar)MUE.M[1][0], (btScalar)MUE.M[1][1], (btScalar)MUE.M[1][2],
        (btScalar)MUE.M[2][0], (btScalar)MUE.M[2][1], (btScalar)MUE.M[2][2]
    );
    const btMatrix3x3 C = Bullet_C_YUp();
    const btMatrix3x3 RB = C.transpose() * RUE * C;
    btQuaternion qB; RB.getRotation(qB); TB.setRotation(qB); return TB;
}

static inline FTransform BulletToUE_Transform_YUp(const btTransform& TB, float UnitScale = 8.f)
{
    const btVector3 pB = TB.getOrigin();
    const FVector pUE(pB.getZ() * UnitScale, -pB.getX() * UnitScale, pB.getY() * UnitScale);
    const btMatrix3x3 RB = TB.getBasis();
    const btMatrix3x3 C = Bullet_C_YUp();
    const btMatrix3x3 RUE = C * RB * C.transpose();
    FMatrix M = FMatrix::Identity;
    M.M[0][0] = (float)RUE[0][0]; M.M[0][1] = (float)RUE[0][1]; M.M[0][2] = (float)RUE[0][2];
    M.M[1][0] = (float)RUE[1][0]; M.M[1][1] = (float)RUE[1][1]; M.M[1][2] = (float)RUE[1][2];
    M.M[2][0] = (float)RUE[2][0]; M.M[2][1] = (float)RUE[2][1]; M.M[2][2] = (float)RUE[2][2];
    const FQuat qUE = FQuat(M);
    return FTransform(qUE, pUE, FVector(1,1,1));
}
static inline FVector BulletToUE_Vector_YUp(const btVector3& vB, float UnitScale){ return FVector(vB.getZ() * UnitScale, -vB.getX() * UnitScale, vB.getY() * UnitScale);} 
static inline btVector3 UEToBullet_Vector_YUp(const FVector& vUE, float UnitScale){ return btVector3(-vUE.Y / UnitScale, vUE.Z / UnitScale, vUE.X / UnitScale);} 
static inline FVector BulletToUE_AngVel_YUp(const btVector3& wB){ return FVector(wB.getZ(), -wB.getX(), wB.getY()); }
static inline btVector3 UEToBullet_AngVel_YUp(const FVector& wUE){ return btVector3(-wUE.Y, wUE.Z, wUE.X); }

static inline FTransform ConvertPMXPositionRotationToUnrealFTransform(const FVector& PMXPosition,const FVector& PMXRotationRad,float UnitScale = 8.0f)
{
    const FVector UEPos(PMXPosition.X * UnitScale, -PMXPosition.Z * UnitScale, PMXPosition.Y * UnitScale);
    const double sx = FMath::Sin(PMXRotationRad.X); const double cx = FMath::Cos(PMXRotationRad.X);
    const double sy = FMath::Sin(PMXRotationRad.Y); const double cy = FMath::Cos(PMXRotationRad.Y);
    const double sz = FMath::Sin(PMXRotationRad.Z); const double cz = FMath::Cos(PMXRotationRad.Z);
    const double Rx[3][3]={{1,0,0},{0,cx,-sx},{0,sx,cx}}; const double Ry[3][3]={{cy,0,sy},{0,1,0},{-sy,0,cy}}; const double Rz[3][3]={{cz,-sz,0},{sz,cz,0},{0,0,1}};
    auto Mul33=[&](const double A[3][3],const double B[3][3],double O[3][3]){ for(int r=0;r<3;++r) for(int c=0;c<3;++c) O[r][c]=A[r][0]*B[0][c]+A[r][1]*B[1][c]+A[r][2]*B[2][c];};
    double Rzy[3][3]; Mul33(Rz,Ry,Rzy); double Rpmx[3][3]; Mul33(Rzy,Rx,Rpmx);
    const double C[3][3]={{1,0,0},{0,0,-1},{0,1,0}}; const double CT[3][3]={{1,0,0},{0,0,1},{0,-1,0}}; double T[3][3]; Mul33(C,Rpmx,T); double Rue[3][3]; Mul33(T,CT,Rue);
    FMatrix M=FMatrix::Identity; M.M[0][0]=(float)Rue[0][0]; M.M[0][1]=(float)Rue[0][1]; M.M[0][2]=(float)Rue[0][2];
    M.M[1][0]=(float)Rue[1][0]; M.M[1][1]=(float)Rue[1][1]; M.M[1][2]=(float)Rue[1][2];
    M.M[2][0]=(float)Rue[2][0]; M.M[2][1]=(float)Rue[2][1]; M.M[2][2]=(float)Rue[2][2];
    const FQuat UERot = FQuat(M); return FTransform(UERot, UEPos, FVector(1,1,1));
}

static inline btCollisionShape* CreateCollisionShape(const PMXRigid& Rigid)
{
    switch(Rigid.ShapeType)
    {
    case 0: return new btSphereShape((btScalar)Rigid.Size.X);
    case 1: return new btBoxShape(btVector3((btScalar)Rigid.Size.X*0.5f,(btScalar)Rigid.Size.Y*0.5f,(btScalar)Rigid.Size.Z*0.5f));
    case 2: return new btCapsuleShape((btScalar)Rigid.Size.X,(btScalar)Rigid.Size.Y);
    default: UE_LOG(LogTemp, Error, TEXT("未知形状类型: %d"),Rigid.ShapeType); return new btBoxShape(btVector3(0.05f,0.05f,0.05f)); }
}
static inline btTransform MakeBulletStartTransformFromPMX(const PMXRigid& Rigid, float UnitScale = 8.f){ const FTransform LocalUE=ConvertPMXPositionRotationToUnrealFTransform(Rigid.Position,Rigid.Rotation,UnitScale); return UEToBullet_Transform_YUp(LocalUE,UnitScale);} 
static inline btDefaultMotionState* CreateMotionState(const PMXRigid& Rigid,float UnitScale = 8.f){ const btTransform StartB=MakeBulletStartTransformFromPMX(Rigid,UnitScale); return new btDefaultMotionState(StartB);} 
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
    DynamicsWorld->setGravity(btVector3(0,-9.81f,0));
    btContactSolverInfo& SI=DynamicsWorld->getSolverInfo();
    SI.m_numIterations=15; SI.m_splitImpulse=1; SI.m_splitImpulsePenetrationThreshold=-0.02f; SI.m_erp=0.2f; SI.m_erp2=0.1f; SI.m_globalCfm=0.f;
    SI.m_solverMode|=SOLVER_USE_WARMSTARTING|SOLVER_USE_2_FRICTION_DIRECTIONS|SOLVER_ENABLE_FRICTION_DIRECTION_CACHING|SOLVER_RANDMIZE_ORDER;
}

void FMMDPhysicsSimulator::InitializeRigidBody(const PMXDatas PMXData)
{
    if(!DynamicsWorld) return; int i=0; for(const PMXRigid& Rigid: PMXData.ModelRigids)
    {
        BulletRigidBody NewRigidBody; NewRigidBody.Shape=CreateCollisionShape(Rigid); NewRigidBody.MotionState=CreateMotionState(Rigid,8.f);
        btScalar Mass=(Rigid.PhysicsMode==0)? btScalar(0): btScalar(FMath::Max(Rigid.Mass,0.0001f)); btVector3 Inertia(0,0,0); if(Mass>0) NewRigidBody.Shape->calculateLocalInertia(Mass,Inertia);
        btRigidBody::btRigidBodyConstructionInfo CI(Mass,NewRigidBody.MotionState,NewRigidBody.Shape,Inertia); CI.m_friction=(btScalar)Rigid.Friction; CI.m_restitution=(btScalar)Rigid.Restitution; CI.m_linearDamping=(btScalar)Rigid.LinearDamping; CI.m_angularDamping=(btScalar)Rigid.AngularDamping;
        NewRigidBody.Body=new btRigidBody(CI);
        switch(Rigid.PhysicsMode){ case 0: NewRigidBody.Body->setCollisionFlags(NewRigidBody.Body->getCollisionFlags()|btCollisionObject::CF_KINEMATIC_OBJECT); NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION); break; case 1: NewRigidBody.Body->setActivationState(ACTIVE_TAG); break; case 2: NewRigidBody.Body->setActivationState(ACTIVE_TAG); break; default: break; }
        NewRigidBody.CollisionGroup=Rigid.Group; NewRigidBody.CollisionMask=Rigid.CollisionMask; DynamicsWorld->addRigidBody(NewRigidBody.Body,NewRigidBody.CollisionGroup,NewRigidBody.CollisionMask);
        btTransform StartB; if(NewRigidBody.Body->getMotionState()) NewRigidBody.Body->getMotionState()->getWorldTransform(StartB); else StartB=NewRigidBody.Body->getWorldTransform();
        NewRigidBody.BulletWorldTransform=StartB; NewRigidBody.PrevBulletWorldTransform=StartB; NewRigidBody.UEWorldTransform=BulletToUE_Transform_YUp(StartB,8.f); NewRigidBody.PrevUEWorldTransform=NewRigidBody.UEWorldTransform;
        NewRigidBody.RelatedBoneIndex=Rigid.RelatedBoneIndex; NewRigidBody.BoneToRigid=ConvertPMXPositionRotationToUnrealFTransform(Rigid.Position,Rigid.Rotation,8.f); NewRigidBody.RigidToBone=NewRigidBody.BoneToRigid.Inverse(); NewRigidBody.DebugID=i++;
        BulletRigidBodies.Add(NewRigidBody);
    }
}

void FMMDPhysicsSimulator::InitializeJoints(const PMXDatas PMXData)
{
    if(!DynamicsWorld) return; auto GetBodyByIndex=[&](int32 Index)->btRigidBody*{ if(Index<0) return &GetBulletFixedBody(); if(!BulletRigidBodies.IsValidIndex(Index)) return nullptr; return BulletRigidBodies[Index].Body; };
    for(const PMXJoint& Joint: PMXData.ModelJoints)
    {
        btRigidBody* BodyA=GetBodyByIndex(Joint.RigidA); btRigidBody* BodyB=GetBodyByIndex(Joint.RigidB); if(!BodyA||!BodyB){ UE_LOG(LogTemp, Error, TEXT("Joint references invalid/null rigid bodies: %d, %d"),Joint.RigidA,Joint.RigidB); continue; }
        const FTransform JointUE=ConvertPMXPositionRotationToUnrealFTransform(Joint.Position,Joint.Rotation,UnitScale); const btTransform JointB=UEToBullet_Transform_YUp(JointUE,UnitScale);
        const btTransform AWorld=BodyA->getWorldTransform(); const btTransform BWorld=BodyB->getWorldTransform(); const btTransform FrameInA=AWorld.inverse()*JointB; const btTransform FrameInB=BWorld.inverse()*JointB;
        auto* Constraint=new btGeneric6DofSpring2Constraint(*BodyA,*BodyB,FrameInA,FrameInB,RO_XYZ);
        btVector3 LinLo(Joint.LimitPosLower.X,Joint.LimitPosLower.Y,Joint.LimitPosLower.Z); btVector3 LinHi(Joint.LimitPosUpper.X,Joint.LimitPosUpper.Y,Joint.LimitPosUpper.Z); for(int a=0;a<3;++a) if(LinLo[a]>LinHi[a]){ LinLo[a]=-BT_LARGE_FLOAT; LinHi[a]=BT_LARGE_FLOAT; }
        Constraint->setLinearLowerLimit(LinLo); Constraint->setLinearUpperLimit(LinHi);
        btVector3 AngLo(Joint.LimitRotLower.X,Joint.LimitRotLower.Y,Joint.LimitRotLower.Z); btVector3 AngHi(Joint.LimitRotUpper.X,Joint.LimitRotUpper.Y,Joint.LimitRotUpper.Z); for(int a=0;a<3;++a) if(AngLo[a]>AngHi[a]){ AngLo[a]=-SIMD_PI; AngHi[a]=SIMD_PI; }
        Constraint->setAngularLowerLimit(AngLo); Constraint->setAngularUpperLimit(AngHi);
        for(int axis=0;axis<3;++axis){ const float KLin=Joint.SpringPos[axis]; Constraint->enableSpring(axis,KLin>0.f); if(KLin>0.f){ Constraint->setStiffness(axis,KLin); Constraint->setDamping(axis,0.6f);} const int AngAxis=3+axis; const float KAng=Joint.SpringRot[axis]; Constraint->enableSpring(AngAxis,KAng>0.f); if(KAng>0.f){ Constraint->setStiffness(AngAxis,KAng); Constraint->setDamping(AngAxis,0.6f);} }
        Constraint->setEquilibriumPoint(); for(int i=0;i<6;++i) Constraint->setParam(BT_CONSTRAINT_STOP_ERP,0.475f,i); DynamicsWorld->addConstraint(Constraint,true); BulletJoints.Add(Constraint);
    }
}

void FMMDPhysicsSimulator::PreSyncKinematicFromBones(const TArray<FTransform>& BoneWorldUE)
{
    if(!DynamicsWorld) return; for(BulletRigidBody& RB: BulletRigidBodies){ if(!RB.Body) continue; const bool bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; if(!bKinematic) continue; const int32 BoneIdx=RB.RelatedBoneIndex; if(BoneIdx<0||!BoneWorldUE.IsValidIndex(BoneIdx)) continue; const FTransform TargetUE=BoneWorldUE[BoneIdx]*RB.BoneToRigid; const btTransform TargetB=UEToBullet_Transform_YUp(TargetUE,UnitScale); const btTransform PrevB=RB.Body->getWorldTransform(); RB.Body->setInterpolationWorldTransform(PrevB); RB.Body->setWorldTransform(TargetB); if(auto* MS=RB.Body->getMotionState()) MS->setWorldTransform(TargetB); if(FixedTimeStep>0.f){ const btVector3 dv=(TargetB.getOrigin()-PrevB.getOrigin())/FixedTimeStep; RB.Body->setInterpolationLinearVelocity(dv); btQuaternion qa,qb; PrevB.getBasis().getRotation(qa); TargetB.getBasis().getRotation(qb); btQuaternion dq=qb*qa.inverse(); dq.normalize(); btScalar angle=dq.getAngle(); if(angle>SIMD_EPSILON){ btVector3 axis=btVector3(dq.getX(),dq.getY(),dq.getZ()).normalized(); RB.Body->setInterpolationAngularVelocity(axis*(angle/FixedTimeStep)); } else { RB.Body->setInterpolationAngularVelocity(btVector3(0,0,0)); } } RB.Body->activate(true); }
}

void FMMDPhysicsSimulator::StepSimulationMMD(float DeltaSeconds){ if(!DynamicsWorld) return; DynamicsWorld->stepSimulation(DeltaSeconds,MaxSubSteps,FixedTimeStep); }

void FMMDPhysicsSimulator::PostSyncBonesFromPhysics(TArray<FTransform>& InOutBoneWorldUE)
{
    if(!DynamicsWorld) return; for(const BulletRigidBody& RB: BulletRigidBodies){ if(!RB.Body) continue; const int32 BoneIdx=RB.RelatedBoneIndex; if(BoneIdx<0||!InOutBoneWorldUE.IsValidIndex(BoneIdx)) continue; const bool bKinematic=(RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT)!=0; if(bKinematic) continue; const btTransform WorldB=RB.Body->getWorldTransform(); const FTransform WorldUE=BulletToUE_Transform_YUp(WorldB,UnitScale); const FTransform BoneUE=WorldUE*RB.RigidToBone; InOutBoneWorldUE[BoneIdx]=BoneUE; }
}

void FMMDPhysicsSimulator::TickMMDPhysics(float DeltaSeconds, TArray<FTransform>& InOutBoneWorldUE)
{
    if(!bFirstSyncDone){ for(BulletRigidBody& RB: BulletRigidBodies){ if(!RB.Body) continue; const int32 BoneIdx=RB.RelatedBoneIndex; if(BoneIdx<0||!InOutBoneWorldUE.IsValidIndex(BoneIdx)) continue; const FTransform TargetUE=InOutBoneWorldUE[BoneIdx]*RB.BoneToRigid; const btTransform TargetB=UEToBullet_Transform_YUp(TargetUE,UnitScale); RB.Body->setWorldTransform(TargetB); if(auto* MS=RB.Body->getMotionState()) MS->setWorldTransform(TargetB); RB.Body->clearForces(); RB.Body->setLinearVelocity(btVector3(0,0,0)); RB.Body->setAngularVelocity(btVector3(0,0,0)); RB.Body->activate(true);} bFirstSyncDone=true; }
    PreSyncKinematicFromBones(InOutBoneWorldUE); StepSimulationMMD(DeltaSeconds); PostSyncBonesFromPhysics(InOutBoneWorldUE);
}

void FMMDPhysicsSimulator::DebugDraw()
{
    if (CVarMMDPhysDebug->GetInt() == 0) return;
    USkeletalMeshComponent* SkelComp = OwnerSkelComp.Get();
    if (!SkelComp) return;
    UWorld* World = SkelComp->GetWorld();
    if (!World) return;

    const float Thickness = 1.5f;
    const bool bPersistent = true;
    const float LifeTime = 0.f;

    for (const BulletRigidBody& RB : BulletRigidBodies)
    {
        if (!RB.Body) continue;
        const btCollisionShape* Shape = RB.Body->getCollisionShape();
        if (!Shape) continue;

        const btTransform WorldB = RB.Body->getWorldTransform();
        const FTransform WorldUE = BulletToUE_Transform_YUp(WorldB, UnitScale);
        const FVector Center = WorldUE.GetLocation();
        const FQuat   Rot = WorldUE.GetRotation();
        const bool bKinematic = (RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT) != 0;
        const bool bDynamic   = RB.Body->getInvMass() > 0;
        const FColor Color = bKinematic ? FColor(255,128,0) : (bDynamic ? FColor(0,255,255) : FColor(200,200,200));

        switch (Shape->getShapeType())
        {
        case SPHERE_SHAPE_PROXYTYPE:
        {
            const btSphereShape* S = static_cast<const btSphereShape*>(Shape);
            const float R = (float)S->getRadius() * UnitScale;
            DrawDebugSphere(World, Center, R, 24, Color, bPersistent, LifeTime, 0, Thickness);
            break;
        }
        case BOX_SHAPE_PROXYTYPE:
        {
            const btBoxShape* B = static_cast<const btBoxShape*>(Shape);
            const btVector3 He = B->getHalfExtentsWithMargin();
            const FVector Extents((float)He.getX() * UnitScale, (float)He.getY() * UnitScale, (float)He.getZ() * UnitScale);
            DrawDebugBox(World, Center, Extents, Rot, Color, bPersistent, LifeTime, 0, Thickness);
            break;
        }
        case CAPSULE_SHAPE_PROXYTYPE:
        {
            const btCapsuleShape* C = static_cast<const btCapsuleShape*>(Shape);
            const float Radius = (float)C->getRadius() * UnitScale;
            const float HalfH  = (float)C->getHalfHeight() * UnitScale;
            DrawDebugCapsule(World, Center, HalfH, Radius, Rot, Color, bPersistent, LifeTime, 0, Thickness);
            break;
        }
        default:
        {
            DrawDebugCoordinateSystem(World, Center, Rot.Rotator(), 8.f * UnitScale, bPersistent, LifeTime, 0, Thickness);
            break;
        }
        }

        DrawDebugCoordinateSystem(World, Center, Rot.Rotator(), 4.f * UnitScale, bPersistent, LifeTime, 0, 1.0f);
    }

    for (btTypedConstraint* TC : BulletJoints)
    {
        if (!TC) continue;
        btGeneric6DofSpring2Constraint* C = static_cast<btGeneric6DofSpring2Constraint*>(TC);

        const btTransform AWorld = C->getRigidBodyA().getWorldTransform();
        const btTransform BWorld = C->getRigidBodyB().getWorldTransform();
        const btTransform FA = C->getFrameOffsetA();
        const btTransform FB = C->getFrameOffsetB();

        const btTransform AnchorA_B = AWorld * FA;
        const btTransform AnchorB_B = BWorld * FB;

        const FTransform AnchorA_UE = BulletToUE_Transform_YUp(AnchorA_B, UnitScale);
        const FTransform AnchorB_UE = BulletToUE_Transform_YUp(AnchorB_B, UnitScale);

        const FVector PA = AnchorA_UE.GetLocation();
        const FVector PB = AnchorB_UE.GetLocation();

        DrawDebugLine(World, PA, PB, FColor::Green, bPersistent, LifeTime, 0, Thickness);
        DrawDebugPoint(World, PA, 6.f, FColor::Green, bPersistent, LifeTime, 0);
        DrawDebugPoint(World, PB, 6.f, FColor::Green, bPersistent, LifeTime, 0);

        DrawDebugCoordinateSystem(World, PA, AnchorA_UE.GetRotation().Rotator(), 4.f * UnitScale, bPersistent, LifeTime, 0, 1.0f);
        DrawDebugCoordinateSystem(World, PB, AnchorB_UE.GetRotation().Rotator(), 4.f * UnitScale, bPersistent, LifeTime, 0, 1.0f);
    }
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
    if(DynamicsWorld)
    {
        for(btGeneric6DofSpring2Constraint* C: BulletJoints){ if(C){ DynamicsWorld->removeConstraint(C); delete C; }}
    }
    BulletJoints.Empty();
    if(DynamicsWorld)
    {
        for(BulletRigidBody& RB: BulletRigidBodies)
        {
            if(RB.Body){ DynamicsWorld->removeRigidBody(RB.Body); delete RB.Body; }
            if(RB.MotionState){ delete RB.MotionState; }
            if(RB.Shape){ delete RB.Shape; }
        }
    }
    BulletRigidBodies.Empty();
    if(DynamicsWorld){ delete DynamicsWorld; DynamicsWorld=nullptr; }
    if(Solver){ delete Solver; Solver=nullptr; }
    if(Broadphase){ delete Broadphase; Broadphase=nullptr; }
    if(Dispatcher){ delete Dispatcher; Dispatcher=nullptr; }
    if(CollisionConfiguration){ delete CollisionConfiguration; CollisionConfiguration=nullptr; }
    bInitialized=false; bFirstSyncDone=false;
}
