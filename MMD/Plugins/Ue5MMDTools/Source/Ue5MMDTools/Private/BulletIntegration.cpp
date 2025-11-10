#include "BulletIntegration.h"
#include <btBulletDynamicsCommon.h>

FBulletWorld::FBulletWorld()
    : Broadphase(nullptr), CollisionConfig(nullptr), Dispatcher(nullptr), Solver(nullptr), World(nullptr)
{
}

FBulletWorld::~FBulletWorld()
{
    Shutdown();
}

bool FBulletWorld::Init()
{
    CollisionConfig = new btDefaultCollisionConfiguration();
    Dispatcher = new btCollisionDispatcher(CollisionConfig);
    Broadphase = new btDbvtBroadphase();
    Solver = new btSequentialImpulseConstraintSolver();
    World = new btDiscreteDynamicsWorld(Dispatcher, Broadphase, Solver, CollisionConfig);
    World->setGravity(btVector3(0, 0, -9.8f * 100.0f));
    return World != nullptr;
}

void FBulletWorld::Shutdown()
{
    if (!World) return;
    for (int i = Constraints.Num()-1; i>=0; --i)
    {
        World->removeConstraint(Constraints[i]);
        delete Constraints[i];
    }
    Constraints.Empty();

    for (int i = Bodies.Num()-1; i>=0; --i)
    {
        FBulletBody* b = Bodies[i];
        if (b->Body)
        {
            World->removeRigidBody(b->Body);
            delete b->Body->getMotionState();
            delete b->Body;
        }
        if (b->Shape) delete b->Shape;
        delete b;
    }
    Bodies.Empty();

    delete World; World = nullptr;
    delete Solver; Solver = nullptr;
    delete Broadphase; Broadphase = nullptr;
    delete Dispatcher; Dispatcher = nullptr;
    delete CollisionConfig; CollisionConfig = nullptr;
}

static btCollisionShape* CreateShapeFromPMX(const FMMDRigidBodyRuntime& R)
{
    if (R.ShapeType == 0) // sphere
    {
        float radius = FMath::Abs(R.Size.X);
        return new btSphereShape(radius);
    }
    else if (R.ShapeType == 1) // box
    {
        btVector3 halfExtents(R.Size.X * 0.5f, R.Size.Y * 0.5f, R.Size.Z * 0.5f);
        return new btBoxShape(halfExtents);
    }
    else if (R.ShapeType == 2) // capsule (PMX: r=size.x, h=size.y; axis = local Y)
    {
        float radius = FMath::Abs(R.Size.X);
        float height = FMath::Abs(R.Size.Y);
        return new btCapsuleShape(radius, height);
    }
    return nullptr;
}

FBulletBody* FBulletWorld::CreateRigidBody(const FMMDRigidBodyRuntime& PMXRigid)
{
    if (!World) return nullptr;
    btCollisionShape* shape = CreateShapeFromPMX(PMXRigid);
    if (!shape) return nullptr;

    btTransform startTransform;
    startTransform.setIdentity();
    startTransform.setOrigin(btVector3(PMXRigid.Position.X, PMXRigid.Position.Y, PMXRigid.Position.Z));
    FQuat q = PMXRigid.PrevRotation;
    startTransform.setRotation(btQuaternion(q.X, q.Y, q.Z, q.W));

    btScalar mass = (PMXRigid.PhysicsMode == 1) ? PMXRigid.Mass : 0.0f;
    bool isKinematic = (PMXRigid.PhysicsMode == 2);

    btVector3 localInertia(0,0,0);
    if (mass > 0.0f)
        shape->calculateLocalInertia(mass, localInertia);

    btDefaultMotionState* motionState = new btDefaultMotionState(startTransform);
    btRigidBody::btRigidBodyConstructionInfo ci(mass, motionState, shape, localInertia);
    ci.m_friction = PMXRigid.Friction;
    ci.m_restitution = PMXRigid.Restitution;
    btRigidBody* body = new btRigidBody(ci);

    if (isKinematic)
    {
        body->setCollisionFlags(body->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
        body->setActivationState(DISABLE_DEACTIVATION);
    }
    World->addRigidBody(body);

    FBulletBody* out = new FBulletBody();
    out->Body = body;
    out->Shape = shape;
    out->PMXRigidIndex = PMXRigid.RelatedBoneIndex;
    Bodies.Add(out);
    return out;
}

btTypedConstraint* FBulletWorld::Create6DofSpringConstraint(const FMMDJointRuntime& Joint, FBulletBody* A, FBulletBody* B)
{
    if (!A || !B || !World) return nullptr;
    btTransform frameInA, frameInB;
    frameInA.setIdentity(); frameInB.setIdentity();
    frameInA.setOrigin(btVector3(Joint.Position.X, Joint.Position.Y, Joint.Position.Z));
    FQuat qA = FQuat::MakeFromEuler(FVector(Joint.Rotation.X, Joint.Rotation.Y, Joint.Rotation.Z) * (180.0f/PI));
    frameInA.setRotation(btQuaternion(qA.X, qA.Y, qA.Z, qA.W));
    frameInB = frameInA;

    btGeneric6DofSpringConstraint* c = new btGeneric6DofSpringConstraint(*A->Body, *B->Body, frameInA, frameInB, true);

    c->setLinearLowerLimit(btVector3(Joint.LimitPosLower.X, Joint.LimitPosLower.Y, Joint.LimitPosLower.Z));
    c->setLinearUpperLimit(btVector3(Joint.LimitPosUpper.X, Joint.LimitPosUpper.Y, Joint.LimitPosUpper.Z));
    c->setAngularLowerLimit(btVector3(Joint.LimitRotLower.X, Joint.LimitRotLower.Y, Joint.LimitRotLower.Z));
    c->setAngularUpperLimit(btVector3(Joint.LimitRotUpper.X, Joint.LimitRotUpper.Y, Joint.LimitRotUpper.Z));

    for (int i = 0; i < 3; ++i)
    {
        float k = Joint.SpringPos[i];
        if (k > 0.0f)
        {
            c->enableSpring(i, true);
            c->setStiffness(i, k);
            c->setDamping(i, 0.1f);
        }
        int angIndex = 3 + i;
        float krot = Joint.SpringRot[i];
        if (krot > 0.0f)
        {
            c->enableSpring(angIndex, true);
            c->setStiffness(angIndex, krot);
            c->setDamping(angIndex, 0.1f);
        }
    }

    World->addConstraint(c, true);
    Constraints.Add(c);
    return c;
}

void FBulletWorld::RemoveRigidBody(FBulletBody* Body)
{
    if (!Body || !World) return;
    World->removeRigidBody(Body->Body);
    delete Body->Body->getMotionState();
    delete Body->Body;
    if (Body->Shape) delete Body->Shape;
    Bodies.Remove(Body);
    delete Body;
}

void FBulletWorld::RemoveConstraint(btTypedConstraint* Constraint)
{
    if (!Constraint || !World) return;
    World->removeConstraint(Constraint);
    Constraints.Remove(Constraint);
    delete Constraint;
}

void FBulletWorld::StepSimulation(float TimeStep, int MaxSubSteps)
{
    if (!World) return;
    World->stepSimulation(TimeStep, MaxSubSteps, 1.0f / 120.0f);
}

void FBulletWorld::SyncBonesToPhysics(const TArray<FMMDRigidBodyRuntime>& Runtimes)
{
    for (FBulletBody* B : Bodies)
    {
        if (!B || !B->Body) continue;
        int idx = B->PMXRigidIndex;
        if (idx >= 0 && Runtimes.IsValidIndex(idx))
        {
            const FMMDRigidBodyRuntime& R = Runtimes[idx];
            if (R.PhysicsMode == 2)
            {
                btTransform t;
                t.setIdentity();
                t.setOrigin(btVector3(R.Position.X, R.Position.Y, R.Position.Z));
                FQuat q = R.PrevRotation;
                t.setRotation(btQuaternion(q.X, q.Y, q.Z, q.W));
                B->Body->setWorldTransform(t);
                B->Body->getMotionState()->setWorldTransform(t);
            }
        }
    }
}

void FBulletWorld::SyncPhysicsToBones(TArray<FMMDRigidBodyRuntime>& Runtimes)
{
    for (FBulletBody* B : Bodies)
    {
        if (!B || !B->Body) continue;
        int idx = B->PMXRigidIndex;
        if (idx >= 0 && Runtimes.IsValidIndex(idx))
        {
            FMMDRigidBodyRuntime& R = Runtimes[idx];
            if (R.PhysicsMode == 1)
            {
                btTransform t;
                B->Body->getMotionState()->getWorldTransform(t);
                btVector3 pos = t.getOrigin();
                btQuaternion rot = t.getRotation();
                R.Position = FVector(pos.x(), pos.y(), pos.z());
                R.PrevRotation = FQuat(rot.x(), rot.y(), rot.z(), rot.w());
            }
        }
    }
}
