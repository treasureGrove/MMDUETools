#pragma once

#include "CoreMinimal.h"
#include "AGN_MMDSkeletalControl.h"

class UE5MMDTOOLS_API FMMDPhysicsSimulator
{
public:
    static void SimulatePhysics(
        TArray<FMMDRigidBodyRuntime>& RigidBodies,
        TArray<FMMDJointRuntime>& Joints,
		TArray<FMMDSoftBodyRuntime>& SoftBodies,
        FComponentSpacePoseContext& Output,
        float DeltaTime
    );
    /**
     * 同步骨骼位置到刚体（根据 PhysicsMode）
     */
    static void SyncBoneToPhysics(
        TArray<FMMDRigidBodyRuntime>& RigidBodies,
        FComponentSpacePoseContext& Output
    );
    //应用重力和其他外力
    static void ApplyForces(
        FMMDRigidBodyRuntime& Rigid,
        float DeltaTime
    );

    /**
     * 速度积分和位置预测
     */
    static void IntegrateVelocity(
        FMMDRigidBodyRuntime& Rigid,
        float DeltaTime
    );

    /**
     * 碰撞检测和响应
     */
    static void DetectAndResolveCollisions(
        TArray<FMMDRigidBodyRuntime>& RigidBodies,
        float DeltaTime
    );

    /**
     * 约束求解（核心算法）
     * @param IterationCount 迭代次数（MMD 通常用 4-10 次）
     */
    static void SolveConstraints(
        TArray<FMMDRigidBodyRuntime>& RigidBodies,
        TArray<FMMDJointRuntime>& Joints,
        float DeltaTime,
        int32 IterationCount = 10
    );

    /**
     * 求解单个 Spring6DOF 约束
     */
    static void SolveSingleConstraint(
        FMMDRigidBodyRuntime& RigidA,
        FMMDRigidBodyRuntime& RigidB,
        const FMMDJointRuntime& Joint,
        float DeltaTime
    );

    // ========== 阶段 3: 写回骨骼 ==========

    /**
     * 将物理结果写回骨骼
     */
    static void WriteBackToBones(
        const TArray<FMMDRigidBodyRuntime>& RigidBodies,
        TArray<FBoneTransform>& OutBoneTransforms
    );

    // ========== 辅助函数 ==========

    static bool ShouldCollide(
        const FMMDRigidBodyRuntime& A,
        const FMMDRigidBodyRuntime& B
    );

    static bool CheckCollisionShape(
        const FMMDRigidBodyRuntime& A,
        const FMMDRigidBodyRuntime& B,
        FVector& OutContactNormal,
        float& OutPenetrationDepth
    );

    static FVector CalculateCollisionImpulse(
        const FMMDRigidBodyRuntime& A,
        const FMMDRigidBodyRuntime& B,
        const FVector& ContactNormal,
        float PenetrationDepth
    );

};