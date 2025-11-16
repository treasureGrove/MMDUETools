#pragma once
#include "CoreMinimal.h"
#include "btBulletDynamicsCommon.h"    
#include "TPMXParser.h"


struct BulletRigidBody
{
	btCollisionShape* Shape=nullptr;
	btMotionState* MotionState=nullptr;
	btRigidBody* Body=nullptr;
	//刚体物理属性

	//碰撞组和掩码
	short CollisionGroup = 0;//刚体的碰撞组，用于指定刚体所属的组
	short CollisionMask = -1;//刚体的碰撞掩码,用于指定刚体可以与哪些组发生碰撞

	FTransform UEWorldTransform;//UE坐标系下的变换
	btTransform BulletWorldTransform;//Bullet坐标系下的变换

	FTransform PrevUEWorldTransform;//上一次更新时的UE坐标系下的变换
	btTransform PrevBulletWorldTransform;//上一次更新时的Bullet坐标系下的变换

	int32 RelatedBoneIndex = -1;//关联的骨骼索引

	FTransform BoneToRigid = FTransform::Identity;//刚体相对于骨骼的偏移变换
	FTransform RigidToBone = FTransform::Identity;//骨骼相对于刚体的偏移变换

	int32 DebugID = -1;//用于调试的ID
};


class FMMDPhysicsSimulator
{
public:
    FMMDPhysicsSimulator() = default;

	bool InitializeFromPMX(const PMXDatas& PMXData,
		USkeletalMeshComponent* InSkelComp,
		float InUnitScale = 8.f,
		int32 InMaxSubSteps = 3,
		float InFixedTimeStep = 1.f / 60.f);
	void GameThreadTick(float DeltaSeconds);

	void InitializeBulletWorld();
	void InitializeRigidBody(const PMXDatas PMXData);
	void InitializeJoints(const PMXDatas PMXData);
	void StepSimulationMMD(float DeltaSeconds);

	// MMD Tick 流程
	void PreSyncKinematicFromBones(const TArray<FTransform>& BoneWorldUE);
	void PostSyncBonesFromPhysics(TArray<FTransform>& InOutBoneWorldUE);
	void TickMMDPhysics(float DeltaSeconds, TArray<FTransform>& InOutBoneWorldUE);

	USkeletalMeshComponent* GetOwnerSkelComp() const { return OwnerSkelComp.Get(); }
private:
	// Bullet 物理世界相关
	btDefaultCollisionConfiguration* CollisionConfiguration = nullptr;
	btCollisionDispatcher* Dispatcher = nullptr;
	btDbvtBroadphase* Broadphase = nullptr;
	btSequentialImpulseConstraintSolver* Solver = nullptr;
	btDiscreteDynamicsWorld* DynamicsWorld = nullptr;

	//Bullet 刚体列表
	TArray<BulletRigidBody> BulletRigidBodies;
	TArray<btGeneric6DofSpring2Constraint*> BulletJoints;
	TWeakObjectPtr<USkeletalMeshComponent> OwnerSkelComp;
	bool bInitialized = false;
	float UnitScale = 8.f;
	int32 MaxSubSteps = 3;
	float FixedTimeStep = 1.f / 60.f;

};
//class FMMDPhysicsSimManager
//{
//public:
//	static FMMDPhysicsSimManager& Get();
//
//	// 返回该组件的模拟器（没有则创建并返回）
//	FMMDPhysicsSimulator* RegisterOrGet(USkeletalMeshComponent* SkelComp,
//		const PMXDatas* OptionalPMX = nullptr,
//		float UnitScale = 8.f,
//		int32 MaxSubSteps = 3,
//		float FixedTimeStep = 1.f / 60.f);
//
//	// 仅查找，不创建
//	FMMDPhysicsSimulator* Find(USkeletalMeshComponent* SkelComp);
//
//	// 解除并销毁该组件对应的模拟器（角色销毁时调用）
//	void Unregister(USkeletalMeshComponent* SkelComp);
//
//	// 游戏线程全局 Tick（自动注册的 Ticker 会调用）
//	void TickAll(float DeltaSeconds);
//
//	// 显式控制 Ticker（通常首次 Register 时自动启动）
//	void StartTicker();
//	void StopTicker();
//
//private:
//	FMMDPhysicsSimManager() = default;
//
//	TMap<TWeakObjectPtr<USkeletalMeshComponent>, TUniquePtr<FMMDPhysicsSimulator>> Sims;
//	FDelegateHandle TickerHandle;
//};