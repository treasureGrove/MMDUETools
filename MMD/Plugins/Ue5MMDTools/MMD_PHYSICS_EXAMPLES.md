# MMD Physics System Usage Examples

## C++ Usage

### Basic Setup

```cpp
#include "AMMDActor.h"
#include "TPMXParser.h"
#include "MMDPhysicsComponent.h"

// In your game code (e.g., in a GameMode or Level Blueprint)
void AMyGameMode::SpawnMMDCharacter()
{
    // Create MMD Actor
    AMMDActor* MMDCharacter = GetWorld()->SpawnActor<AMMDActor>(
        FVector(0, 0, 0),
        FRotator::ZeroRotator
    );

    if (!MMDCharacter)
    {
        return;
    }

    // Load and parse PMX file
    FString PMXFilePath = FPaths::ProjectContentDir() + TEXT("Models/Miku.pmx");
    
    TPMXParser Parser;
    if (Parser.ParsePMXFile(PMXFilePath))
    {
        // Build mesh and initialize physics from PMX data
        MMDCharacter->BuildFromPMXData(Parser.PMXInfo, PMXFilePath);
        
        UE_LOG(LogTemp, Log, TEXT("MMD character loaded successfully"));
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("Failed to parse PMX file: %s"), *PMXFilePath);
    }
}
```

### Physics Control

```cpp
void ControlMMDPhysics(AMMDActor* MMDCharacter)
{
    if (!MMDCharacter)
    {
        return;
    }

    // Enable/disable physics simulation
    MMDCharacter->SetPhysicsEnabled(true);

    // Get physics component for advanced control
    UMMDPhysicsComponent* PhysicsComp = MMDCharacter->GetPhysicsComponent();
    
    if (PhysicsComp)
    {
        // Adjust gravity
        PhysicsComp->SetGravity(FVector(0, 0, -980.0f));

        // Adjust individual bone properties
        int32 HairBoneIndex = 0; // Example: first physics bone
        PhysicsComp->SetPhysicsBoneDamping(HairBoneIndex, 0.8f, 0.9f);
        PhysicsComp->SetPhysicsBoneMass(HairBoneIndex, 0.5f);

        // Get physics bones for inspection
        const TArray<FMMDPhysicsBone>& PhysicsBones = PhysicsComp->GetPhysicsBones();
        UE_LOG(LogTemp, Log, TEXT("Total physics bones: %d"), PhysicsBones.Num());
    }
}
```

### Debug Visualization

```cpp
void EnablePhysicsDebugVisualization(AMMDActor* MMDCharacter)
{
    if (!MMDCharacter)
    {
        return;
    }

    // Enable debug drawing
    MMDCharacter->SetPhysicsDebugDrawEnabled(true);

    // Advanced debug control
    UMMDPhysicsDebugDraw* DebugDraw = MMDCharacter->GetDebugDrawComponent();
    
    if (DebugDraw)
    {
        DebugDraw->SetDrawPhysicsBones(true);
        DebugDraw->SetDrawConstraints(true);
        DebugDraw->SetDrawVelocities(true);
        DebugDraw->SetDrawCollisionGroups(false);
    }
}
```

## Blueprint Usage

### Setting Up MMD Character in Blueprint

1. **Add MMD Actor to Level**
   - In the Level Editor, drag `AMMDActor` from the Place Actors panel
   - Or use SpawnActor node in Blueprint

2. **Load PMX Model**
   ```
   Event BeginPlay
   ├─→ Get MMD Actor Reference
   ├─→ Setup Components (File Path = "Content/Models/Miku.pmx")
   └─→ Set Physics Enabled (Enabled = true)
   ```

3. **Adjust Physics Parameters**
   ```
   Event Tick
   ├─→ Get MMD Actor Reference
   ├─→ Get Physics Component
   ├─→ Set Gravity (Gravity = 0, 0, -980)
   └─→ Set Physics Bone Damping (Index, Linear, Angular)
   ```

### Blueprint Functions Available

#### From AMMDActor:
- `Get Mesh Component` - Returns the skeletal mesh component
- `Get Physics Component` - Returns the physics component
- `Get Debug Draw Component` - Returns the debug visualization component
- `Set Physics Enabled` - Enable/disable physics simulation
- `Is Physics Enabled` - Check if physics is enabled
- `Set Physics Debug Draw Enabled` - Enable/disable debug visualization

#### From UMMDPhysicsComponent:
- `Initialize From PMX Data` - Initialize physics from parsed PMX data
- `Set Physics Enabled` - Enable/disable physics
- `Set Gravity` - Set gravity vector
- `Get Gravity` - Get current gravity
- `Set Physics Bone Damping` - Adjust damping for a specific bone
- `Set Physics Bone Mass` - Adjust mass for a specific bone
- `Get Physics Bones` - Get array of all physics bones
- `Get Physics Constraints` - Get array of all constraints

#### From UMMDPhysicsDebugDraw:
- `Set Debug Draw Enabled` - Enable/disable debug drawing
- `Is Debug Draw Enabled` - Check if debug drawing is enabled
- `Set Draw Physics Bones` - Toggle physics bone visualization
- `Set Draw Constraints` - Toggle constraint visualization
- `Set Draw Collision Groups` - Toggle collision group colors
- `Set Draw Velocities` - Toggle velocity vector visualization

### Example: Wind Effect

```cpp
// Apply wind force to physics bones
void ApplyWindToMMDCharacter(AMMDActor* MMDCharacter, FVector WindDirection, float WindStrength)
{
    UMMDPhysicsComponent* PhysicsComp = MMDCharacter->GetPhysicsComponent();
    
    if (!PhysicsComp)
    {
        return;
    }

    // Get physics bones
    const TArray<FMMDPhysicsBone>& PhysicsBones = PhysicsComp->GetPhysicsBones();
    
    // Apply wind force to dynamic bones
    for (int32 i = 0; i < PhysicsBones.Num(); ++i)
    {
        const FMMDPhysicsBone& Bone = PhysicsBones[i];
        
        if (Bone.PhysicsMode == EMMDPhysicsMode::Dynamic)
        {
            // Calculate wind force based on bone properties
            FVector WindForce = WindDirection.GetSafeNormal() * WindStrength;
            
            // Apply force by adjusting gravity temporarily
            // This is a simplified approach; full implementation would
            // require extending the physics component
        }
    }
}
```

### Example: Physics Parameter Animation

```cpp
// Animate physics parameters over time
class UMMDPhysicsAnimator : public UActorComponent
{
public:
    virtual void TickComponent(float DeltaTime, ...) override
    {
        Super::TickComponent(DeltaTime, ...);
        
        if (!MMDCharacter)
        {
            return;
        }

        // Animate damping based on time
        float TimeSin = FMath::Sin(GetWorld()->GetTimeSeconds() * 2.0f);
        float Damping = FMath::GetMappedRangeValueClamped(
            FVector2D(-1.0f, 1.0f),
            FVector2D(0.3f, 0.9f),
            TimeSin
        );

        UMMDPhysicsComponent* PhysicsComp = MMDCharacter->GetPhysicsComponent();
        if (PhysicsComp)
        {
            // Apply to all bones
            const TArray<FMMDPhysicsBone>& Bones = PhysicsComp->GetPhysicsBones();
            for (int32 i = 0; i < Bones.Num(); ++i)
            {
                PhysicsComp->SetPhysicsBoneDamping(i, Damping, Damping);
            }
        }
    }

private:
    UPROPERTY()
    AMMDActor* MMDCharacter = nullptr;
};
```

## Animation Blueprint Integration

### Using UMMDAnimInstance

1. **Create Animation Blueprint**
   - Right-click in Content Browser → Animation → Animation Blueprint
   - Select your MMD skeletal mesh as the target
   - Set Parent Class to `UMMDAnimInstance`

2. **Access Physics in Animation Graph**
   ```
   In Event Graph:
   ├─→ Event Blueprint Update Animation
   ├─→ Set Physics Blend Weight (Weight = 1.0)
   └─→ Enable Physics Simulation (Enable = true)
   ```

3. **Blend Physics with Animation**
   ```
   In Anim Graph:
   ├─→ Slot Node (Default Slot)
   ├─→ [Physics blending happens automatically in UMMDAnimInstance]
   └─→ Output Pose
   ```

### Custom Animation Logic

```cpp
// In your custom Animation Instance derived from UMMDAnimInstance
void UMyMMDAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
    Super::NativeUpdateAnimation(DeltaSeconds);

    // Custom logic to control physics based on animation state
    if (bIsJumping)
    {
        // Reduce physics influence during jumps
        SetPhysicsBlendWeight(0.3f);
    }
    else if (bIsLanding)
    {
        // Increase physics influence for landing impact
        SetPhysicsBlendWeight(1.5f);
    }
    else
    {
        // Normal physics
        SetPhysicsBlendWeight(1.0f);
    }
}
```

## Performance Tips

1. **Adjust Constraint Iterations**
   - Lower iterations = Better performance, less stable
   - Higher iterations = Worse performance, more stable
   - Default is 4, suitable for most cases

2. **Use LOD for Physics**
   - Disable physics for distant characters
   - Reduce constraint iterations for background characters

3. **Collision Group Optimization**
   - Only enable collision between necessary groups
   - Use collision masks efficiently

4. **Gravity Settings**
   - Standard MMD gravity: (0, 0, -980) cm/s²
   - Adjust for desired effect (lighter = more floaty)

## Troubleshooting

### Physics Not Working
- Check that physics is enabled: `IsPhysicsEnabled()`
- Verify PMX data has rigid bodies: `GetPhysicsBones().Num() > 0`
- Ensure skeletal mesh component exists

### Unstable Physics
- Reduce time step or enable frame rate smoothing
- Increase constraint iterations
- Increase damping values
- Check for extreme mass values

### Collision Issues
- Verify collision groups and masks are set correctly
- Check shape sizes are reasonable
- Enable debug visualization to see collision shapes

### Performance Issues
- Reduce number of physics bones if possible
- Lower constraint iterations
- Disable physics for off-screen characters
- Use spatial partitioning for collision detection
