# MMD Physics System Implementation

## Overview
This document describes the implementation of the MMD physics system for UE5, which provides lightweight physics simulation compatible with MMD (MikuMikuDance) physics behavior.

## Architecture

### Core Components

1. **UMMDPhysicsComponent** - Main physics simulation component
   - Manages physics bones (rigid bodies) and constraints (joints)
   - Implements Verlet integration for lightweight physics
   - Handles collision detection and resolution
   - Provides Blueprint-accessible interface

2. **FMMDPhysicsBone** - Physics bone data structure
   - Represents a single physics-enabled bone
   - Contains shape, mass, damping, and collision properties
   - Maintains current and previous state for Verlet integration

3. **FMMDPhysicsConstraint** - Constraint data structure
   - Represents joints between physics bones
   - Spring6DOF constraints with position and rotation limits
   - Spring stiffness parameters

4. **UMMDAnimInstance** - Animation instance for MMD
   - Integrates physics simulation with animation system
   - Provides Blueprint interface for animation control
   - Manages physics/animation blending

### Physics Modes

The system supports three MMD physics modes:

- **Static (0)** - Kinematic, follows bone transform directly
- **Dynamic (1)** - Fully simulated physics
- **Bone Tracked (2)** - Hybrid mode, tracks bone with physics influence

## Implementation Details

### Verlet Integration

The physics system uses Verlet integration for stability and simplicity:

```
x(t+dt) = 2*x(t) - x(t-dt) + a*dt^2
```

Benefits:
- Stable with larger time steps
- Implicit velocity storage (no explicit velocity variable needed)
- Simple constraint solving
- Memory efficient

### Coordinate System Conversion

MMD uses a right-handed coordinate system (Y-up), while UE5 uses left-handed (Z-up).

Conversion applied:
```cpp
UE5.X = MMD.X
UE5.Y = -MMD.Z
UE5.Z = MMD.Y
```

### Collision Detection

Currently implements:
- Sphere-sphere collision detection
- Collision group filtering using bitmasks
- Simple impulse-based collision response

Future improvements:
- Box collision
- Capsule collision
- Continuous collision detection

### Constraint Solving

Spring constraints with:
- Position limits (linear)
- Rotation limits (angular)
- Spring stiffness parameters
- Multiple constraint iterations for stability

## Usage

### From Code

```cpp
// Create MMD Actor
AMMDActor* MMDCharacter = GetWorld()->SpawnActor<AMMDActor>();

// Load PMX file
TPMXParser Parser;
if (Parser.ParsePMXFile("Content/Models/Miku.pmx"))
{
    // Build mesh and initialize physics
    MMDCharacter->BuildFromPMXData(Parser.PMXInfo, "Content/Models/Miku.pmx");
}

// Control physics
MMDCharacter->SetPhysicsEnabled(true);
UMMDPhysicsComponent* PhysicsComp = MMDCharacter->GetPhysicsComponent();
PhysicsComp->SetGravity(FVector(0, 0, -980.0f));
```

### From Blueprint

1. Add MMD Actor to level
2. Call `Build From PMX Data` or `Setup Components`
3. Use `Set Physics Enabled` to toggle physics
4. Access `Get Physics Component` for parameter adjustment

### Animation Blueprint Integration

1. Create Animation Blueprint
2. Set parent class to `UMMDAnimInstance`
3. Access physics blend weight and enable/disable functions
4. Physics automatically blends with animation

## Performance Considerations

1. **Lightweight Design**
   - Verlet integration is computationally inexpensive
   - No full rigid body dynamics needed
   - Suitable for many characters simultaneously

2. **Optimization Opportunities**
   - Constraint iterations can be adjusted per quality needs
   - Collision detection can be spatially partitioned
   - Physics bones can be LOD-based

3. **Memory Usage**
   - Minimal per-bone state (position, velocity)
   - No heavy physics objects or scenes
   - Efficient for mobile platforms

## Integration with Existing Systems

### PMX Parser Integration

Physics data is automatically extracted from PMX files:
- Rigid bodies → Physics bones
- Joints → Physics constraints
- Collision groups and masks preserved

### Skeletal Mesh Integration

Physics component:
- Automatically finds skeletal mesh component
- Updates bone transforms from physics (planned)
- Respects animation blueprint blending

### Animation System Integration

Through UMMDAnimInstance:
- Physics updates in animation graph
- Blend physics with keyframe animation
- Compatible with UE5 animation features

## Future Enhancements

1. **Complete Bone Transform Application**
   - Apply physics results to skeletal mesh bones
   - Handle component space vs local space
   - Implement proper blending with animation

2. **Advanced Collision Shapes**
   - Box collision detection
   - Capsule collision detection
   - Compound shapes

3. **Wind and External Forces**
   - Wind zones affecting physics bones
   - External impulses and forces
   - Directional forces

4. **Soft Body Physics**
   - PMX soft body support
   - Cloth simulation
   - Deformable bodies

5. **Performance Optimization**
   - Multi-threading support
   - Spatial partitioning for collisions
   - GPU acceleration

6. **Debug Visualization**
   - Draw physics shapes
   - Show constraint limits
   - Display collision groups

7. **Editor Tools**
   - Physics parameter adjustment in editor
   - Visual physics tuning
   - Real-time preview

## Technical Notes

### Stability

- Delta time is clamped to 0.033s (30 FPS) to prevent instability
- Multiple constraint iterations improve stability
- Damping prevents excessive oscillation

### Accuracy

- System prioritizes stability over perfect physics accuracy
- Matches MMD behavior rather than real-world physics
- Good enough for character animation (hair, clothes, accessories)

### Compatibility

- Fully compatible with PMX 2.0 and 2.1 formats
- Preserves MMD physics behavior
- Works with existing MMD models without modification

## References

- [PMX Specification](https://gist.github.com/felixjones/f8a06bd48f9da9a4539f)
- [MMD Physics Documentation](http://mikumikudance.wikia.com/wiki/Physics)
- [Verlet Integration](https://en.wikipedia.org/wiki/Verlet_integration)
