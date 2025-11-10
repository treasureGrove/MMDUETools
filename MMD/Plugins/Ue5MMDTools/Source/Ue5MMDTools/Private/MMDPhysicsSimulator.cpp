#include "MMDPhysicsSimulator.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Math/UnrealMathUtility.h"
#include "Logging/LogMacros.h"
#include "BulletIntegration.h"

#pragma region 碰撞检测工具函数
static FVector ClosestPointOnSegment(const FVector& A,const FVector& B,const FVector& P,float &Out) {
	const FVector AB = B - A;
    const float AB2 = AB.SizeSquared();
    if (AB2 <= KINDA_SMALL_NUMBER)
    {
        Out = 0.0f;
        return A;
    }
	const float t = FVector::DotProduct(P - A, AB) / AB2;
    Out = FMath::Clamp(t, 0.0f, 1.0f);
    return A + AB * Out;

}
static void ClosestPtSegmentSegment(const FVector& P1, const FVector& Q1, const FVector& P2, const FVector& Q2, FVector& OutP, FVector& OutQ, float& OutS, float& OutT)
{
    const FVector d1 = Q1 - P1; // Direction vector of segment S1
    const FVector d2 = Q2 - P2; // Direction vector of segment S2
    const FVector r = P1 - P2;
    const float a = FVector::DotProduct(d1, d1); // squared length of segment S1
    const float e = FVector::DotProduct(d2, d2); // squared length of segment S2
    const float f = FVector::DotProduct(d2, r);

    // Check if either or both segments degenerate into points
    if (a <= KINDA_SMALL_NUMBER && e <= KINDA_SMALL_NUMBER)
    {
        OutS = 0.0f;
        OutT = 0.0f;
        OutP = P1;
        OutQ = P2;
        return;
    }
    if (a <= KINDA_SMALL_NUMBER)
    {
        // First segment degenerate to a point
        OutS = 0.0f;
        OutT = FMath::Clamp(f / e, 0.0f, 1.0f);
    }
    else
    {
        const float c = FVector::DotProduct(d1, r);
        if (e <= KINDA_SMALL_NUMBER)
        {
            // Second segment degenerate to a point
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

// Bullet integration helpers (file-scope static)
static FBulletWorld* GBulletWorld = nullptr;
static TArray<FBulletBody*> GBodiesMap; // parallel to RigidBodies by index
static TArray<btTypedConstraint*> GConstraints;

static void DestroyBulletWorld()
{
    if (!GBulletWorld) return;
    for (btTypedConstraint* c : GConstraints)
    {
        if (c) GBulletWorld->RemoveConstraint(c);
    }
    GConstraints.Empty();
    for (FBulletBody* b : GBodiesMap)
    {
        if (b) GBulletWorld->RemoveRigidBody(b);
    }
    GBodiesMap.Empty();
    GBulletWorld->Shutdown();
    delete GBulletWorld;
    GBulletWorld = nullptr;
}

static void EnsureBulletWorldAndMaps(const TArray<FMMDRigidBodyRuntime>& RigidBodies, const TArray<FMMDJointRuntime>& Joints)
{
    // initialize world if necessary
    if (!GBulletWorld)
    {
        GBulletWorld = new FBulletWorld();
        if (!GBulletWorld->Init())
        {
            delete GBulletWorld; GBulletWorld = nullptr;
            UE_LOG(LogTemp, Error, TEXT("Bullet: Failed to initialize world"));
            return;
        }
    }

    // Rebuild bodies map if size mismatch
    if (GBodiesMap.Num() != RigidBodies.Num())
    {
        // remove old
        for (FBulletBody* b : GBodiesMap)
        {
            if (b) GBulletWorld->RemoveRigidBody(b);
        }
        GBodiesMap.Empty();

        // create new bodies
        GBodiesMap.SetNum(RigidBodies.Num());
        for (int32 i = 0; i < RigidBodies.Num(); ++i)
        {
            GBodiesMap[i] = GBulletWorld->CreateRigidBody(RigidBodies[i]);
            if (GBodiesMap[i]) GBodiesMap[i]->PMXRigidIndex = i;
        }

        // Recreate constraints
        for (btTypedConstraint* c : GConstraints)
        {
            if (c) GBulletWorld->RemoveConstraint(c);
        }
        GConstraints.Empty();

        for (const FMMDJointRuntime& Joint : Joints)
        {
            if (Joint.RigidA < 0 || Joint.RigidB < 0) continue;
            if (RigidBodies.IsValidIndex(Joint.RigidA) && RigidBodies.IsValidIndex(Joint.RigidB))
            {
                FBulletBody* A = GBodiesMap[Joint.RigidA];
                FBulletBody* B = GBodiesMap[Joint.RigidB];
                if (A && B)
                {
                    btTypedConstraint* c = GBulletWorld->Create6DofSpringConstraint(Joint, A, B);
                    if (c) GConstraints.Add(c);
                }
            }
        }
    }
}

void FMMDPhysicsSimulator::SimulatePhysics(TArray<FMMDRigidBodyRuntime>&RigidBodies, TArray<FMMDJointRuntime>&Joints, TArray<FMMDSoftBodyRuntime>&SoftBodies, FComponentSpacePoseContext & Output, float DeltaTime)
{
    if(RigidBodies.Num() == 0 || DeltaTime <= KINDA_SMALL_NUMBER) return;

    // Use Bullet integration: ensure world and mapping
    EnsureBulletWorldAndMaps(RigidBodies, Joints);
    if (!GBulletWorld)
    {
        // fallback to built-in solver if Bullet not available
        const float TimeStep = 1.0f / 30.0f;
        const int32 SubStepCount = 2;
        const float SubDeltaTime = TimeStep / SubStepCount;
        SyncBoneToPhysics(RigidBodies, Output);
        for (int32 Step = 0; Step < SubStepCount; ++Step)
        {
            for (FMMDRigidBodyRuntime& Rigid : RigidBodies)
            {
                ApplyForces(Rigid, SubDeltaTime);
                IntegrateVelocity(Rigid, SubDeltaTime);//对速度进行积分
            }
            DetectAndResolveCollisions(RigidBodies, SubDeltaTime);
            const int32 Iterations = 10;
            SolveConstraints(RigidBodies, Joints, SubDeltaTime, Iterations);
        }
        TArray<FBoneTransform> OutBoneTransforms;
        WriteBackToBones(RigidBodies, OutBoneTransforms);
        return;
    }

    // Sync bones to Bullet kinematic bodies
    SyncBoneToPhysics(RigidBodies, Output);
    GBulletWorld->SyncBonesToPhysics(RigidBodies);

    // Step Bullet: use DeltaTime directly, allow internal substeps
    const float TimeStep = DeltaTime;
    const int MaxSubSteps = 2;
    GBulletWorld->StepSimulation(TimeStep, MaxSubSteps);

    // Read back dynamic bodies into runtime
    GBulletWorld->SyncPhysicsToBones(RigidBodies);

    // SolveConstraints no longer needed (handled by Bullet), but keep call to maintain API
    // Write back bones
    TArray<FBoneTransform> OutBoneTransforms;
    WriteBackToBones(RigidBodies, OutBoneTransforms);
}

// The rest of the file (SyncBoneToPhysics, ApplyForces, IntegrateVelocity, DetectAndResolveCollisions, SolveConstraints, etc.) remains unchanged and is used as fallback implementations