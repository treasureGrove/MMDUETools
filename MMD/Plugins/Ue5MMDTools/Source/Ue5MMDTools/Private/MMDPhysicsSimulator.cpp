#include "MMDPhysicsSimulator.h"
#include "TPMXParser.h"
#include <btBulletDynamicsCommon.h>

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

    // 位置： (x,y,z)_UE = (z, -x, y) 反解 -> (xB,yB,zB) = (-yUE, zUE, xUE) / s
    const FVector p = TUE.GetLocation();
    TB.setOrigin(btVector3(-p.Y / UnitScale, p.Z / UnitScale, p.X / UnitScale));

    // 旋转： R_b = C^T R_ue C
    const FMatrix MUE = FRotationMatrix::Make(TUE.GetRotation());
    const btMatrix3x3 RUE(
        (btScalar)MUE.M[0][0], (btScalar)MUE.M[0][1], (btScalar)MUE.M[0][2],
        (btScalar)MUE.M[1][0], (btScalar)MUE.M[1][1], (btScalar)MUE.M[1][2],
        (btScalar)MUE.M[2][0], (btScalar)MUE.M[2][1], (btScalar)MUE.M[2][2]
    );
    const btMatrix3x3 C = Bullet_C_YUp();
    const btMatrix3x3 RB = C.transpose() * RUE * C;

    btQuaternion qB; RB.getRotation(qB);
    TB.setRotation(qB);
    return TB;
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

    return FTransform(qUE, pUE, FVector(1, 1, 1));
}

// 你现有的 PMX->UE（位置+欧拉）保持不变（单位 8）
static inline FTransform ConvertPMXPositionRotationToUnrealFTransform(
    const FVector& PMXPosition,
    const FVector& PMXRotationRad,
    float UnitScale = 8.0f
)
{
    const FVector UEPos(PMXPosition.X * UnitScale, -PMXPosition.Z * UnitScale, PMXPosition.Y * UnitScale);

    const double sx = FMath::Sin(PMXRotationRad.X);
    const double cx = FMath::Cos(PMXRotationRad.X);
    const double sy = FMath::Sin(PMXRotationRad.Y);
    const double cy = FMath::Cos(PMXRotationRad.Y);
    const double sz = FMath::Sin(PMXRotationRad.Z);
    const double cz = FMath::Cos(PMXRotationRad.Z);

    // Z->Y->X：Rz * Ry * Rx
    const double Rx[3][3] = { {1,0,0},{0,cx,-sx},{0,sx,cx} };
    const double Ry[3][3] = { {cy,0,sy},{0,1,0},{-sy,0,cy} };
    const double Rz[3][3] = { {cz,-sz,0},{sz,cz,0},{0,0,1} };
    auto Mul33 = [&](const double A[3][3], const double B[3][3], double O[3][3]) {
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 3; ++c)
            O[r][c] = A[r][0] * B[0][c] + A[r][1] * B[1][c] + A[r][2] * B[2][c];
        };
    double Rzy[3][3]; Mul33(Rz, Ry, Rzy);
    double Rpmx[3][3]; Mul33(Rzy, Rx, Rpmx);

    // 基变换 C: (x,y,z)_UE = (x,-z,y) -> C = [1 0 0; 0 0 -1; 0 1 0]
    const double C[3][3] = { {1,0,0},{0,0,-1},{0,1,0} };
    const double CT[3][3] = { {1,0,0},{0,0, 1},{0,-1,0} };
    double T[3][3];  Mul33(C, Rpmx, T);
    double Rue[3][3]; Mul33(T, CT, Rue);

    FMatrix M = FMatrix::Identity;
    M.M[0][0] = (float)Rue[0][0]; M.M[0][1] = (float)Rue[0][1]; M.M[0][2] = (float)Rue[0][2];
    M.M[1][0] = (float)Rue[1][0]; M.M[1][1] = (float)Rue[1][1]; M.M[1][2] = (float)Rue[1][2];
    M.M[2][0] = (float)Rue[2][0]; M.M[2][1] = (float)Rue[2][1]; M.M[2][2] = (float)Rue[2][2];
    const FQuat UERot = FQuat(M);

    return FTransform(UERot, UEPos, FVector(1, 1, 1));
}

// ============ 工厂：形状 / 起始 MotionState（返回指针） ============
static inline btCollisionShape* CreateCollisionShape(const PMXRigid& Rigid)
{
    switch (Rigid.ShapeType)
    {
    case 0: return new btSphereShape((btScalar)Rigid.Size.X);
    case 1: return new btBoxShape(btVector3(
        (btScalar)Rigid.Size.X * 0.5f,
        (btScalar)Rigid.Size.Y * 0.5f,
        (btScalar)Rigid.Size.Z * 0.5f));
    case 2: return new btCapsuleShape((btScalar)Rigid.Size.X, (btScalar)Rigid.Size.Y); // Y 轴胶囊
    default:
        UE_LOG(LogTemp, Error, TEXT("未知形状类型: %d"), Rigid.ShapeType);
        return new btBoxShape(btVector3(0.05f, 0.05f, 0.05f));
    }
}

// 由 PMX 局部偏移构造 Bullet 起始世界变换（未乘骨骼；如需乘骨骼，请在外层 BoneWorldUE * LocalUE）
static inline btTransform MakeBulletStartTransformFromPMX(const PMXRigid& Rigid, float UnitScale = 8.f)
{
    const FTransform LocalUE = ConvertPMXPositionRotationToUnrealFTransform(Rigid.Position, Rigid.Rotation, UnitScale);
    return UEToBullet_Transform_YUp(LocalUE, UnitScale);
}

static inline btDefaultMotionState* CreateMotionState(const PMXRigid& Rigid, float UnitScale = 8.f)
{
    const btTransform StartB = MakeBulletStartTransformFromPMX(Rigid, UnitScale);
    return new btDefaultMotionState(StartB);
}
static inline btRigidBody& GetBulletFixedBody()
{
    // 质量=0、无 MotionState、无形状的固定刚体。用于“连接到世界”的约束端点。
    static btRigidBody sFixedBody(0.f, nullptr, nullptr);
    return sFixedBody;
}
#pragma endregion

// ============ 坐标/变换工具（Y-Up Bullet <-> UE，单位缩放默认 8，与 PMX->UE 保持一致） ============


bool FMMDPhysicsSimulator::InitializeFromPMX(const PMXDatas& PMXData, USkeletalMeshComponent* InSkelComp, float InUnitScale, int32 InMaxSubSteps, float InFixedTimeStep)
{
    if (!InSkelComp)
    {
        UE_LOG(LogTemp, Error, TEXT("InitializeFromPMX failed: SkeletalMeshComponent null"));
        return false;
    }

    if (bInitialized)
    {
        UE_LOG(LogTemp, Warning, TEXT("InitializeFromPMX skipped: already initialized"));
        return true;
    }
    UnitScale = InUnitScale;
    MaxSubSteps = InMaxSubSteps;
    FixedTimeStep = InFixedTimeStep;
    OwnerSkelComp = InSkelComp;
    InitializeBulletWorld();
	InitializeRigidBody(PMXData);
	InitializeJoints(PMXData);

    bInitialized = true;
	return true;
}


void FMMDPhysicsSimulator::InitializeBulletWorld()
{
    CollisionConfiguration = new btDefaultCollisionConfiguration();

    Dispatcher = new btCollisionDispatcher(CollisionConfiguration);

    Broadphase = new btDbvtBroadphase();

    Solver = new btSequentialImpulseConstraintSolver();

    DynamicsWorld = new btDiscreteDynamicsWorld(Dispatcher, Broadphase, Solver, CollisionConfiguration);

	DynamicsWorld->setGravity(btVector3(0, -9.81f, 0));
    btContactSolverInfo& SI = DynamicsWorld->getSolverInfo();
    SI.m_numIterations = 15;
    SI.m_splitImpulse = 1;
    SI.m_splitImpulsePenetrationThreshold = -0.02f;
    SI.m_erp = 0.2f;
    SI.m_erp2 = 0.1f;
    SI.m_globalCfm = 0.f;

    SI.m_solverMode |= SOLVER_USE_WARMSTARTING;
    SI.m_solverMode |= SOLVER_USE_2_FRICTION_DIRECTIONS;
    SI.m_solverMode |= SOLVER_ENABLE_FRICTION_DIRECTION_CACHING;
    SI.m_solverMode |= SOLVER_RANDMIZE_ORDER;
}

// ============ 主初始化：修正版 ============
void FMMDPhysicsSimulator::InitializeRigidBody(const PMXDatas PMXData)
{
    if (!DynamicsWorld) return;
    int i = 0;
    for (const PMXRigid& Rigid : PMXData.ModelRigids)
    {
        BulletRigidBody NewRigidBody;

        // 1) 形状 + MotionState（必须 new）
        NewRigidBody.Shape = CreateCollisionShape(Rigid);
        NewRigidBody.MotionState = CreateMotionState(Rigid, /*UnitScale*/8.f);

        // 2) 质量/惯性（Mode 0 强制 0）
        btScalar Mass = (Rigid.PhysicsMode == 0) ? btScalar(0) : btScalar(FMath::Max(Rigid.Mass, 0.0001f));
        btVector3 Inertia(0, 0, 0);
        if (Mass > 0) NewRigidBody.Shape->calculateLocalInertia(Mass, Inertia);

        // 3) 刚体构造
        btRigidBody::btRigidBodyConstructionInfo CI(Mass, NewRigidBody.MotionState, NewRigidBody.Shape, Inertia);
        CI.m_friction = (btScalar)Rigid.Friction;
        CI.m_restitution = (btScalar)Rigid.Restitution;
        CI.m_linearDamping = (btScalar)Rigid.LinearDamping;
        CI.m_angularDamping = (btScalar)Rigid.AngularDamping;

        NewRigidBody.Body = new btRigidBody(CI);

        // 4) 三种模式的差异设置
        switch (Rigid.PhysicsMode)
        {
        case 0: // Bone / Kinematic
            NewRigidBody.Body->setCollisionFlags(NewRigidBody.Body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
            NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);
            break;

        case 1: // Pure Dynamic
            NewRigidBody.Body->setActivationState(ACTIVE_TAG); // 允许睡眠即可
            break;

        case 2: // Physics + Bone（动态体；是否禁睡视伺服/弹簧实现而定）
            NewRigidBody.Body->setActivationState(ACTIVE_TAG);
            // 如果你采用“锚点/伺服”并希望始终响应，可改为：
            // NewRigidBody.Body->setActivationState(DISABLE_DEACTIVATION);
            break;

        default:
            break;
        }

        // 5) 过滤与加入世界
        NewRigidBody.CollisionGroup = Rigid.Group;
        NewRigidBody.CollisionMask = Rigid.CollisionMask;
        DynamicsWorld->addRigidBody(NewRigidBody.Body, NewRigidBody.CollisionGroup, NewRigidBody.CollisionMask);

        // 6) 缓存变换（与 MotionState 起始一致）
        btTransform StartB;
        if (NewRigidBody.Body->getMotionState())
            NewRigidBody.Body->getMotionState()->getWorldTransform(StartB);
        else
            StartB = NewRigidBody.Body->getWorldTransform();

        NewRigidBody.BulletWorldTransform = StartB;
        NewRigidBody.PrevBulletWorldTransform = StartB;
        NewRigidBody.UEWorldTransform = BulletToUE_Transform_YUp(StartB, 8.f);
        NewRigidBody.PrevUEWorldTransform = NewRigidBody.UEWorldTransform;

        // 7) 骨骼关联与相对偏移（UE 空间）
        NewRigidBody.RelatedBoneIndex = Rigid.RelatedBoneIndex;
        NewRigidBody.BoneToRigid = ConvertPMXPositionRotationToUnrealFTransform(Rigid.Position, Rigid.Rotation, 8.f);
        NewRigidBody.RigidToBone = NewRigidBody.BoneToRigid.Inverse();

        NewRigidBody.DebugID = i++;

        // TODO: 保存 NewRigidBody 到你的容器，并在清理阶段 delete Body/MotionState/Shape
		BulletRigidBodies.Add(NewRigidBody);
    }
}

// ... 你的文件其余内容保持不变，仅示意关键改动处 ...

void FMMDPhysicsSimulator::InitializeJoints(const PMXDatas PMXData)
{
    if (!DynamicsWorld) return;


    for (const PMXJoint& Joint : PMXData.ModelJoints)
    {
        // 支持 -1 连接到世界
        auto GetBodyByIndex = [&](int32 Index)->btRigidBody*
            {
                auto GetBodyByIndex = [&](int32 Index)->btRigidBody*
                    {
                        if (Index < 0) return &GetBulletFixedBody();   // 替代 btRigidBody::getFixedBody()
                        if (!BulletRigidBodies.IsValidIndex(Index)) return nullptr;
                        return BulletRigidBodies[Index].Body;
                    };
                if (!BulletRigidBodies.IsValidIndex(Index)) return nullptr;
                return BulletRigidBodies[Index].Body;
            };

        btRigidBody* BodyA = GetBodyByIndex(Joint.RigidA);
        btRigidBody* BodyB = GetBodyByIndex(Joint.RigidB);
        if (!BodyA || !BodyB)
        {
            UE_LOG(LogTemp, Error, TEXT("Joint references invalid/null rigid bodies: %d, %d"), Joint.RigidA, Joint.RigidB);
            continue;
        }

        // Joint 世界姿态：PMX -> UE(*8) -> Bullet(/8)
        const FTransform JointUE = ConvertPMXPositionRotationToUnrealFTransform(Joint.Position, Joint.Rotation, UnitScale);
        const btTransform JointB = UEToBullet_Transform_YUp(JointUE, UnitScale);

        const btTransform AWorld = BodyA->getWorldTransform();
        const btTransform BWorld = BodyB->getWorldTransform();

        const btTransform FrameInA = AWorld.inverse() * JointB;
        const btTransform FrameInB = BWorld.inverse() * JointB;

        auto* Constraint = new btGeneric6DofSpring2Constraint(*BodyA, *BodyB, FrameInA, FrameInB, RO_XYZ);

        // 线性限位（PMX 自由轴：lower > upper）
        btVector3 LinLo(Joint.LimitPosLower.X, Joint.LimitPosLower.Y, Joint.LimitPosLower.Z);
        btVector3 LinHi(Joint.LimitPosUpper.X, Joint.LimitPosUpper.Y, Joint.LimitPosUpper.Z);
        for (int a = 0; a < 3; ++a)
            if (LinLo[a] > LinHi[a]) { LinLo[a] = -BT_LARGE_FLOAT; LinHi[a] = BT_LARGE_FLOAT; }
        Constraint->setLinearLowerLimit(LinLo);
        Constraint->setLinearUpperLimit(LinHi);

        // 角度限位（弧度；自由轴：[-pi, pi]）
        btVector3 AngLo(Joint.LimitRotLower.X, Joint.LimitRotLower.Y, Joint.LimitRotLower.Z);
        btVector3 AngHi(Joint.LimitRotUpper.X, Joint.LimitRotUpper.Y, Joint.LimitRotUpper.Z);
        for (int a = 0; a < 3; ++a)
            if (AngLo[a] > AngHi[a]) { AngLo[a] = -SIMD_PI; AngHi[a] = SIMD_PI; }
        Constraint->setAngularLowerLimit(AngLo);
        Constraint->setAngularUpperLimit(AngHi);

        // 弹簧与阻尼（PMX 的 SpringPos/SpringRot → 刚度；阻尼先用 0.6）
        for (int axis = 0; axis < 3; ++axis)
        {
            const float KLin = Joint.SpringPos[axis];
            Constraint->enableSpring(axis, KLin > 0.f);
            if (KLin > 0.f) { Constraint->setStiffness(axis, KLin); Constraint->setDamping(axis, 0.6f); }

            const int AngAxis = 3 + axis;
            const float KAng = Joint.SpringRot[axis];
            Constraint->enableSpring(AngAxis, KAng > 0.f);
            if (KAng > 0.f) { Constraint->setStiffness(AngAxis, KAng); Constraint->setDamping(AngAxis, 0.6f); }
        }

        // 设平衡点（重要）
        Constraint->setEquilibriumPoint();

        // 约束 STOP_ERP（贴近 MMD/Ammo，提升“停靠”稳定度）
        for (int i = 0; i < 6; ++i)
            Constraint->setParam(BT_CONSTRAINT_STOP_ERP, 0.475f, i);

        // 加入世界并禁用 A 与 B 的相互碰撞
        DynamicsWorld->addConstraint(Constraint, true);

        // 保存指针（注意容器类型为 TArray<btTypedConstraint*> 或 TArray<btGeneric6DofSpring2Constraint*>）
        BulletJoints.Add(Constraint);
    }
}

void FMMDPhysicsSimulator::PreSyncKinematicFromBones(const TArray<FTransform>& BoneWorldUE)
{
    if (!DynamicsWorld) return;


    for (BulletRigidBody& RB : BulletRigidBodies)
    {
        if (!RB.Body) continue;

        const bool bKinematic =
            (RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT) != 0;
        if (!bKinematic) continue;

        const int32 BoneIdx = RB.RelatedBoneIndex;
        if (BoneIdx < 0 || !BoneWorldUE.IsValidIndex(BoneIdx)) continue;

        // 目标姿态（UE -> Bullet）
        const FTransform TargetUE = BoneWorldUE[BoneIdx] * RB.BoneToRigid;
        const btTransform TargetB = UEToBullet_Transform_YUp(TargetUE, UnitScale);

        // 上一帧（作为插值/速度参考）
        const btTransform PrevB = RB.Body->getWorldTransform();

        // 告知 Bullet：上一帧的插值姿态（用于计算 kinematic 速度）
        RB.Body->setInterpolationWorldTransform(PrevB);

        // 更新到当前姿态
        RB.Body->setWorldTransform(TargetB);
        if (auto* MS = RB.Body->getMotionState())
            MS->setWorldTransform(TargetB);

        // 可选：估算并设置“插值速度”，有助于接触/摩擦更准确
        if (FixedTimeStep > 0.f)
        {
            const btVector3 dv = (TargetB.getOrigin() - PrevB.getOrigin()) / FixedTimeStep;
            RB.Body->setInterpolationLinearVelocity(dv);

            // 简易角速度估算（可选）
            // btTransformUtil::calculateVelocity(PrevB, TargetB, FixedTimeStep, angVelOut) 也可用
            btQuaternion qa, qb;
            PrevB.getBasis().getRotation(qa);
            TargetB.getBasis().getRotation(qb);
            btQuaternion dq = qb * qa.inverse();
            dq.normalize();
            btVector3 axis;
            btScalar angle = dq.getAngle();
            if (angle > SIMD_EPSILON) {
                axis = btVector3(dq.getX(), dq.getY(), dq.getZ()).normalized();
                RB.Body->setInterpolationAngularVelocity(axis * (angle / FixedTimeStep));
            }
            else {
                RB.Body->setInterpolationAngularVelocity(btVector3(0, 0, 0));
            }
        }

        // 确保不睡眠
        RB.Body->activate(true);
    }
}
void FMMDPhysicsSimulator::StepSimulationMMD(float DeltaSeconds)
{
    if (!DynamicsWorld) return;
    DynamicsWorld->stepSimulation(DeltaSeconds, MaxSubSteps, FixedTimeStep);
}



void FMMDPhysicsSimulator::PostSyncBonesFromPhysics(TArray<FTransform>& InOutBoneWorldUE)
{
    if (!DynamicsWorld) return;

    for (const BulletRigidBody& RB : BulletRigidBodies)
    {
        if (!RB.Body) continue;

        const int32 BoneIdx = RB.RelatedBoneIndex;
        if (BoneIdx < 0 || !InOutBoneWorldUE.IsValidIndex(BoneIdx)) continue;

        const bool bKinematic =
            (RB.Body->getCollisionFlags() & btCollisionObject::CF_KINEMATIC_OBJECT) != 0;

        // 仅回写“非 Kinematic”的刚体。
        // 如果你在 BulletRigidBody 里缓存了 PMX 的 PhysicsMode，可进一步限定为 RB.PhysicsMode == 2。
        if (bKinematic) continue;

        const btTransform WorldB = RB.Body->getWorldTransform();
        const FTransform WorldUE = BulletToUE_Transform_YUp(WorldB, UnitScale);

        // 骨骼世界 = 刚体世界 * RigidToBone（把刚体姿态转换回骨骼姿态）
        const FTransform BoneUE = WorldUE * RB.RigidToBone;
        InOutBoneWorldUE[BoneIdx] = BoneUE;
    }
}

void FMMDPhysicsSimulator::TickMMDPhysics(float DeltaSeconds, TArray<FTransform>& InOutBoneWorldUE)
{

    // 1) 让 Mode 0（Kinematic）刚体对齐当前骨骼
    PreSyncKinematicFromBones(InOutBoneWorldUE);

    // 2) 物理步进
    StepSimulationMMD(DeltaSeconds);

    // 3) 把物理结果写回骨骼（通常对应 Mode 2）
    PostSyncBonesFromPhysics(InOutBoneWorldUE);
}
