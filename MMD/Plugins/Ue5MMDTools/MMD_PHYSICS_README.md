# MMD Physics System - Complete Implementation

## Overview

This implementation provides a complete MMD-compatible physics system for Unreal Engine 5, faithfully replicating the behavior of MikuMikuDance (MMD) physics while leveraging UE5's animation and rendering capabilities.

## Features

### ✅ Implemented

- **Lightweight Physics Simulation**
  - Verlet integration for stable, efficient physics
  - Multiple physics modes: Static, Dynamic, Bone Tracked
  - Configurable gravity and damping

- **Collision Detection & Response**
  - Sphere-sphere collision detection
  - Collision group filtering with bitmasks
  - Impulse-based collision response with restitution

- **Constraint System**
  - Spring6DOF constraints (MMD standard)
  - Position and rotation limits
  - Spring stiffness parameters
  - Multiple solver iterations for stability

- **PMX Integration**
  - Automatic initialization from PMX rigid body data
  - Automatic constraint creation from PMX joint data
  - Coordinate system conversion (MMD → UE5)

- **Blueprint Support**
  - Full Blueprint callable API
  - Runtime parameter adjustment
  - Physics enable/disable control

- **Debug Visualization**
  - Physics shape rendering (sphere, box, capsule)
  - Constraint visualization
  - Velocity vector display
  - Collision group color coding

- **Animation Integration**
  - MMDAnimInstance for animation blueprint support
  - Physics/animation blending (framework)
  - Compatible with UE5 animation system

### 🔄 In Progress

- Complete bone transform application
- Box and capsule collision detection
- Advanced wind and force systems
- Multi-threading optimization
- Soft body physics support

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      AMMDActor (Actor)                        │
│  ┌────────────────────────────────────────────────────────┐  │
│  │         USkeletalMeshComponent (Rendering)             │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │      UMMDPhysicsComponent (Physics Simulation)         │  │
│  │  - Physics Bones (FMMDPhysicsBone)                     │  │
│  │  - Constraints (FMMDPhysicsConstraint)                 │  │
│  │  - Verlet Integration                                  │  │
│  │  - Collision Detection                                 │  │
│  │  - Constraint Solving                                  │  │
│  └────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────────────────────────────────┐  │
│  │    UMMDPhysicsDebugDraw (Debug Visualization)          │  │
│  │  - Shape Drawing                                       │  │
│  │  - Constraint Lines                                    │  │
│  │  - Velocity Vectors                                    │  │
│  └────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
         │
         └──> UMMDAnimInstance (Animation Blueprint Integration)
              - Physics Update
              - Animation Blending
```

## Component Details

### UMMDPhysicsComponent

Main physics simulation component.

**Key Methods:**
- `InitializeFromPMXData(PMXDatas)` - Initialize from PMX file data
- `UpdatePhysics(DeltaTime)` - Main physics simulation loop
- `IntegrateVerlet(DeltaTime)` - Verlet integration step
- `CheckCollisions()` - Detect collisions between physics bones
- `SolveConstraints(DeltaTime)` - Apply spring constraints
- `ApplyDamping(DeltaTime)` - Apply linear and angular damping

**Blueprint API:**
- `SetPhysicsEnabled(bool)` - Enable/disable simulation
- `SetGravity(FVector)` - Set gravity vector
- `SetPhysicsBoneDamping(Index, Linear, Angular)` - Adjust bone damping
- `SetPhysicsBoneMass(Index, Mass)` - Adjust bone mass
- `GetPhysicsBones()` - Get all physics bones
- `GetPhysicsConstraints()` - Get all constraints

### FMMDPhysicsBone

Represents a single physics-enabled bone (rigid body).

**Properties:**
- Shape: Sphere, Box, or Capsule
- Physics Mode: Static, Dynamic, or Bone Tracked
- Mass, Damping, Friction, Restitution
- Collision Group and Mask
- Position, Velocity (current and previous for Verlet)

### FMMDPhysicsConstraint

Represents a constraint (joint) between two physics bones.

**Properties:**
- Connected rigid bodies (A and B)
- Position and rotation
- Linear and angular limits
- Spring stiffness parameters

### UMMDAnimInstance

Animation instance for MMD characters.

**Features:**
- Automatic physics component discovery
- Physics blend weight control
- Integration with UE5 animation graphs

### UMMDPhysicsDebugDraw

Debug visualization component.

**Features:**
- Draw physics shapes with color coding
- Show constraint connections
- Display velocity vectors
- Toggle different visualization modes

## Data Flow

```
PMX File
   │
   ├─→ TPMXParser::ParsePMXFile()
   │      │
   │      ├─→ PMXRigid[] (Rigid Bodies)
   │      └─→ PMXJoint[] (Joints/Constraints)
   │
   └─→ AMMDActor::BuildFromPMXData()
          │
          ├─→ TMMDMeshBuilder::BuildSkeletalMeshFromPMX()
          │      └─→ USkeletalMesh
          │
          └─→ UMMDPhysicsComponent::InitializeFromPMXData()
                 │
                 ├─→ Convert PMXRigid → FMMDPhysicsBone[]
                 ├─→ Convert PMXJoint → FMMDPhysicsConstraint[]
                 └─→ Initialize simulation state

Runtime Loop:
   TickComponent()
      │
      ├─→ IntegrateVerlet() - Update positions
      ├─→ CheckCollisions() - Detect collisions
      ├─→ SolveConstraints() - Apply springs
      ├─→ ApplyDamping() - Reduce velocities
      └─→ UpdateBoneTransforms() - Apply to mesh (TODO)
```

## Physics Algorithm

### Verlet Integration

The system uses Verlet integration for stability and simplicity:

```
Position(t+dt) = 2 * Position(t) - Position(t-dt) + Acceleration * dt²
Velocity(t) = (Position(t) - Position(t-dt)) / dt
```

**Advantages:**
- More stable than Euler integration
- Implicit velocity storage
- Good for constraints
- Suitable for game engines

### Constraint Solving

Spring constraints are solved iteratively:

```
For each constraint:
    Delta = PositionB - PositionA
    TargetDelta = ConstraintPosition
    Correction = (TargetDelta - Delta) * SpringStiffness
    
    PositionA -= Correction * (MassB / TotalMass)
    PositionB += Correction * (MassA / TotalMass)
```

Multiple iterations improve stability.

### Collision Response

Sphere-sphere collisions use impulse-based response:

```
1. Check distance < (RadiusA + RadiusB)
2. Calculate overlap and normal
3. Position correction based on mass ratio
4. Velocity correction with restitution:
   
   RelativeVelocity = VelocityB - VelocityA
   VelocityAlongNormal = Dot(RelativeVelocity, Normal)
   ImpulseMagnitude = -(1 + Restitution) * VelocityAlongNormal / (1/MassA + 1/MassB)
   
   VelocityA -= Impulse / MassA
   VelocityB += Impulse / MassB
```

## Coordinate System Conversion

MMD uses a right-handed coordinate system (Y-up), while UE5 uses left-handed (Z-up).

**Conversion:**
```cpp
UE5.X = MMD.X
UE5.Y = -MMD.Z  // Note the negation
UE5.Z = MMD.Y

// For rotations (Euler angles in radians):
UE5.Pitch = Degrees(MMD.X)
UE5.Yaw = Degrees(-MMD.Y)
UE5.Roll = Degrees(MMD.Z)
```

## Usage Quick Start

### C++ Example

```cpp
// Create and setup MMD actor
AMMDActor* Actor = World->SpawnActor<AMMDActor>();

// Load PMX file
TPMXParser Parser;
if (Parser.ParsePMXFile("Miku.pmx"))
{
    Actor->BuildFromPMXData(Parser.PMXInfo, "Miku.pmx");
    Actor->SetPhysicsEnabled(true);
}

// Adjust physics
UMMDPhysicsComponent* Physics = Actor->GetPhysicsComponent();
Physics->SetGravity(FVector(0, 0, -980));
Physics->SetPhysicsBoneDamping(0, 0.8f, 0.9f);

// Enable debug visualization
Actor->SetPhysicsDebugDrawEnabled(true);
```

### Blueprint Example

1. Add MMD Actor to level
2. Call "Build From PMX Data" or "Setup Components"
3. Call "Set Physics Enabled" (true)
4. Optional: Call "Set Physics Debug Draw Enabled" (true)

## Performance Characteristics

### Computational Complexity

- **Verlet Integration**: O(n) where n = number of physics bones
- **Collision Detection**: O(n²) naive, can be optimized to O(n log n) with spatial partitioning
- **Constraint Solving**: O(m × i) where m = number of constraints, i = iterations

### Memory Usage

Per physics bone (~100-200 bytes):
- 2 × FVector (position: current, previous) = 24 bytes
- 2 × FQuat (rotation: current, previous) = 32 bytes
- 2 × FVector (velocity: linear, angular) = 24 bytes
- Shape and physics properties = ~50 bytes

Typical MMD model: 20-50 physics bones = 2-10 KB total

### Performance Tips

1. **Adjust iterations** - Balance between stability and performance
2. **Use collision masks** - Avoid unnecessary collision checks
3. **LOD system** - Disable/simplify physics for distant actors
4. **Spatial partitioning** - For scenes with many characters
5. **Fixed timestep** - Use consistent time steps for stability

## Testing and Validation

### Unit Testing (Planned)

- Coordinate system conversion accuracy
- Collision detection correctness
- Constraint solving stability
- PMX data import correctness

### Integration Testing

- Load real MMD models with physics
- Verify visual behavior matches MMD
- Test performance with multiple characters
- Validate collision group filtering

### Debug Visualization

Enable debug drawing to verify:
- Physics shapes are correctly sized and positioned
- Constraints connect the right bones
- Velocities behave as expected
- Collision groups are properly configured

## Known Limitations

1. **Bone Transform Application** - Currently not applying physics to skeletal mesh bones
2. **Advanced Collision** - Only sphere collision implemented (box/capsule pending)
3. **Soft Bodies** - PMX soft body support not yet implemented
4. **Wind/Forces** - External force system not implemented
5. **Multi-threading** - Single-threaded physics update

## Future Enhancements

### Priority 1: Core Functionality
- [ ] Complete bone transform application
- [ ] Box collision detection
- [ ] Capsule collision detection
- [ ] Full animation blending

### Priority 2: Features
- [ ] Wind and external forces
- [ ] Soft body physics
- [ ] IK integration
- [ ] Performance profiling tools

### Priority 3: Optimization
- [ ] Multi-threading
- [ ] Spatial partitioning
- [ ] SIMD optimizations
- [ ] GPU physics (experimental)

### Priority 4: Tools
- [ ] In-editor physics tuning
- [ ] Physics asset import/export
- [ ] Visual constraint editor
- [ ] Performance profiler

## Contributing

When contributing to the physics system:

1. Maintain compatibility with PMX format
2. Preserve MMD physics behavior
3. Follow UE5 coding standards
4. Add debug visualization for new features
5. Document new parameters and functions
6. Add usage examples

## References

### MMD Resources
- [PMX Format Specification](https://gist.github.com/felixjones/f8a06bd48f9da9a4539f)
- [MMD Physics Overview](http://mikumikudance.wikia.com/wiki/Physics)
- [MMD Model Database](https://bowlroll.net/)

### Technical References
- [Verlet Integration](https://en.wikipedia.org/wiki/Verlet_integration)
- [Game Physics Engines](https://www.toptal.com/game/video-game-physics-part-i-an-introduction-to-rigid-body-dynamics)
- [Constraint Solvers](https://www.gdcvault.com/play/1020603/Physics-for-Game-Programmers-Constraints)

### UE5 Documentation
- [Animation System](https://docs.unrealengine.com/5.0/en-US/animation-system-in-unreal-engine/)
- [Physics System](https://docs.unrealengine.com/5.0/en-US/physics-in-unreal-engine/)
- [Blueprint API](https://docs.unrealengine.com/5.0/en-US/blueprints-visual-scripting-in-unreal-engine/)

## License

This implementation is part of the MMDUETools project. See main project license for details.

## Credits

- Original MMD by Yu Higuchi (樋口優)
- PMX format specification by various MMD community contributors
- Implementation by treasureGrove team

---

For detailed usage examples, see [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md)

For implementation details, see [MMD_PHYSICS_IMPLEMENTATION.md](MMD_PHYSICS_IMPLEMENTATION.md)
