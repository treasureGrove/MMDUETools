#pragma once

#include "CoreMinimal.h"
#include "AGN_MMDSkeletalControl.h"
#include "btBulletDynamicsCommon.h"

class UE5MMDTOOLS_API FMMDPhysicsSimulator
{
public:
	FMMDPhysicsSimulator();
	~FMMDPhysicsSimulator();

	void SimulatePhysics(
		TArray<FMMDRigidBodyRuntime>& RigidBodies,
		TArray<FMMDJointRuntime>& Joints,
		TArray<FMMDSoftBodyRuntime>& SoftBodies,
		FComponentSpacePoseContext& Output,
		float DeltaTime
	);

	void InitializeBulletWorld();
	void DestroyBulletWorld();
	btVector3 UEToBullet(const FVector& UEVec);
	FVector BulletToUE(const btVector3& BtVec);
	btQuaternion UEToBullet(const FQuat& UEQuat);
	FQuat BulletToUE(const btQuaternion& BtQuat);
	btCollisionShape* CreateCollisionShape(const FMMDRigidBodyRuntime& RigidData);
private:
	/** Bullet 世界对象及其依赖 */
	btDiscreteDynamicsWorld* DynamicsWorld;
	btBroadphaseInterface* Broadphase;
	btDefaultCollisionConfiguration* CollisionConfig;
	btCollisionDispatcher* Dispatcher;
	btSequentialImpulseConstraintSolver* Solver;

	/** 是否已初始化 */
	bool bInitialized;

	void InitializeRigidBodies(TArray<FMMDRigidBodyRuntime>& RigidBodies,  FComponentSpacePoseContext& Output);
	void InitializeJoints(TArray<FMMDJointRuntime>& Joints,const TArray<FMMDRigidBodyRuntime>& RigidBodies);
	void UpdateRigidBodiesFromBones(TArray<FMMDRigidBodyRuntime>& RigidBodies,  FComponentSpacePoseContext& Output);
	void UpdateBonesFromRigidBodies(TArray<FMMDRigidBodyRuntime>& RigidBodies);

	TArray<btRigidBody*> CreatedBodies;
	TArray<btCollisionShape*> CreatedShapes;
	TArray<btTypedConstraint*> CreatedConstraints;
};