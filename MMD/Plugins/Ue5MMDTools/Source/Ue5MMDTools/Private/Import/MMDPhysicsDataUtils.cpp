#include "Import/MMDPhysicsDataUtils.h"

void BuildMMDPhysicsData(const PMXDatas& PMXData,
	TArray<FMMDPhysicsRigidBodyData>& Rigids, TArray<FMMDPhysicsJointData>& Joints)
{
	Rigids.Reset(PMXData.ModelRigids.Num());
	Joints.Reset(PMXData.ModelJoints.Num());
	for (const FPMXRigid& Source : PMXData.ModelRigids)
	{
		FMMDPhysicsRigidBodyData& Target = Rigids.AddDefaulted_GetRef();
		Target.Name = Source.NameJP;
		Target.NameEN = Source.NameEN;
		Target.RelatedBoneIndex = Source.RelatedBoneIndex;
		Target.ShapeType = Source.ShapeType;
		Target.ShapeSize = Source.Size;
		Target.ShapePosition = Source.Position;
		Target.ShapeRotation = Source.Rotation;
		Target.Mass = Source.Mass;
		Target.LinearDamping = Source.LinearDamping;
		Target.AngularDamping = Source.AngularDamping;
		Target.Friction = Source.Friction;
		Target.Restitution = Source.Restitution;
		Target.CollisionGroup = Source.Group;
		Target.CollisionMask = Source.CollisionMask;
		Target.PhysicsMode = Source.PhysicsMode;
	}
	for (const FPMXJoint& Source : PMXData.ModelJoints)
	{
		FMMDPhysicsJointData& Target = Joints.AddDefaulted_GetRef();
		Target.Name = Source.NameJP;
		Target.NameEN = Source.NameEN;
		Target.JointType = Source.JointType;
		Target.RigidBodyIndexA = Source.RigidA;
		Target.RigidBodyIndexB = Source.RigidB;
		Target.Position = Source.Position;
		Target.Rotation = Source.Rotation;
		Target.LimitPositionMin = Source.LimitPosLower;
		Target.LimitPositionMax = Source.LimitPosUpper;
		Target.LimitRotationMin = Source.LimitRotLower;
		Target.LimitRotationMax = Source.LimitRotUpper;
		Target.SpringPosition = Source.SpringPos;
		Target.SpringRotation = Source.SpringRot;
	}
}
