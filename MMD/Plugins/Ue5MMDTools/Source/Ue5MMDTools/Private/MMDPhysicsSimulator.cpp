#include "MMDPhysicsSimulator.h"

void FMMDPhysicsSimulator::SimulatePhysics(TArray<FMMDRigidBodyRuntime>& RigidBodies, TArray<FMMDJointRuntime>& Joints, TArray<FMMDSoftBodyRuntime>& SoftBodies, FComponentSpacePoseContext& Output, float DeltaTime)
{
	if(RigidBodies.Num() ==0||DeltaTime<=KINDA_SMALL_NUMBER)
	{
		return;
	}
	const float ClampedDeltaTime = FMath::Min(DeltaTime, 0.0333f);

	SyncBoneToPhysics(RigidBodies, Output);
	//应用力和重力
	for(FMMDRigidBodyRuntime& Rigid : RigidBodies)
	{
		if (Rigid.PhysicsMode == 1 || Rigid.PhysicsMode == 2) {
			ApplyForces(Rigid, ClampedDeltaTime);
		}
	}
	//速度积分
	for(FMMDRigidBodyRuntime& Rigid : RigidBodies)
	{
		if (Rigid.PhysicsMode == 1 || Rigid.PhysicsMode == 2) {
			IntegrateVelocity(Rigid, ClampedDeltaTime);
		}
	}
	//碰撞检测与响应
	DetectAndResolveCollisions(RigidBodies, ClampedDeltaTime);
	// 2.4 约束求解（迭代 10 次，符合 MMD 标准）
	SolveConstraints(RigidBodies, Joints, ClampedDeltaTime, 10);


}

void FMMDPhysicsSimulator::SyncBoneToPhysics(TArray<FMMDRigidBodyRuntime>& RigidBodies, FComponentSpacePoseContext& Output)
{
	for (FMMDRigidBodyRuntime& Rigid : RigidBodies) {
        if (!Rigid.CompactBoneIndex.IsValid())
            continue;

        FTransform BoneTransform = Output.Pose.GetComponentSpaceTransform(Rigid.CompactBoneIndex);

        switch (Rigid.PhysicsMode)
        {
        case 0: // Bone-Driven: 完全跟随骨骼
        {
            Rigid.PrevPosition = BoneTransform.GetLocation();
            Rigid.PrevRotation = BoneTransform.GetRotation();
            Rigid.Velocity = FVector::ZeroVector;
            Rigid.AngularVelocity = FVector::ZeroVector;
        }
        break;

        case 1: // Physics: 纯物理模拟（不同步骨骼）
        {
            // 第一次初始化时从骨骼获取位置
            if (Rigid.PrevPosition.IsNearlyZero())
            {
                Rigid.PrevPosition = BoneTransform.GetLocation();
                Rigid.PrevRotation = BoneTransform.GetRotation();
            }
        }
        break;

        case 2: // Physics+Bone: 混合模式
        {
            // 第一次初始化
            if (Rigid.PrevPosition.IsNearlyZero())
            {
                Rigid.PrevPosition = BoneTransform.GetLocation();
                Rigid.PrevRotation = BoneTransform.GetRotation();
            }
        }
        break;
        }
	}
}

void FMMDPhysicsSimulator::ApplyForces(FMMDRigidBodyRuntime& Rigid, float DeltaTime)
{
    // MMD 使用 UE 单位：厘米/秒²
    const FVector Gravity(0.0f, 0.0f, -980.0f); // 重力加速度

    // 应用重力
    Rigid.Velocity += Gravity * DeltaTime;

    // 应用线性阻尼（指数衰减，符合 MMD/Bullet 标准）
    float LinearDampingFactor = FMath::Exp(-Rigid.LinearDamping * DeltaTime);
    Rigid.Velocity *= LinearDampingFactor;

    // 应用角阻尼
    float AngularDampingFactor = FMath::Exp(-Rigid.AngularDamping * DeltaTime);
    Rigid.AngularVelocity *= AngularDampingFactor;
}

void FMMDPhysicsSimulator::IntegrateVelocity(FMMDRigidBodyRuntime& Rigid, float DeltaTime)
{
    // 更新位置（Euler 积分）
    Rigid.PrevPosition += Rigid.Velocity * DeltaTime;

    // 更新旋转
    if (Rigid.AngularVelocity.SizeSquared() > KINDA_SMALL_NUMBER)
    {
        FVector AngularAxis = Rigid.AngularVelocity;
        float AngularSpeed = AngularAxis.Size();
        AngularAxis.Normalize();

        // 四元数旋转增量
        FQuat DeltaRotation = FQuat(AngularAxis, AngularSpeed * DeltaTime);
        Rigid.PrevRotation = (DeltaRotation * Rigid.PrevRotation).GetNormalized();
    }
}

void FMMDPhysicsSimulator::DetectAndResolveCollisions(TArray<FMMDRigidBodyRuntime>& RigidBodies, float DeltaTime)
{
    for (int32 i = 0; i < RigidBodies.Num(); ++i)
    {
        for (int32 j = i + 1; j < RigidBodies.Num(); ++j)
        {
            FMMDRigidBodyRuntime& A = RigidBodies[i];
            FMMDRigidBodyRuntime& B = RigidBodies[j];

            // 检查是否应该碰撞
            if (!ShouldCollide(A, B))
                continue;

            // 检查碰撞形状
            FVector ContactNormal;
            float PenetrationDepth;
            if (CheckCollisionShape(A, B, ContactNormal, PenetrationDepth))
            {
                // 计算冲量并应用
                FVector Impulse = CalculateCollisionImpulse(A, B, ContactNormal, PenetrationDepth);

                float InvMassA = (A.PhysicsMode == 1 || A.PhysicsMode == 2) ? (1.0f / FMath::Max(A.Mass, 0.001f)) : 0.0f;
                float InvMassB = (B.PhysicsMode == 1 || B.PhysicsMode == 2) ? (1.0f / FMath::Max(B.Mass, 0.001f)) : 0.0f;

                if (InvMassA > 0.0f)
                {
                    A.Velocity -= Impulse * InvMassA;
                    A.PrevPosition -= ContactNormal * (PenetrationDepth * 0.5f);
                }

                if (InvMassB > 0.0f)
                {
                    B.Velocity += Impulse * InvMassB;
                    B.PrevPosition += ContactNormal * (PenetrationDepth * 0.5f);
                }
            }
        }
    }
}

void FMMDPhysicsSimulator::SolveConstraints(TArray<FMMDRigidBodyRuntime>& RigidBodies, TArray<FMMDJointRuntime>& Joints, float DeltaTime, int32 IterationCount)
{
    for (int32 Iter = 0; Iter < IterationCount; ++Iter)
    {
        for (const FMMDJointRuntime& Joint : Joints)
        {
            if (Joint.RigidA < 0 || Joint.RigidB < 0)
                continue;

            if (Joint.RigidA >= RigidBodies.Num() || Joint.RigidB >= RigidBodies.Num())
                continue;

            FMMDRigidBodyRuntime& RigidA = RigidBodies[Joint.RigidA];
            FMMDRigidBodyRuntime& RigidB = RigidBodies[Joint.RigidB];

            // 跳过两个都是静态的
            if (RigidA.PhysicsMode == 0 && RigidB.PhysicsMode == 0)
                continue;

            SolveSingleConstraint(RigidA, RigidB, Joint, DeltaTime);
        }
    }
}

void FMMDPhysicsSimulator::SolveSingleConstraint(FMMDRigidBodyRuntime& RigidA, FMMDRigidBodyRuntime& RigidB, const FMMDJointRuntime& Joint, float DeltaTime)
{
    FVector PositionError = RigidB.PrevPosition - RigidA.PrevPosition - Joint.Position;

    // 应用位置限制
    FVector ClampedPos;
    ClampedPos.X = FMath::Clamp(PositionError.X, Joint.LimitPosLower.X, Joint.LimitPosUpper.X);
    ClampedPos.Y = FMath::Clamp(PositionError.Y, Joint.LimitPosLower.Y, Joint.LimitPosUpper.Y);
    ClampedPos.Z = FMath::Clamp(PositionError.Z, Joint.LimitPosLower.Z, Joint.LimitPosUpper.Z);

    FVector PositionCorrection = (ClampedPos - PositionError) * 0.5f;

    // 应用弹簧力（Spring6DOF）
    FVector SpringForce = FVector(
        -PositionError.X * Joint.SpringPos.X,
        -PositionError.Y * Joint.SpringPos.Y,
        -PositionError.Z * Joint.SpringPos.Z
    );

    float InvMassA = (RigidA.PhysicsMode == 1 || RigidA.PhysicsMode == 2) ? (1.0f / FMath::Max(RigidA.Mass, 0.001f)) : 0.0f;
    float InvMassB = (RigidB.PhysicsMode == 1 || RigidB.PhysicsMode == 2) ? (1.0f / FMath::Max(RigidB.Mass, 0.001f)) : 0.0f;

    if (InvMassA > 0.0f)
    {
        RigidA.Velocity += SpringForce * InvMassA * DeltaTime;
        RigidA.PrevPosition += PositionCorrection;
    }

    if (InvMassB > 0.0f)
    {
        RigidB.Velocity -= SpringForce * InvMassB * DeltaTime;
        RigidB.PrevPosition -= PositionCorrection;
    }

    // TODO: 旋转约束（下一步实现）
}

void FMMDPhysicsSimulator::WriteBackToBones(const TArray<FMMDRigidBodyRuntime>& RigidBodies, TArray<FBoneTransform>& OutBoneTransforms)
{
}

bool FMMDPhysicsSimulator::ShouldCollide(const FMMDRigidBodyRuntime& A, const FMMDRigidBodyRuntime& B)
{
    if (A.PhysicsMode == 0 && B.PhysicsMode == 0)
        return false;

    // 检查碰撞组和掩码
    bool ACanCollideWithB = (A.CollisionMask & (1 << B.Group)) != 0;
    bool BCanCollideWithA = (B.CollisionMask & (1 << A.Group)) != 0;

    return ACanCollideWithB && BCanCollideWithA;
}

bool FMMDPhysicsSimulator::CheckCollisionShape(const FMMDRigidBodyRuntime& A, const FMMDRigidBodyRuntime& B, FVector& OutContactNormal, float& OutPenetrationDepth)
{
    float RadiusA = A.Size.X;
    float RadiusB = B.Size.X;

    FVector Delta = B.PrevPosition - A.PrevPosition;
    float Distance = Delta.Size();

    if (Distance < (RadiusA + RadiusB) && Distance > KINDA_SMALL_NUMBER)
    {
        OutContactNormal = Delta / Distance;
        OutPenetrationDepth = (RadiusA + RadiusB) - Distance;
        return true;
    }

    return false;
}

FVector FMMDPhysicsSimulator::CalculateCollisionImpulse(const FMMDRigidBodyRuntime& A, const FMMDRigidBodyRuntime& B, const FVector& ContactNormal, float PenetrationDepth)
{
    FVector RelativeVelocity = B.Velocity - A.Velocity;
    float VelocityAlongNormal = FVector::DotProduct(RelativeVelocity, ContactNormal);

    if (VelocityAlongNormal > 0.0f)
        return FVector::ZeroVector; // 正在分离

    // 弹性系数（取平均）
    float Restitution = (A.Restitution + B.Restitution) * 0.5f;

    // 计算冲量
    float InvMassA = (A.PhysicsMode == 1 || A.PhysicsMode == 2) ? (1.0f / FMath::Max(A.Mass, 0.001f)) : 0.0f;
    float InvMassB = (B.PhysicsMode == 1 || B.PhysicsMode == 2) ? (1.0f / FMath::Max(B.Mass, 0.001f)) : 0.0f;

    float ImpulseMagnitude = -(1.0f + Restitution) * VelocityAlongNormal;
    ImpulseMagnitude /= (InvMassA + InvMassB);

    return ContactNormal * ImpulseMagnitude;
}
