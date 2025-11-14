#include "MMDPhysicsSimulator.h"
#include "btBulletDynamicsCommon.h"

FMMDPhysicsSimulator::FMMDPhysicsSimulator()
    : DynamicsWorld(nullptr)
    , Broadphase(nullptr)
    , CollisionConfig(nullptr)
    , Dispatcher(nullptr)
    , Solver(nullptr)
    , bInitialized(false)
{
}
FMMDPhysicsSimulator::~FMMDPhysicsSimulator()
{
	DestroyBulletWorld();
}
void FMMDPhysicsSimulator::InitializeBulletWorld()
{
    if (bInitialized)
        return;

    Broadphase = new btDbvtBroadphase();
    CollisionConfig = new btDefaultCollisionConfiguration();
    Dispatcher = new btCollisionDispatcher(CollisionConfig);
    Solver = new btSequentialImpulseConstraintSolver();
    DynamicsWorld = new btDiscreteDynamicsWorld(Dispatcher, Broadphase, Solver, CollisionConfig);
    DynamicsWorld->setGravity(btVector3(0, 0, -9.8f));
    bInitialized = true;
}
void FMMDPhysicsSimulator::DestroyBulletWorld()
{
    if (!bInitialized) return;

    // 先清理所有约束，避免泄露
    if (DynamicsWorld)
    {
        for (int32 i = DynamicsWorld->getNumConstraints() - 1; i >= 0; --i)
        {
            btTypedConstraint* C = DynamicsWorld->getConstraint(i);
            DynamicsWorld->removeConstraint(C);
        }
    }
    for (btTypedConstraint* C : CreatedConstraints)
    {
        delete C;
    }
    CreatedConstraints.Empty();

    // 再清理刚体（保持你原有逻辑）
    for (int i = DynamicsWorld->getNumCollisionObjects() - 1; i >= 0; i--)
    {
        btCollisionObject* obj = DynamicsWorld->getCollisionObjectArray()[i];
        btRigidBody* body = btRigidBody::upcast(obj);
        if (body && body->getMotionState())
        {
            delete body->getMotionState();
        }
        DynamicsWorld->removeCollisionObject(obj);
        delete obj;
    }

    delete DynamicsWorld;
    delete Solver;
    delete Dispatcher;
    delete CollisionConfig;
    delete Broadphase;

    DynamicsWorld = nullptr;
    Solver = nullptr;
    Dispatcher = nullptr;
    CollisionConfig = nullptr;
    Broadphase = nullptr;

    bInitialized = false;
    UE_LOG(LogTemp, Log, TEXT("MMD Bullet World destroyed"));
}

btVector3 FMMDPhysicsSimulator::UEToBullet(const FVector& UEVec)
{
    // UE5使用左手坐标系，Bullet使用右手坐标系
    // UE5: X=Forward, Y=Right, Z=Up
    // Bullet: X=Right, Y=Up, Z=Forward
    return btVector3(UEVec.Y / 100.0f, UEVec.Z / 100.0f, UEVec.X / 100.0f);
}
FVector FMMDPhysicsSimulator::BulletToUE(const btVector3& BtVec)
{
    return FVector(BtVec.z() * 100.0f, BtVec.x() * 100.0f, BtVec.y() * 100.0f);
}
btQuaternion FMMDPhysicsSimulator::UEToBullet(const FQuat& UEQuat)
{
    // 四元数坐标系转换
    return btQuaternion(UEQuat.Y, UEQuat.Z, UEQuat.X, UEQuat.W);
}

FQuat FMMDPhysicsSimulator::BulletToUE(const btQuaternion& BtQuat)
{
    // 反向转换：Bullet四元数 -> UE5四元数
    return FQuat(BtQuat.z(), BtQuat.x(), BtQuat.y(), BtQuat.w());
}

btCollisionShape* FMMDPhysicsSimulator::CreateCollisionShape(const FMMDRigidBodyRuntime& RigidData)
{
    btCollisionShape* Shape = nullptr;

    switch (RigidData.ShapeType)
    {
    case 0: // 球体
    {
        const float Diameter = FMath::Max(RigidData.Size.X, 0.1f);
        const float RadiusM = (Diameter * 0.5f) / 100.0f;
        Shape = new btSphereShape(RadiusM);
    }
    break;

    case 1: // 盒子
    {
        const btVector3 HalfExtentsM(
            FMath::Max(RigidData.Size.X, 0.1f) * 0.5f / 100.0f,
            FMath::Max(RigidData.Size.Y, 0.1f) * 0.5f / 100.0f,
            FMath::Max(RigidData.Size.Z, 0.1f) * 0.5f / 100.0f
        );
        Shape = new btBoxShape(HalfExtentsM);
    }
    break;

    case 2: // 胶囊
    {
        const float Diameter = FMath::Max(RigidData.Size.X, 0.1f);
        const float TotalH = FMath::Max(RigidData.Size.Y, 0.2f);
        const float RadiusM = (Diameter * 0.5f) / 100.0f;
        float HeightM = TotalH / 100.0f - 2.0f * RadiusM;
        if (HeightM < 0.0f) HeightM = 0.0f;
        Shape = new btCapsuleShape(RadiusM, HeightM);
    }
    break;

    default:
        // 默认使用球体
        Shape = new btSphereShape(0.05f); // 5cm
        UE_LOG(LogTemp, Warning, TEXT("Unknown MMD shape type %d, using sphere"), RigidData.ShapeType);
        break;
    }

    return Shape;
}
void FMMDPhysicsSimulator::InitializeRigidBodies(TArray<FMMDRigidBodyRuntime>& RigidBodies,FComponentSpacePoseContext& Output)
{
    CreatedBodies.Empty();
    CreatedShapes.Empty();

    UE_LOG(LogTemp, Log, TEXT("Initializing %d MMD rigid bodies"), RigidBodies.Num());
    FCSPose<FCompactPose>& CSPose = Output.Pose;

    for (FMMDRigidBodyRuntime& RigidData : RigidBodies)
    {
        if (!RigidData.CompactBoneIndex.IsValid())
            continue;

        // 骨骼世界
        const FTransform BoneWorld = CSPose.GetComponentSpaceTransform(RigidData.CompactBoneIndex);
        // PMX 弧度->度
        const FVector RotDeg = FMath::RadiansToDegrees(RigidData.Rotation);
        const FRotator LocalRot(RotDeg.Y, RotDeg.Z, RotDeg.X);
        const FTransform LocalOffset(LocalRot, RigidData.Position);
        RigidData.RigidBodyOffset = LocalOffset;

        // 刚体世界初始
        const FTransform RigidWorld = BoneWorld * LocalOffset;
        RigidData.PrevPosition = RigidWorld.GetLocation();
        RigidData.PrevRotation = RigidWorld.GetRotation();

        // Bullet Transform
        btTransform BtStart; BtStart.setIdentity();
        BtStart.setOrigin(UEToBullet(RigidData.PrevPosition));
        BtStart.setRotation(UEToBullet(RigidData.PrevRotation));

        // 形状与质量/惯性
        btCollisionShape* Shape = CreateCollisionShape(RigidData);
        CreatedShapes.Add(Shape);

        const bool bMode0 = (RigidData.PhysicsMode == 0);
        float Mass = bMode0 ? 0.0f : FMath::Max(RigidData.Mass, 0.0f);

        btVector3 Inertia(0, 0, 0);
        if (Mass > 0.0f)
            Shape->calculateLocalInertia(Mass, Inertia);

        btDefaultMotionState* MotionState = new btDefaultMotionState(BtStart);
        btRigidBody::btRigidBodyConstructionInfo CI(Mass, MotionState, Shape, Inertia);
        CI.m_friction = RigidData.Friction;
        CI.m_restitution = RigidData.Restitution;
        CI.m_linearDamping = RigidData.LinearDamping;
        CI.m_angularDamping = RigidData.AngularDamping;

        btRigidBody* Body = new btRigidBody(CI);

        if (bMode0)
        {
            // Kinematic，用于作为碰撞屏障
            Body->setCollisionFlags(Body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
            Body->setActivationState(DISABLE_DEACTIVATION);
        }
        else
        {
            // 动态刚体保持活跃；Mode2 后续会施加追随力
            Body->setActivationState(DISABLE_DEACTIVATION);
        }

        // CCD 可选
        Body->setCcdMotionThreshold(Shape->getMargin());
        Body->setCcdSweptSphereRadius(Shape->getMargin() * 0.9f);

        int32 GroupBit = 1 << FMath::Clamp<int32>(RigidData.Group, 0, 15);
        DynamicsWorld->addRigidBody(Body, GroupBit, RigidData.CollisionMask);

        CreatedBodies.Add(Body);
        RigidData.BulletBody = Body;
        RigidData.InvMass = (Mass > 0.0f) ? 1.0f / Mass : 0.0f;
    }
}
void FMMDPhysicsSimulator::InitializeJoints(TArray<FMMDJointRuntime>& Joints, const TArray<FMMDRigidBodyRuntime>& RigidBodies)
{
    if (!DynamicsWorld) return;

    int32 Created = 0;
    for (FMMDJointRuntime& J : Joints)
    {
        if (J.BulletConstraint) continue; // 已创建过
        if (!RigidBodies.IsValidIndex(J.RigidA) || !RigidBodies.IsValidIndex(J.RigidB)) continue;

        const FMMDRigidBodyRuntime& A = RigidBodies[J.RigidA];
        const FMMDRigidBodyRuntime& B = RigidBodies[J.RigidB];
        if (!A.BulletBody || !B.BulletBody) continue;

        // 关节世界锚点（PMX 给的是模型空间/世界空间，这里按“组件空间”使用，已与骨骼对齐）
        btTransform WorldFrame; WorldFrame.setIdentity();
        WorldFrame.setOrigin(UEToBullet(J.Position));

        // 旋转（弧度 -> 度 -> 四元数），并做与 UE->Bullet 一致的轴重排
        const FRotator RotDeg(
            FMath::RadiansToDegrees(J.Rotation.Y),
            FMath::RadiansToDegrees(J.Rotation.Z),
            FMath::RadiansToDegrees(J.Rotation.X)
        );
        WorldFrame.setRotation(UEToBullet(RotDeg.Quaternion()));

        // 计算 frameInA / frameInB（世界 -> 刚体局部）
        const btTransform InvA = A.BulletBody->getWorldTransform().inverse();
        const btTransform InvB = B.BulletBody->getWorldTransform().inverse();
        const btTransform FrameInA = InvA * WorldFrame;
        const btTransform FrameInB = InvB * WorldFrame;

        // 使用 6DoF 弹簧约束（与 PMX 语义匹配）
        auto* Constraint = new btGeneric6DofSpringConstraint(*A.BulletBody, *B.BulletBody, FrameInA, FrameInB, true);

        // 平移限制（厘米 -> 米）
        Constraint->setLimit(0, J.LimitPosLower.X / 100.f, J.LimitPosUpper.X / 100.f);
        Constraint->setLimit(1, J.LimitPosLower.Y / 100.f, J.LimitPosUpper.Y / 100.f);
        Constraint->setLimit(2, J.LimitPosLower.Z / 100.f, J.LimitPosUpper.Z / 100.f);
        // 旋转限制（弧度）
        Constraint->setLimit(3, J.LimitRotLower.X, J.LimitRotUpper.X);
        Constraint->setLimit(4, J.LimitRotLower.Y, J.LimitRotUpper.Y);
        Constraint->setLimit(5, J.LimitRotLower.Z, J.LimitRotUpper.Z);

        // 线性弹簧
        for (int axis = 0; axis < 3; ++axis)
        {
            const float K = J.SpringPos[axis];
            if (K > 0.f)
            {
                Constraint->enableSpring(axis, true);
                Constraint->setStiffness(axis, K);
                Constraint->setDamping(axis, 0.25f); // 经验阻尼，避免发散
            }
        }
        // 角度弹簧
        for (int axis = 0; axis < 3; ++axis)
        {
            const int rotAxis = 3 + axis;
            const float K = J.SpringRot[axis];
            if (K > 0.f)
            {
                Constraint->enableSpring(rotAxis, true);
                Constraint->setStiffness(rotAxis, K);
                Constraint->setDamping(rotAxis, 0.3f);
            }
        }

        DynamicsWorld->addConstraint(Constraint, true /*disableCollisionsBetweenLinkedBodies*/);
        J.BulletConstraint = Constraint;
        J.bConstraintInitialized = true;
        CreatedConstraints.Add(Constraint);
        ++Created;
    }

    if (Created > 0)
    {
        UE_LOG(LogTemp, Log, TEXT("Initialized %d MMD joints (6DoF Spring)"), Created);
    }
}
void FMMDPhysicsSimulator::UpdateRigidBodiesFromBones(TArray<FMMDRigidBodyRuntime>& RigidBodies, FComponentSpacePoseContext& Output)
{
    float StepDt = 1.0f / 60.0f;
    for (FMMDRigidBodyRuntime& Rigid : RigidBodies)
    {
        if (!Rigid.BulletBody || !Rigid.CompactBoneIndex.IsValid())
            continue;

        const FTransform BoneWorld = Output.Pose.GetComponentSpaceTransform(Rigid.CompactBoneIndex);
        const FTransform TargetRigidWorld = BoneWorld * Rigid.RigidBodyOffset;
        btRigidBody* Body = Rigid.BulletBody;

        if (Rigid.PhysicsMode == 0)
        {
            btTransform T; T.setIdentity();
            T.setOrigin(UEToBullet(TargetRigidWorld.GetLocation()));
            T.setRotation(UEToBullet(TargetRigidWorld.GetRotation()));
            Body->setWorldTransform(T);
            if (Body->getMotionState()) Body->getMotionState()->setWorldTransform(T);
            Body->setInterpolationWorldTransform(T);
            continue;
        }

        if (Rigid.PhysicsMode == 2)
        {
            const btTransform Curr = Body->getWorldTransform();
            const FVector CurrPos = BulletToUE(Curr.getOrigin());
            const FQuat   CurrRot = BulletToUE(Curr.getRotation());
            const FVector TargetPos = TargetRigidWorld.GetLocation();
            const FQuat   TargetRot = TargetRigidWorld.GetRotation();

            const FVector PosDelta = TargetPos - CurrPos;
            const float FollowPosStrength = 0.35f;
            const FVector DesiredLinVel = PosDelta / FMath::Max<float>(StepDt, 1e-4f) * FollowPosStrength;
            Body->setLinearVelocity(Body->getLinearVelocity() + UEToBullet(DesiredLinVel));

            FQuat DeltaRot = TargetRot * CurrRot.Inverse();
            DeltaRot.Normalize();
            FVector Axis; float Angle = 0.f;
            DeltaRot.ToAxisAndAngle(Axis, Angle);
            if (Angle > KINDA_SMALL_NUMBER)
            {
                const float FollowRotStrength = 0.35f;
                const FVector DesiredAngVel = Axis * (Angle / FMath::Max<float>(StepDt, 1e-4f)) * FollowRotStrength;
                const btVector3 BtAngVel(DesiredAngVel.Y, DesiredAngVel.Z, DesiredAngVel.X);
                Body->setAngularVelocity(Body->getAngularVelocity() + BtAngVel);
            }
        }
    }
}
void FMMDPhysicsSimulator::UpdateBonesFromRigidBodies(TArray<FMMDRigidBodyRuntime>& RigidBodies)
{
    for (FMMDRigidBodyRuntime& Rigid : RigidBodies)
    {
        if (!Rigid.BulletBody) continue;
        // Mode 0 为 Kinematic，仅作为碰撞，不写回
        if (Rigid.PhysicsMode == 0) continue;

        const btTransform Curr = Rigid.BulletBody->getWorldTransform();
        Rigid.PrevPosition = BulletToUE(Curr.getOrigin());
        Rigid.PrevRotation = BulletToUE(Curr.getRotation());
    }
}
void FMMDPhysicsSimulator::SimulatePhysics(
    TArray<FMMDRigidBodyRuntime>& RigidBodies,
    TArray<FMMDJointRuntime>& Joints,
    TArray<FMMDSoftBodyRuntime>& SoftBodies,
    FComponentSpacePoseContext& Output,
    float DeltaTime)
{
    if (!bInitialized)
    {
        InitializeBulletWorld();
    }
    if (RigidBodies.Num() == 0) return;

    // 首次：创建刚体与约束
    if (DynamicsWorld->getNumCollisionObjects() == 0)
    {
        InitializeRigidBodies(RigidBodies, Output);
    }
    // 若世界中还没有任何约束且有 Joint 数据，则初始化关节
    if (DynamicsWorld->getNumConstraints() == 0 && Joints.Num() > 0)
    {
        InitializeJoints(Joints, RigidBodies);
    }

    static float Accum = 0.f;
    const float FixedDt = 1.f / 60.f;
    const float MaxFrameDt = 1.f / 15.f;
    const float FrameDt = FMath::Clamp(DeltaTime, 0.f, MaxFrameDt);
    Accum += FrameDt;

    while (Accum >= FixedDt)
    {
        UpdateRigidBodiesFromBones(RigidBodies, Output);
        DynamicsWorld->stepSimulation(FixedDt, 0);
        Accum -= FixedDt;
    }

    UpdateBonesFromRigidBodies(RigidBodies);
}