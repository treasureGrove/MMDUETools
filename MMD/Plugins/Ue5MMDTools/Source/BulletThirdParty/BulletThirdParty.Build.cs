using UnrealBuildTool;
using System.IO;

public class BulletThirdParty : ModuleRules
{
    public BulletThirdParty(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;
        // Path to vendorized bullet source relative to module directory
        string BulletRoot = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", "ThirdParty", "bullet3-master"));
        PublicIncludePaths.Add(Path.Combine(BulletRoot, "src"));

        // Collect source files to compile into static lib for multiple platforms
        if (Target.Platform == UnrealTargetPlatform.Win64)
        {
            // Example: compile subset of Bullet sources
            string[] BulletSources = new string[] {
                Path.Combine(BulletRoot, "src", "BulletCollision", "CollisionDispatch", "btDefaultCollisionConfiguration.cpp"),
                Path.Combine(BulletRoot, "src", "BulletCollision", "CollisionShapes", "btBoxShape.cpp"),
                Path.Combine(BulletRoot, "src", "BulletCollision", "CollisionShapes", "btSphereShape.cpp"),
                Path.Combine(BulletRoot, "src", "BulletCollision", "CollisionShapes", "btCapsuleShape.cpp"),
                Path.Combine(BulletRoot, "src", "BulletCollision", "NarrowPhaseCollision", "GjkEpa2.cpp"),
                Path.Combine(BulletRoot, "src", "BulletCollision", "CollisionDispatch", "CollisionWorld.cpp"),
                Path.Combine(BulletRoot, "src", "BulletDynamics", "Dynamics", "RigidBody.cpp"),
                Path.Combine(BulletRoot, "src", "BulletDynamics", "ConstraintSolver", "SequentialImpulseConstraintSolver.cpp"),
                Path.Combine(BulletRoot, "src", "LinearMath", "btQuaternion.cpp"),
                Path.Combine(BulletRoot, "src", "LinearMath", "btMatrix3x3.cpp"),
            };

            foreach (string src in BulletSources)
            {
                PublicAdditionalLibraries.Add(src);
            }
        }
    }
}
