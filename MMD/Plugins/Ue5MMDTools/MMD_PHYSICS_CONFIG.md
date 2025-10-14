# MMD Physics Configuration Guide

## Overview

This guide explains how to configure and tune MMD physics parameters for optimal results.

## Physics Component Configuration

### Basic Settings

```cpp
// In C++ or via Blueprint
UMMDPhysicsComponent* Physics = Actor->GetPhysicsComponent();

// Enable/disable physics
Physics->SetPhysicsEnabled(true);

// Set gravity (MMD standard: -980 cm/s²)
Physics->SetGravity(FVector(0, 0, -980.0f));
```

### Global Parameters

These are set on the `UMMDPhysicsComponent`:

| Parameter | Default | Range | Description |
|-----------|---------|-------|-------------|
| `bPhysicsEnabled` | true | bool | Master enable/disable switch |
| `Gravity` | (0,0,-980) | FVector | Gravity acceleration in cm/s² |
| `PhysicsBlendWeight` | 1.0 | 0.0-1.0 | Blend between physics and animation |
| `ConstraintIterations` | 4 | 1-10 | Number of constraint solver iterations |

### Per-Bone Parameters

Each physics bone (`FMMDPhysicsBone`) has these tunable parameters:

| Parameter | Description | Typical Range |
|-----------|-------------|---------------|
| `Mass` | Bone mass in kg | 0.1 - 10.0 |
| `LinearDamping` | Velocity damping (0=none, 1=full) | 0.3 - 0.9 |
| `AngularDamping` | Rotation damping (0=none, 1=full) | 0.3 - 0.9 |
| `Restitution` | Bounciness (0=none, 1=full) | 0.0 - 0.8 |
| `Friction` | Surface friction | 0.3 - 0.9 |

## Physics Modes

Each bone can be in one of three modes:

### Static (0)
- Kinematic, does not move
- Follows bone transform directly
- Use for: Fixed accessories, rigid parts

```cpp
// In PMX file, PhysicsMode = 0
// Or programmatically:
Bone.PhysicsMode = EMMDPhysicsMode::Static;
```

### Dynamic (1)
- Fully simulated physics
- Affected by gravity, collisions, constraints
- Use for: Hair, clothes, loose accessories

```cpp
// In PMX file, PhysicsMode = 1
// Or programmatically:
Bone.PhysicsMode = EMMDPhysicsMode::Dynamic;
```

### Bone Tracked (2)
- Hybrid mode
- Follows bone but with physics influence
- Use for: Semi-rigid parts, weighted physics

```cpp
// In PMX file, PhysicsMode = 2
// Or programmatically:
Bone.PhysicsMode = EMMDPhysicsMode::BoneTracked;
```

## Collision Configuration

### Collision Groups

MMD supports 16 collision groups (0-15). Each bone belongs to one group.

**Common grouping:**
- Group 0: Body/torso
- Group 1: Head
- Group 2: Arms
- Group 3: Legs
- Group 4: Hair
- Group 5: Skirt
- Group 6-15: Custom accessories

### Collision Masks

Each bone has a 16-bit collision mask determining which groups it collides with.

**Examples:**

```cpp
// Collide with groups 0, 1, 2 (body, head, arms)
CollisionMask = 0b0000000000000111 = 0x0007

// Collide with all groups
CollisionMask = 0xFFFF

// Collide with no groups (physics-only, no collision)
CollisionMask = 0x0000

// Collide with groups 4 and 5 (hair and skirt)
CollisionMask = 0b0000000000110000 = 0x0030
```

**Common configurations:**

| Use Case | Group | Mask | Description |
|----------|-------|------|-------------|
| Body | 0 | 0xFFFE | Collide with everything except self |
| Hair | 4 | 0x0003 | Collide with head and body only |
| Skirt | 5 | 0x0003 | Collide with body and legs |
| Loose accessory | 6 | 0x003F | Collide with body, head, limbs |

## Tuning Guide

### Hair Physics

**Goal:** Flowing, responsive hair that doesn't clip

**Recommended settings:**
```cpp
Mass: 0.5 - 1.0
LinearDamping: 0.7 - 0.85
AngularDamping: 0.75 - 0.9
Restitution: 0.1 - 0.3
Friction: 0.4 - 0.6
PhysicsMode: Dynamic
CollisionGroup: 4
CollisionMask: 0x0003 (body and head)
```

**If hair is too bouncy:**
- Increase damping (0.8 - 0.9)
- Decrease restitution (0.0 - 0.1)
- Increase constraint iterations (6-8)

**If hair is too stiff:**
- Decrease damping (0.5 - 0.7)
- Increase spring stiffness in constraints
- Decrease mass (0.3 - 0.5)

### Cloth/Skirt Physics

**Goal:** Natural cloth movement, follows body

**Recommended settings:**
```cpp
Mass: 1.0 - 2.0
LinearDamping: 0.6 - 0.8
AngularDamping: 0.7 - 0.85
Restitution: 0.0 - 0.2
Friction: 0.6 - 0.8
PhysicsMode: Dynamic
CollisionGroup: 5
CollisionMask: 0x0013 (body, head, legs)
```

**If cloth clips through body:**
- Increase collision iterations
- Add more physics bones
- Adjust collision group masks
- Increase position correction strength

**If cloth is too floaty:**
- Increase mass (2.0 - 3.0)
- Increase damping (0.8 - 0.9)
- Adjust gravity if needed

### Accessories

**Goal:** Natural movement, stays attached

**Recommended settings:**
```cpp
Mass: 0.3 - 1.0 (depends on size)
LinearDamping: 0.5 - 0.7
AngularDamping: 0.6 - 0.8
Restitution: 0.2 - 0.4
Friction: 0.5 - 0.7
PhysicsMode: BoneTracked or Dynamic
CollisionGroup: 6-15
CollisionMask: 0x003F (body parts)
```

## Constraint Tuning

### Spring Constraints

Spring constraints connect two rigid bodies with 6DOF (degrees of freedom).

**Parameters:**

| Parameter | Description | Typical Range |
|-----------|-------------|---------------|
| `SpringLinear` | Linear spring stiffness (x,y,z) | 0.0 - 100.0 |
| `SpringAngular` | Angular spring stiffness (x,y,z) | 0.0 - 100.0 |
| `LinearLowerLimit` | Min position offset | -100 to 0 |
| `LinearUpperLimit` | Max position offset | 0 to 100 |
| `AngularLowerLimit` | Min rotation (radians) | -π to 0 |
| `AngularUpperLimit` | Max rotation (radians) | 0 to π |

**Common presets:**

**Stiff joint (bone-like):**
```cpp
SpringLinear: (80, 80, 80)
SpringAngular: (80, 80, 80)
LinearLimits: ±1 cm
AngularLimits: ±5°
```

**Flexible joint (hair, cloth):**
```cpp
SpringLinear: (20, 20, 20)
SpringAngular: (10, 10, 10)
LinearLimits: ±5 cm
AngularLimits: ±30°
```

**Very loose (dangling accessory):**
```cpp
SpringLinear: (5, 5, 5)
SpringAngular: (2, 2, 2)
LinearLimits: ±10 cm
AngularLimits: ±90°
```

## Performance Tuning

### Optimization Levels

**High Quality (60 FPS, close-up)**
```cpp
ConstraintIterations: 6-8
EnableCollisions: true
PhysicsEnabled: all bones
UpdateRate: Every frame
```

**Medium Quality (60 FPS, gameplay)**
```cpp
ConstraintIterations: 4-5
EnableCollisions: true
PhysicsEnabled: important bones only
UpdateRate: Every frame
```

**Low Quality (mobile, distant)**
```cpp
ConstraintIterations: 2-3
EnableCollisions: false
PhysicsEnabled: major bones only
UpdateRate: Every 2-3 frames
```

### LOD System

Implement distance-based physics LOD:

```cpp
float Distance = (CameraLocation - ActorLocation).Size();

if (Distance < 500.0f) // Close
{
    Physics->SetConstraintIterations(6);
    Physics->SetPhysicsEnabled(true);
}
else if (Distance < 2000.0f) // Medium
{
    Physics->SetConstraintIterations(3);
    Physics->SetPhysicsEnabled(true);
}
else // Far
{
    Physics->SetPhysicsEnabled(false);
}
```

## Troubleshooting

### Problem: Physics explodes/becomes unstable

**Solutions:**
1. Reduce time step or enable frame smoothing
2. Increase damping values (0.8+)
3. Increase constraint iterations (6-10)
4. Check for extreme mass values
5. Verify constraints are properly connected

### Problem: Physics is too slow/heavy

**Solutions:**
1. Increase gravity magnitude
2. Reduce mass values
3. Reduce damping
4. Check spring stiffness isn't too high

### Problem: Collisions not working

**Solutions:**
1. Verify collision groups/masks are correct
2. Check shape sizes are reasonable
3. Enable debug visualization
4. Ensure physics is enabled
5. Check bones are in Dynamic mode

### Problem: Jittery/vibrating physics

**Solutions:**
1. Increase damping (0.8-0.9)
2. Reduce spring stiffness
3. Increase constraint iterations
4. Use fixed time step
5. Check for constraint conflicts

### Problem: Hair/cloth clips through body

**Solutions:**
1. Increase constraint iterations
2. Add more collision shapes
3. Increase collision shape sizes slightly
4. Adjust collision masks
5. Use smaller physics bones

## Advanced Configuration

### Custom Gravity Direction

For special effects (underwater, space, etc.):

```cpp
// Underwater (slower fall)
Physics->SetGravity(FVector(0, 0, -300.0f));

// Zero gravity
Physics->SetGravity(FVector::ZeroVector);

// Custom direction (wind effect)
Physics->SetGravity(FVector(200, 0, -980));
```

### Per-Frame Physics Adjustment

```cpp
void AMyMMDActor::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    
    // Adjust physics based on animation state
    if (bIsJumping)
    {
        // Reduce physics influence during jumps
        PhysicsComponent->SetPhysicsBlendWeight(0.3f);
    }
    else if (Velocity.Size() > 500.0f)
    {
        // Increase damping when moving fast
        for (int32 i = 0; i < PhysicsComponent->GetPhysicsBones().Num(); ++i)
        {
            PhysicsComponent->SetPhysicsBoneDamping(i, 0.9f, 0.95f);
        }
    }
    else
    {
        // Normal physics
        PhysicsComponent->SetPhysicsBlendWeight(1.0f);
    }
}
```

### Wind Effect Simulation

```cpp
// Add wind to gravity
FVector BaseGravity = FVector(0, 0, -980);
FVector WindForce = FVector(100 * FMath::Sin(Time * 2.0f), 50, 0);
Physics->SetGravity(BaseGravity + WindForce);
```

## Configuration Files

### Saving Physics Configuration

```cpp
// Save physics configuration to file
void SavePhysicsConfig(UMMDPhysicsComponent* Physics, const FString& ConfigPath)
{
    TArray<FString> ConfigLines;
    
    const TArray<FMMDPhysicsBone>& Bones = Physics->GetPhysicsBones();
    for (int32 i = 0; i < Bones.Num(); ++i)
    {
        const FMMDPhysicsBone& Bone = Bones[i];
        ConfigLines.Add(FString::Printf(TEXT("%s,%f,%f,%f,%f,%f"),
            *Bone.BoneName,
            Bone.Mass,
            Bone.LinearDamping,
            Bone.AngularDamping,
            Bone.Restitution,
            Bone.Friction));
    }
    
    FFileHelper::SaveStringArrayToFile(ConfigLines, *ConfigPath);
}
```

### Loading Physics Configuration

```cpp
// Load physics configuration from file
void LoadPhysicsConfig(UMMDPhysicsComponent* Physics, const FString& ConfigPath)
{
    TArray<FString> ConfigLines;
    if (FFileHelper::LoadFileToStringArray(ConfigLines, *ConfigPath))
    {
        for (const FString& Line : ConfigLines)
        {
            TArray<FString> Parts;
            Line.ParseIntoArray(Parts, TEXT(","));
            
            if (Parts.Num() >= 6)
            {
                FString BoneName = Parts[0];
                float Mass = FCString::Atof(*Parts[1]);
                float LinearDamping = FCString::Atof(*Parts[2]);
                float AngularDamping = FCString::Atof(*Parts[3]);
                float Restitution = FCString::Atof(*Parts[4]);
                float Friction = FCString::Atof(*Parts[5]);
                
                // Find bone and apply settings
                // (Implementation depends on your bone lookup system)
            }
        }
    }
}
```

## Best Practices

1. **Start with defaults** - Use PMX file values as baseline
2. **Tune incrementally** - Small changes, test frequently
3. **Use debug visualization** - See what physics is doing
4. **Profile performance** - Monitor frame time impact
5. **Document changes** - Keep notes on what works
6. **Test edge cases** - Extreme movements, collisions
7. **Validate with reference** - Compare to MMD if possible

## Reference Values

### Typical MMD Model Physics

| Component | Mass | Linear Damp | Angular Damp | Restitution |
|-----------|------|-------------|--------------|-------------|
| Hair front | 1.0 | 0.75 | 0.80 | 0.2 |
| Hair back | 1.5 | 0.80 | 0.85 | 0.15 |
| Hair side | 1.2 | 0.78 | 0.82 | 0.18 |
| Skirt front | 2.0 | 0.70 | 0.75 | 0.1 |
| Skirt back | 2.2 | 0.72 | 0.77 | 0.08 |
| Ribbon | 0.5 | 0.65 | 0.70 | 0.3 |
| Sleeve | 1.8 | 0.75 | 0.80 | 0.05 |

These are typical values and should be adjusted based on the specific model and desired effect.
