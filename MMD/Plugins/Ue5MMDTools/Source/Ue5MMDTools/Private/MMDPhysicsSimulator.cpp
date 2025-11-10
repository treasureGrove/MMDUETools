#include "MMDPhysicsSimulator.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Logging/LogMacros.h"

#pragma region 工具函数
static FVector ClosestPointOnSegment(const FVector& A, const FVector& B, const FVector& P, float& OutT)
{
    const FVector AB = B - A;
    const float AB2 = AB.SizeSquared();
    if (AB2 <= KINDA_SMALL_NUMBER)
    {
        OutT = 0.0f;
        return A;
    }
    const float t = FVector::DotProduct(P - A, AB) / AB2;
    OutT = FMath::Clamp(t, 0.0f, 1.0f);
    return A + AB * OutT;
}

// 两线段最近点（Christer Ericson）
static void ClosestPtSegmentSegment(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2, FVector& OutP, FVector& OutQ, float& OutS, float& OutT)
{
    const FVector d1 = Q1 - P1;
    const FVector d2 = Q2 - P2;
    const FVector r = P1 - P2;
    const float a = FVector::DotProduct(d1, d1);
    const float e = FVector::DotProduct(d2, d2);
    const float f = FVector::DotProduct(d2, r);

    if (a <= KINDA_SMALL_NUMBER && e <= KINDA_SMALL_NUMBER)
    {
        OutS = OutT = 0.0f;
        OutP = P1; OutQ = P2;
        return;
    }
    if (a <= KINDA_SMALL_NUMBER)
    {
        OutS = 0.0f;
        OutT = FMath::Clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = FVector::DotProduct(d1, r);
        if (e <= KINDA_SMALL_NUMBER)
        {
            OutT = 0.0f;
            OutS = FMath::Clamp(-c / a, 0.0f, 1.0f);
        }
        else
        {
            const float b = FVector::DotProduct(d1, d2);
            const float denom = a * e - b * b;
            if (denom != 0.0f)
            {
                OutS = FMath::Clamp((b * f - c * e) / denom, 0.0f, 1.0f);
            }
            else
            {
                OutS = 0.0f;
            }
            const float tNom = b * OutS + f;
            if (tNom < 0.0f)
            {
                OutT = 0.0f;
                OutS = FMath::Clamp(-c / a, 0.0f, 1.0f);
            }
            else if (tNom > e)
            {
                OutT = 1.0f;
                OutS = FMath::Clamp((b - c) / a, 0.0f, 1.0f);
            }
            else
            {
                OutT = tNom / e;
            }
        }
    }
    OutP = P1 + d1 * OutS;
    OutQ = P2 + d2 * OutT;
}

// PMX 中胶囊轴约定：本地 Y 轴。 PMX rotation 存为弧度（R.Rotation），这里用于生成世界轴方向。
// 返回胶囊段端点 OutA, OutB（世界空间），以及半径 OutRadius。
static void MakeCapsuleSegmentFromPMX(const FMMDRigidBodyRuntime& R, FVector& OutA, FVector& OutB, float& OutRadius)
{
    float radius = FMath::Abs(R.Size.X);
    float height = FMath::Abs(R.Size.Y);

    // R.Rotation 存为弧度向量 (x,y,z) 按注释。 转为度给 MakeFromEuler。
    const FVector RotDegrees = R.Rotation * (180.0f / PI);
    const FQuat BodyQuat = FQuat::MakeFromEuler(RotDegrees);

    // PMX 胶囊轴为本地 Y -> UE 右轴 (RightVector)
    const FVector LocalCapsuleAxis = FVector::RightVector;
    const FVector AxisWorld = BodyQuat.RotateVector(LocalCapsuleAxis).GetSafeNormal();
    const FVector Center = R.Position;
    const FVector Half = AxisWorld * (0.5f * height);
    OutA = Center - Half;
    OutB = Center + Half;
    OutRadius = radius;
}

// 计算局部逆惯量对角 (对角近似)
static FVector ComputeInvInertiaLocal(const FMMDRigidBodyRuntime& R)
{
    const float m = FMath::Max(R.Mass, KINDA_SMALL_NUMBER);
    FVector InertiaDiag = FVector::ZeroVector;
    switch (R.ShapeType)
    {
    case 0: // sphere
    {
        float r = FMath::Abs(R.Size.X);
        float I = 0.4f * m * r * r;
        InertiaDiag = FVector(I, I, I);
        break;
    }
    case 1: // box
    {
        float x = FMath::Abs(R.Size.X);
        float y = FMath::Abs(R.Size.Y);
        float z = FMath::Abs(R.Size.Z);
        InertiaDiag.X = (1.0f / 12.0f) * m * (y * y + z * z);
        InertiaDiag.Y = (1.0f / 12.0f) * m * (x * x + z * z);
        InertiaDiag.Z = (1.0f / 12.0f) * m * (x * x + y * y);
        break;
    }
    case 2: // capsule approximated as cylinder
    {
        float r = FMath::Abs(R.Size.X);
        float h = FMath::Abs(R.Size.Y);
        float I_axial = 0.5f * m * r * r;
        float I_perp = (1.0f / 12.0f) * m * (3.0f * r * r + h * h);
        InertiaDiag = FVector(I_perp, I_perp, I_axial);
        break;
    }
    default:
        InertiaDiag = FVector(0, 0, 0);
    }

    FVector Inv(0, 0, 0);
    Inv.X = (InertiaDiag.X > KINDA_SMALL_NUMBER) ? (1.0f / InertiaDiag.X) : 0.0f;
    Inv.Y = (InertiaDiag.Y > KINDA_SMALL_NUMBER) ? (1.0f / InertiaDiag.Y) : 0.0f;
    Inv.Z = (InertiaDiag.Z > KINDA_SMALL_NUMBER) ? (1.0f / InertiaDiag.Z) : 0.0f;
    return Inv;
}

// 局部对角逆惯量旋转到世界，返回 3x3 矩阵（InvI_world）
static FMatrix ComputeInvInertiaWorld(const FMMDRigidBodyRuntime& R)
{
    FVector invIlocal = ComputeInvInertiaLocal(R);
    FMatrix InvILocal = FMatrix::Identity;
    InvILocal.M[0][0] = invIlocal.X;
    InvILocal.M[1][1] = invIlocal.Y;
    InvILocal.M[2][2] = invIlocal.Z;

    // 使用当前仿真旋转 PrevRotation 作为刚体朝向
    FMatrix Rm = FQuatRotationMatrix(R.PrevRotation);
    FMatrix InvIWorld = Rm * InvILocal * Rm.GetTransposed();
    return InvIWorld;
}
#pragma endregion

void FMMDPhysicsSimulator::SimulatePhysics(TArray<FMMDRigidBodyRuntime>& RigidBodies, TArray<FMMDJointRuntime>& Joints, TArray<FMMDSoftBodyRuntime>& SoftBodies, FComponentSpacePoseContext& Output, float DeltaTime)
{
    if (RigidBodies.Num() == 0 || DeltaTime <= KINDA_SMALL_NUMBER) return;

    const float TimeStep = 1.0f / 30.0f;
    const int32 SubStepCount = 2;
    const float SubDeltaTime = TimeStep / SubStepCount;

    // 把骨骼（动画）同步到刚体（用于 Bone-tracked / kinematic）
    SyncBoneToPhysics(RigidBodies, Output);

    for (int32 Step = 0; Step < SubStepCount; ++Step)
    {
        // 施力、速度积分（子步）
        for (FMMDRigidBodyRuntime& Rigid : RigidBodies)
        {
            ApplyForces(Rigid, SubDeltaTime);
            IntegrateVelocity(Rigid, SubDeltaTime);
        }

        // 碰撞检测与响应
        DetectAndResolveCollisions(RigidBodies, SubDeltaTime);

        // 约束迭代求解（Joint spring/limits）
        const int32 Iterations = 10;
        SolveConstraints(RigidBodies, Joints, SubDeltaTime, Iterations);
    }

    // 写回骨骼变换（物理驱动骨骼）
    TArray<FBoneTransform> OutBoneTransforms;
    WriteBackToBones(RigidBodies, OutBoneTransforms);

    // 将骨骼变换应用到动画系统（若上层需要此数组）
    // 这里只是填充 OutBoneTransforms；调用者负责使用它。
}

void FMMDPhysicsSimulator::SyncBoneToPhysics(TArray<FMMDRigidBodyRuntime>& RigidBodies, FComponentSpacePoseContext& Output)
{
    // 对于 PhysicsMode == 2 (BoneTracked)，把骨骼位置/旋转作为刚体的目标并直接设置 PrevPosition/PrevRotation
    for (FMMDRigidBodyRuntime& R : RigidBodies)
    {
        if (R.CompactBoneIndex != FCompactPoseBoneIndex(INDEX_NONE))
        {
            // 从动画上下文获取 Component-space transform
            const FTransform BoneCS = Output.Pose.GetComponentSpaceTransform(R.CompactBoneIndex);
            if (R.PhysicsMode == 2) // kinematic / bone-driven
            {
                R.PrevPosition = BoneCS.GetLocation();
                R.Position = R.PrevPosition;
                R.PrevRotation = BoneCS.GetRotation();
                R.Rotation = R.PrevRotation.Euler() * (PI / 180.0f); // store as radians like PMX.Rotation
                R.Velocity = FVector::ZeroVector;
                R.AngularVelocity = FVector::ZeroVector;
            }
            else if (R.PhysicsMode == 0) // static : stick to bone/pose
            {
                R.PrevPosition = BoneCS.GetLocation();
                R.Position = R.PrevPosition;
                R.PrevRotation = BoneCS.GetRotation();
                R.Rotation = R.PrevRotation.Euler() * (PI / 180.0f);
                R.Velocity = FVector::ZeroVector;
                R.AngularVelocity = FVector::ZeroVector;
            }
        }
    }
}

void FMMDPhysicsSimulator::ApplyForces(FMMDRigidBodyRuntime& Rigid, float DeltaTime)
{
    if (Rigid.Mass <= KINDA_SMALL_NUMBER) return;
    if (Rigid.PhysicsMode != 1) return; // only Dynamic

    // 缓存逆质量
    if (Rigid.InvMass <= KINDA_SMALL_NUMBER)
    {
        Rigid.InvMass = (Rigid.Mass > KINDA_SMALL_NUMBER) ? (1.0f / Rigid.Mass) : 0.0f;
    }

    FVector TotalForce = FVector::ZeroVector;
    FVector TotalTorque = FVector::ZeroVector;

    // 重力（cm/s^2）
    const FVector Gravity = FVector(0.0f, 0.0f, -980.0f);
    TotalForce += Gravity * Rigid.Mass;

    // 线性阻尼近似为力项（或可直接衰减速度）
    if (Rigid.LinearDamping > KINDA_SMALL_NUMBER)
    {
        TotalForce += -Rigid.LinearDamping * Rigid.Velocity;
    }

    // 半隐式欧拉: v += (F/m) * dt
    FVector Acc = TotalForce * Rigid.InvMass;
    Rigid.Velocity += Acc * DeltaTime;

    // 角动力学：用对角局部逆惯量近似（外力矩项留空，PMX 中通常没有直接外力矩）
    FVector invIlocal = ComputeInvInertiaLocal(Rigid);
    FVector TorqueLocal = Rigid.PrevRotation.UnrotateVector(TotalTorque);
    FVector AngularAccLocal = FVector(
        TorqueLocal.X * invIlocal.X,
        TorqueLocal.Y * invIlocal.Y,
        TorqueLocal.Z * invIlocal.Z
    );
    FVector AngularAccWorld = Rigid.PrevRotation.RotateVector(AngularAccLocal);
    Rigid.AngularVelocity += AngularAccWorld * DeltaTime;

    // 角阻尼
    if (Rigid.AngularDamping > KINDA_SMALL_NUMBER)
    {
        float factor = FMath::Clamp(1.0f - Rigid.AngularDamping * DeltaTime, 0.0f, 1.0f);
        Rigid.AngularVelocity *= factor;
    }

    // 限制速度避免发散
    const float MaxLinear = 10000.0f;
    if (Rigid.Velocity.SizeSquared() > MaxLinear * MaxLinear)
    {
        Rigid.Velocity = Rigid.Velocity.GetSafeNormal() * MaxLinear;
    }
    const float MaxAngular = 10000.0f;
    if (Rigid.AngularVelocity.SizeSquared() > MaxAngular * MaxAngular)
    {
        Rigid.AngularVelocity = Rigid.AngularVelocity.GetSafeNormal() * MaxAngular;
    }
}

void FMMDPhysicsSimulator::IntegrateVelocity(FMMDRigidBodyRuntime& Rigid, float DeltaTime)
{
    if (Rigid.PhysicsMode == 0 || Rigid.PhysicsMode == 2) return; // static or bone-driven don't integrate

    if (DeltaTime <= KINDA_SMALL_NUMBER) return;

    // 半隐式欧拉：位置由 velocity 更新（ApplyForces 已更新 velocity）
    Rigid.PrevPosition = Rigid.Position;
    Rigid.Position += Rigid.Velocity * DeltaTime;

    // 角速度积分：四元数导数 q_dot = 0.5 * omega_quat * q
    if (Rigid.AngularVelocity.SizeSquared() > KINDA_SMALL_NUMBER)
    {
        FQuat q = Rigid.PrevRotation;
        FQuat omegaQ(0.0f, Rigid.AngularVelocity.X, Rigid.AngularVelocity.Y, Rigid.AngularVelocity.Z);
        FQuat qDot = omegaQ * q;
        qDot.X *= 0.5f; qDot.Y *= 0.5f; qDot.Z *= 0.5f; qDot.W *= 0.5f;
        q.X += qDot.X * DeltaTime;
        q.Y += qDot.Y * DeltaTime;
        q.Z += qDot.Z * DeltaTime;
        q.W += qDot.W * DeltaTime;
        q.Normalize();
        Rigid.PrevRotation = q;
        Rigid.Rotation = q.Euler() * (PI / 180.0f); // store as radians to be consistent
    }
    else
    {
        Rigid.Rotation = Rigid.PrevRotation.Euler() * (PI / 180.0f);
    }

    // clamp checks
    const float MaxLinear = 1000.0f;
    if (Rigid.Velocity.SizeSquared() > MaxLinear * MaxLinear)
    {
        Rigid.Velocity = Rigid.Velocity.GetSafeNormal() * MaxLinear;
    }
    const float MaxAngular = 50.0f;
    if (Rigid.AngularVelocity.SizeSquared() > MaxAngular * MaxAngular)
    {
        Rigid.AngularVelocity = Rigid.AngularVelocity.GetSafeNormal() * MaxAngular;
    }
}

bool FMMDPhysicsSimulator::ShouldCollide(const FMMDRigidBodyRuntime& A, const FMMDRigidBodyRuntime& B)
{
    if (&A == &B) return false;
    const uint32 AGroupBit = (A.Group < 32) ? (1u << A.Group) : 0u;
    const uint32 BGroupBit = (B.Group < 32) ? (1u << B.Group) : 0u;
    if ((AGroupBit & B.CollisionMask) == 0u) return false;
    if ((BGroupBit & A.CollisionMask) == 0u) return false;
    // 两者都为静态通常不需要检测
    if (A.PhysicsMode == 0 && B.PhysicsMode == 0) return false;
    return true;
}

bool FMMDPhysicsSimulator::CheckCollisionShape(const FMMDRigidBodyRuntime& A, const FMMDRigidBodyRuntime& B, FVector& OutContactNormal, float& OutPenetrationDepth)
{
    const FVector PosA = A.Position;
    const FVector PosB = B.Position;

    auto BoundingSphereRadius = [](const FMMDRigidBodyRuntime& R) -> float
        {
            if (R.ShapeType == 1)
                return 0.5f * FMath::Max3(FMath::Abs(R.Size.X), FMath::Abs(R.Size.Y), FMath::Abs(R.Size.Z));
            if (R.ShapeType == 0)
                return FMath::Abs(R.Size.X);
            if (R.ShapeType == 2)
                return FMath::Abs(R.Size.X) + 0.5f * FMath::Abs(R.Size.Y);
            return 0.0f;
        };

    // sphere-sphere
    if (A.ShapeType == 0 && B.ShapeType == 0)
    {
        float rA = FMath::Abs(A.Size.X);
        float rB = FMath::Abs(B.Size.X);
        FVector d = PosB - PosA;
        float dist2 = d.SizeSquared();
        float rsum = rA + rB;
        if (dist2 <= rsum * rsum)
        {
            float dist = FMath::Sqrt(FMath::Max(dist2, KINDA_SMALL_NUMBER));
            OutContactNormal = (dist > KINDA_SMALL_NUMBER) ? (d / dist) : FVector::RightVector;
            OutPenetrationDepth = rsum - dist;
            return true;
        }
        return false;
    }

    // capsule-capsule
    if (A.ShapeType == 2 && B.ShapeType == 2)
    {
        FVector A1, A2, B1, B2; float rA, rB;
        MakeCapsuleSegmentFromPMX(A, A1, A2, rA);
        MakeCapsuleSegmentFromPMX(B, B1, B2, rB);
        FVector PA, PB; float s, t;
        ClosestPtSegmentSegment(A1, A2, B1, B2, PA, PB, s, t);
        FVector delta = PB - PA;
        float dist2 = delta.SizeSquared();
        float rsum = rA + rB;
        if (dist2 <= rsum * rsum)
        {
            float dist = FMath::Sqrt(FMath::Max(dist2, KINDA_SMALL_NUMBER));
            OutContactNormal = (dist > KINDA_SMALL_NUMBER) ? (delta / dist) : FVector::RightVector;
            OutPenetrationDepth = rsum - dist;
            return true;
        }
        return false;
    }

    // capsule-sphere
    if ((A.ShapeType == 2 && B.ShapeType == 0) || (A.ShapeType == 0 && B.ShapeType == 2))
    {
        const FMMDRigidBodyRuntime& Cap = (A.ShapeType == 2) ? A : B;
        const FMMDRigidBodyRuntime& Sph = (A.ShapeType == 0) ? A : B;
        FVector C1, C2; float rCap;
        MakeCapsuleSegmentFromPMX(Cap, C1, C2, rCap);
        float t; FVector Closest = ClosestPointOnSegment(C1, C2, Sph.Position, t);
        float rSph = FMath::Abs(Sph.Size.X);
        FVector delta = Sph.Position - Closest;
        float dist2 = delta.SizeSquared();
        float rsum = rCap + rSph;
        if (dist2 <= rsum * rsum)
        {
            float dist = FMath::Sqrt(FMath::Max(dist2, KINDA_SMALL_NUMBER));
            FVector normal = (dist > KINDA_SMALL_NUMBER) ? (delta / dist) : FVector::RightVector;
            if (&Cap == &A)
                OutContactNormal = normal;
            else
                OutContactNormal = -normal;
            OutPenetrationDepth = rsum - dist;
            return true;
        }
        return false;
    }

    // fallback bounding spheres
    {
        float rA = BoundingSphereRadius(A);
        float rB = BoundingSphereRadius(B);
        FVector d = PosB - PosA;
        float dist2 = d.SizeSquared();
        float rsum = rA + rB;
        if (dist2 <= rsum * rsum)
        {
            float dist = FMath::Sqrt(FMath::Max(dist2, KINDA_SMALL_NUMBER));
            OutContactNormal = (dist > KINDA_SMALL_NUMBER) ? (d / dist) : FVector::RightVector;
            OutPenetrationDepth = rsum - dist;
            return true;
        }
        return false;
    }
}

// 计算碰撞冲量：使用有效质量法，考虑线性质量与角惯量贡献（更靠近 Bullet）
FVector FMMDPhysicsSimulator::CalculateCollisionImpulse(const FMMDRigidBodyRuntime& A, const FMMDRigidBodyRuntime& B, const FVector& ContactNormal, float PenetrationDepth)
{
    // 此函数需要接触点 r 向量来精确计算有效质量；为了兼容调用者，我们将返回基于质心近似的线性冲量。
    // 更精确的角传播在 DetectAndResolveCollisions 中通过 r × J 和 InvIWorld 实现。
    const float invMassA = A.InvMass;
    const float invMassB = B.InvMass;
    const float invMassSum = invMassA + invMassB;
    if (invMassSum <= KINDA_SMALL_NUMBER) return FVector::ZeroVector;

    FVector relVel = B.Velocity - A.Velocity;
    float vn = FVector::DotProduct(relVel, ContactNormal);

    float e = FMath::Max(A.Restitution, B.Restitution);

    float jn = 0.0f;
    if (vn < 0.0f)
    {
        jn = -(1.0f + e) * vn / invMassSum;
    }
    else
    {
        jn = 0.0f;
    }

    FVector impulseN = jn * ContactNormal;

    // 切向摩擦（使用简化模型）
    FVector vt = relVel - vn * ContactNormal;
    FVector impulseT = FVector::ZeroVector;
    float vtLen = vt.Size();
    if (vtLen > KINDA_SMALL_NUMBER)
    {
        FVector tangent = vt / vtLen;
        float jt = -vtLen / invMassSum;
        float mu = FMath::Sqrt(FMath::Max(0.0f, A.Friction * B.Friction));
        float maxF = mu * jn;
        if (FMath::IsNearlyZero(jn)) maxF = 0.0f;
        jt = FMath::Clamp(jt, -maxF, maxF);
        impulseT = jt * tangent;
    }

    return impulseN + impulseT;
}

void FMMDPhysicsSimulator::DetectAndResolveCollisions(TArray<FMMDRigidBodyRuntime>& RigidBodies, float DeltaTime)
{
    const int32 Count = RigidBodies.Num();
    if (Count < 2 || DeltaTime <= KINDA_SMALL_NUMBER) return;

    // O(n^2) 处理，可后续加入加速结构
    for (int32 i = 0; i < Count; ++i)
    {
        for (int32 j = i + 1; j < Count; ++j)
        {
            FMMDRigidBodyRuntime& A = RigidBodies[i];
            FMMDRigidBodyRuntime& B = RigidBodies[j];

            if (!ShouldCollide(A, B)) continue;

            FVector ContactNormal;
            float PenetrationDepth = 0.0f;
            if (!CheckCollisionShape(A, B, ContactNormal, PenetrationDepth)) continue;

            // 计算接触点
            FVector contactA = FVector::ZeroVector;
            FVector contactB = FVector::ZeroVector;

            if (A.ShapeType == 0 && B.ShapeType == 0)
            {
                float rA = FMath::Abs(A.Size.X);
                float rB = FMath::Abs(B.Size.X);
                contactA = A.Position + ContactNormal * rA;
                contactB = B.Position - ContactNormal * rB;
            }
            else if (A.ShapeType == 2 && B.ShapeType == 2)
            {
                FVector A1, A2, B1, B2; float rA, rB;
                MakeCapsuleSegmentFromPMX(A, A1, A2, rA);
                MakeCapsuleSegmentFromPMX(B, B1, B2, rB);
                FVector PA, PB; float s, t;
                ClosestPtSegmentSegment(A1, A2, B1, B2, PA, PB, s, t);
                contactA = PA; contactB = PB;
            }
            else if ((A.ShapeType == 2 && B.ShapeType == 0) || (A.ShapeType == 0 && B.ShapeType == 2))
            {
                const FMMDRigidBodyRuntime& Cap = (A.ShapeType == 2) ? A : B;
                const FMMDRigidBodyRuntime& Sph = (A.ShapeType == 0) ? A : B;
                FVector C1, C2; float rCap;
                MakeCapsuleSegmentFromPMX(Cap, C1, C2, rCap);
                float t; FVector Closest = ClosestPointOnSegment(C1, C2, Sph.Position, t);
                if (&Cap == &A)
                {
                    contactA = Closest; contactB = Sph.Position;
                }
                else
                {
                    contactA = Sph.Position; contactB = Closest;
                }
            }
            else
            {
                float rA = (A.ShapeType == 0) ? FMath::Abs(A.Size.X) : 0.5f * FMath::Max3(FMath::Abs(A.Size.X), FMath::Abs(A.Size.Y), FMath::Abs(A.Size.Z));
                float rB = (B.ShapeType == 0) ? FMath::Abs(B.Size.X) : 0.5f * FMath::Max3(FMath::Abs(B.Size.X), FMath::Abs(B.Size.Y), FMath::Abs(B.Size.Z));
                FVector d = B.Position - A.Position;
                float dist = FMath::Sqrt(FMath::Max(d.SizeSquared(), KINDA_SMALL_NUMBER));
                FVector n = (dist > KINDA_SMALL_NUMBER) ? (d / dist) : ContactNormal;
                contactA = A.Position + n * rA;
                contactB = B.Position - n * rB;
            }

            // 位置修正（穿透纠正，按质量分配）
            const float k_slop = 0.5f; // cm
            const float percent = 0.8f;
            float corr = FMath::Max(PenetrationDepth - k_slop, 0.0f);
            float invMassSum = A.InvMass + B.InvMass;
            if (invMassSum > KINDA_SMALL_NUMBER && corr > KINDA_SMALL_NUMBER)
            {
                FVector correction = (corr / invMassSum) * percent * ContactNormal;
                if (A.InvMass > 0.0f) A.Position -= correction * A.InvMass;
                if (B.InvMass > 0.0f) B.Position += correction * B.InvMass;
                A.PrevPosition = A.Position;
                B.PrevPosition = B.Position;
            }

            // 计算线性冲量（基于质心近似）
            FVector Impulse = CalculateCollisionImpulse(A, B, ContactNormal, PenetrationDepth);

            // 应用到线速度
            if (A.InvMass > KINDA_SMALL_NUMBER) A.Velocity -= Impulse * A.InvMass;
            if (B.InvMass > KINDA_SMALL_NUMBER) B.Velocity += Impulse * B.InvMass;

            // 角传播：使用 r x J 并通过世界逆惯量转换为角速度增量
            FVector rA = contactA - A.Position;
            FVector rB = contactB - B.Position;

            FVector torqueA = FVector::CrossProduct(rA, -Impulse);
            FVector torqueB = FVector::CrossProduct(rB, Impulse);

            FMatrix InvIA = ComputeInvInertiaWorld(A);
            FMatrix InvIB = ComputeInvInertiaWorld(B);

            FVector deltaOmegaA = InvIA.TransformVector(torqueA);
            FVector deltaOmegaB = InvIB.TransformVector(torqueB);

            A.AngularVelocity += deltaOmegaA;
            B.AngularVelocity += deltaOmegaB;
        }
    }
}

void FMMDPhysicsSimulator::SolveSingleConstraint(FMMDRigidBodyRuntime& RigidA, FMMDRigidBodyRuntime& RigidB, const FMMDJointRuntime& Joint, float DeltaTime)
{
    // 简化的 Spring6DOF 实现：
    // 保存初始相对位置（第一次调用时）
    if (!const_cast<FMMDJointRuntime&>(Joint).bInitialized)
    {
        const_cast<FMMDJointRuntime&>(Joint).InitialPositionWorld = RigidB.Position - RigidA.Position;
        const_cast<FMMDJointRuntime&>(Joint).bInitialized = true;
    }

    FVector desiredRel = Joint.InitialPositionWorld; // 目标相对位置（world）
    FVector currentRel = RigidB.Position - RigidA.Position;
    FVector posError = currentRel - desiredRel;

    // 弹簧参数来自 Joint.SpringPos (PMX)，把其作为刚度（k）与阻尼(d)参考
    FVector springK = Joint.SpringPos; // 用分量直接作为 spring 系数（可调）
    FVector springD = Joint.SpringRot * 0.5f; // 仅示意

    // 计算一个简单的脉冲以修正位置误差（按线性质量）
    float invMassA = RigidA.InvMass;
    float invMassB = RigidB.InvMass;
    float invMassSum = invMassA + invMassB;
    if (invMassSum <= KINDA_SMALL_NUMBER) return;

    // 计算目标速度来抵消误差（bias），并加入阻尼（相对速度）
    FVector relVel = RigidB.Velocity - RigidA.Velocity;
    FVector bias = -posError * 0.2f / DeltaTime; // Baumgarte-like bias
    FVector Cdot = relVel;
    FVector desiredVelChange = bias - Cdot;

    // 近似分量化处理
    FVector impulse;
    impulse.X = desiredVelChange.X / invMassSum;
    impulse.Y = desiredVelChange.Y / invMassSum;
    impulse.Z = desiredVelChange.Z / invMassSum;

    // apply impulses
    if (RigidA.InvMass > 0.0f) RigidA.Velocity -= impulse * RigidA.InvMass;
    if (RigidB.InvMass > 0.0f) RigidB.Velocity += impulse * RigidB.InvMass;

    // 角度约束与弹簧 (简化): 根据 Joint.SpringRot 施加扭矩阻尼到角速度
    FVector angError = FVector::ZeroVector; // 精确实现需要骨架空间旋转差异计算，略去
    FVector angImpulse = -(RigidB.AngularVelocity - RigidA.AngularVelocity) * 0.5f;
    RigidA.AngularVelocity += angImpulse * 0.5f;
    RigidB.AngularVelocity -= angImpulse * 0.5f;
}

void FMMDPhysicsSimulator::SolveConstraints(TArray<FMMDRigidBodyRuntime>& RigidBodies, TArray<FMMDJointRuntime>& Joints, float DeltaTime, int32 IterationCount)
{
    if (Joints.Num() == 0) return;
    for (int32 iter = 0; iter < IterationCount; ++iter)
    {
        for (FMMDJointRuntime& Joint : Joints)
        {
            if (Joint.RigidA < 0 || Joint.RigidB < 0) continue;
            if (!RigidBodies.IsValidIndex(Joint.RigidA) || !RigidBodies.IsValidIndex(Joint.RigidB)) continue;
            SolveSingleConstraint(RigidBodies[Joint.RigidA], RigidBodies[Joint.RigidB], Joint, DeltaTime);
        }
    }
}

void FMMDPhysicsSimulator::WriteBackToBones(const TArray<FMMDRigidBodyRuntime>& RigidBodies, TArray<FBoneTransform>& OutBoneTransforms)
{
    // 把与骨骼关联的刚体结果写回骨骼（只处理 PhysicsMode 为 Dynamic 时的写回）
    for (const FMMDRigidBodyRuntime& R : RigidBodies)
    {
        if (R.CompactBoneIndex != FCompactPoseBoneIndex(INDEX_NONE))
        {
            if (R.PhysicsMode == 1) // dynamic -> 写回（Blend 应由上层决定）
            {
                FTransform T(R.PrevRotation, R.Position);
                OutBoneTransforms.Add(FBoneTransform(R.CompactBoneIndex, T));
            }
            else if (R.PhysicsMode == 2) // bone-driven: bone already drives physics; optionally ensure bone transform uses RigidBodyOffset
            {
                // no-op: bone drives rigidbody
            }
        }
    }
}