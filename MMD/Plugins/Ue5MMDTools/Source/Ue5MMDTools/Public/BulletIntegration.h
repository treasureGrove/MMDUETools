#pragma once

#include "CoreMinimal.h"
#include "AGN_MMDSkeletalControl.h"

class btBroadphaseInterface;
class btDefaultCollisionConfiguration;
class btCollisionDispatcher;
class btSequentialImpulseConstraintSolver;
class btDiscreteDynamicsWorld;
class btCollisionShape;
class btRigidBody;
class btTypedConstraint;

struct FBulletBody
{
    btRigidBody* Body = nullptr;
    btCollisionShape* Shape = nullptr;
    int PMXRigidIndex = -1; // 关联 PMX 刚体索引
};

class FBulletWorld
{
public:
    FBulletWorld();
    ~FBulletWorld();

    bool Init();
    void Shutdown();

    // 创建刚体（从 FMMDRigidBodyRuntime 映射）
    FBulletBody* CreateRigidBody(const FMMDRigidBodyRuntime& PMXRigid);

    // 创建 6DOF Spring 约束（从 FMMDJointRuntime 映射）
    btTypedConstraint* Create6DofSpringConstraint(const FMMDJointRuntime& Joint, FBulletBody* A, FBulletBody* B);

    // 删除刚体/约束
    void RemoveRigidBody(FBulletBody* Body);
    void RemoveConstraint(btTypedConstraint* Constraint);

    // 步进物理
    void StepSimulation(float TimeStep, int MaxSubSteps = 2);

    // 同步骨骼：写入 kinematic bodies / 读回 dynamic bodies
    void SyncBonesToPhysics(const TArray<FMMDRigidBodyRuntime>& Runtimes);
    void SyncPhysicsToBones(TArray<FMMDRigidBodyRuntime>& Runtimes);

private:
    btBroadphaseInterface* Broadphase = nullptr;
    btDefaultCollisionConfiguration* CollisionConfig = nullptr;
    btCollisionDispatcher* Dispatcher = nullptr;
    btSequentialImpulseConstraintSolver* Solver = nullptr;
    btDiscreteDynamicsWorld* World = nullptr;

    TArray<FBulletBody*> Bodies;
    TArray<btTypedConstraint*> Constraints;
};
