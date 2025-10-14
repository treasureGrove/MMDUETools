# MMD Physics System Documentation Index

Welcome to the MMD Physics System documentation! This index will help you find the information you need.

## Quick Start

**New to MMD Physics?** Start here:
1. Read [MMD_PHYSICS_README.md](MMD_PHYSICS_README.md) for an overview
2. Check [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md) for usage examples
3. Try the Blueprint examples in your project

**Setting up physics?** Follow these steps:
1. Create or load an AMMDActor with a PMX model
2. Physics component is automatically initialized
3. Call `SetPhysicsEnabled(true)` to start simulation
4. Use `SetPhysicsDebugDrawEnabled(true)` to visualize

## Documentation Files

### 1. [MMD_PHYSICS_README.md](MMD_PHYSICS_README.md)
**Main documentation** - Start here for a complete overview

**Contents:**
- System overview and features
- Architecture diagrams
- Component details
- Data flow
- Quick start guide
- Performance characteristics
- Future roadmap

**Read this if you:**
- Are new to the system
- Want to understand the architecture
- Need performance information
- Want to contribute

---

### 2. [MMD_PHYSICS_IMPLEMENTATION.md](MMD_PHYSICS_IMPLEMENTATION.md)
**Technical implementation details**

**Contents:**
- Verlet integration algorithm
- Coordinate system conversion
- Collision detection details
- Constraint solving
- Integration with existing systems

**Read this if you:**
- Need to understand the algorithms
- Want to optimize performance
- Are debugging issues
- Are extending the system

---

### 3. [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md)
**Usage examples and tutorials**

**Contents:**
- C++ usage examples
- Blueprint usage examples
- Animation Blueprint integration
- Advanced scenarios (wind, LOD, etc.)
- Troubleshooting guide

**Read this if you:**
- Want to use the system in your project
- Need code examples
- Are creating Blueprints
- Have integration questions

---

### 4. [MMD_PHYSICS_CONFIG.md](MMD_PHYSICS_CONFIG.md)
**Configuration and tuning guide**

**Contents:**
- Parameter reference
- Tuning guide for different scenarios
- Collision configuration
- Performance optimization
- Best practices
- Reference values

**Read this if you:**
- Need to tune physics parameters
- Want better physics behavior
- Are optimizing performance
- Need configuration help

---

### 5. [MMD_PHYSICS_SUMMARY.md](MMD_PHYSICS_SUMMARY.md)
**Implementation summary**

**Contents:**
- What was implemented
- Statistics and metrics
- Features checklist
- Known limitations
- Next steps

**Read this if you:**
- Want a quick overview
- Need implementation statistics
- Are evaluating the system
- Want to know what's done/pending

---

### 6. [MMD_PHYSICS_ARCHITECTURE.md](MMD_PHYSICS_ARCHITECTURE.md)
**Visual architecture and diagrams**

**Contents:**
- System architecture diagrams
- Data flow diagrams
- Component relationships
- Collision detection flow
- Coordinate conversion
- Performance characteristics

**Read this if you:**
- Prefer visual documentation
- Want to see the big picture
- Are learning the system structure
- Need reference diagrams

---

### 7. [MMD_HYBRID_ARCHITECTURE.md](MMD_HYBRID_ARCHITECTURE.md)
**Overall MMD in UE5 architecture**

**Contents:**
- Complete system design
- Integration approach
- Roadmap and status
- Usage methods

**Read this if you:**
- Want to understand the full MMD system
- Need context for physics implementation
- Are working on other MMD components

---

## Quick Reference

### Common Tasks

#### Load a Model with Physics
```cpp
AMMDActor* Actor = World->SpawnActor<AMMDActor>();
TPMXParser Parser;
if (Parser.ParsePMXFile("Miku.pmx"))
{
    Actor->BuildFromPMXData(Parser.PMXInfo, "Miku.pmx");
}
```
See: [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md#basic-setup)

#### Enable Physics Simulation
```cpp
Actor->SetPhysicsEnabled(true);
```
See: [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md#physics-control)

#### Adjust Gravity
```cpp
UMMDPhysicsComponent* Physics = Actor->GetPhysicsComponent();
Physics->SetGravity(FVector(0, 0, -980));
```
See: [MMD_PHYSICS_CONFIG.md](MMD_PHYSICS_CONFIG.md#global-parameters)

#### Debug Visualization
```cpp
Actor->SetPhysicsDebugDrawEnabled(true);
```
See: [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md#debug-visualization)

#### Tune Hair Physics
See: [MMD_PHYSICS_CONFIG.md](MMD_PHYSICS_CONFIG.md#hair-physics)

#### Tune Cloth Physics
See: [MMD_PHYSICS_CONFIG.md](MMD_PHYSICS_CONFIG.md#clothskirt-physics)

---

## API Reference

### UMMDPhysicsComponent

**Main Physics API:**
- `InitializeFromPMXData(PMXDatas)` - Initialize from PMX
- `SetPhysicsEnabled(bool)` - Enable/disable
- `SetGravity(FVector)` - Set gravity
- `SetPhysicsBoneDamping(Index, Linear, Angular)` - Tune damping
- `SetPhysicsBoneMass(Index, Mass)` - Tune mass
- `GetPhysicsBones()` - Get all bones
- `GetPhysicsConstraints()` - Get all constraints

See: [MMD_PHYSICS_README.md](MMD_PHYSICS_README.md#ummaphysicscomponent)

### UMMDAnimInstance

**Animation API:**
- `SetPhysicsBlendWeight(float)` - Blend physics/animation
- `EnablePhysicsSimulation(bool)` - Enable/disable
- `GetPhysicsBlendWeight()` - Query blend weight
- `IsPhysicsSimulationEnabled()` - Query state

See: [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md#animation-blueprint-integration)

### UMMDPhysicsDebugDraw

**Debug API:**
- `SetDebugDrawEnabled(bool)` - Enable/disable
- `SetDrawPhysicsBones(bool)` - Toggle bone drawing
- `SetDrawConstraints(bool)` - Toggle constraint drawing
- `SetDrawVelocities(bool)` - Toggle velocity drawing
- `SetDrawCollisionGroups(bool)` - Toggle group colors

See: [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md#debug-visualization)

---

## Troubleshooting

### Common Issues

**Physics not working?**
- See: [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md#physics-not-working)

**Unstable physics?**
- See: [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md#unstable-physics)

**Collision issues?**
- See: [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md#collision-issues)

**Performance problems?**
- See: [MMD_PHYSICS_CONFIG.md](MMD_PHYSICS_CONFIG.md#performance-tuning)

**Jittery physics?**
- See: [MMD_PHYSICS_CONFIG.md](MMD_PHYSICS_CONFIG.md#problem-jittervibrating-physics)

---

## Code Organization

```
Source/Ue5MMDTools/
├── Public/
│   ├── MMDPhysicsComponent.h    ← Main physics component
│   ├── MMDAnimInstance.h        ← Animation integration
│   ├── MMDPhysicsDebugDraw.h    ← Debug visualization
│   └── AMMDActor.h              ← Actor (modified)
│
└── Private/
    ├── MMDPhysicsComponent.cpp  ← Physics implementation
    ├── MMDAnimInstance.cpp      ← Animation implementation
    ├── MMDPhysicsDebugDraw.cpp  ← Debug implementation
    └── AMMDActor.cpp            ← Actor (modified)
```

---

## Learning Path

### Beginner
1. Start with [MMD_PHYSICS_README.md](MMD_PHYSICS_README.md)
2. Try examples from [MMD_PHYSICS_EXAMPLES.md](MMD_PHYSICS_EXAMPLES.md)
3. Use default parameters

### Intermediate
1. Read [MMD_PHYSICS_CONFIG.md](MMD_PHYSICS_CONFIG.md)
2. Tune parameters for your models
3. Set up collision groups
4. Optimize performance

### Advanced
1. Study [MMD_PHYSICS_IMPLEMENTATION.md](MMD_PHYSICS_IMPLEMENTATION.md)
2. Review [MMD_PHYSICS_ARCHITECTURE.md](MMD_PHYSICS_ARCHITECTURE.md)
3. Extend or optimize the system
4. Contribute improvements

---

## Contributing

When contributing to the physics system:

1. **Read the docs** - Especially implementation and architecture
2. **Follow patterns** - Match existing code style
3. **Document changes** - Update relevant docs
4. **Add examples** - Show how to use new features
5. **Test thoroughly** - Verify physics behavior

See: [MMD_PHYSICS_README.md](MMD_PHYSICS_README.md#contributing)

---

## External References

### MMD Resources
- [PMX Format Specification](https://gist.github.com/felixjones/f8a06bd48f9da9a4539f)
- [MMD Physics Overview](http://mikumikudance.wikia.com/wiki/Physics)

### Technical References
- [Verlet Integration](https://en.wikipedia.org/wiki/Verlet_integration)
- [Game Physics Engines](https://www.toptal.com/game/video-game-physics-part-i-an-introduction-to-rigid-body-dynamics)

### UE5 Documentation
- [Animation System](https://docs.unrealengine.com/5.0/en-US/animation-system-in-unreal-engine/)
- [Physics System](https://docs.unrealengine.com/5.0/en-US/physics-in-unreal-engine/)

---

## Version Information

**Current Version**: 1.0.0 (Initial Release)
**Last Updated**: 2025-10-14
**UE5 Compatibility**: 5.0+
**Status**: Core features complete, ready for testing

---

## Contact and Support

For issues, questions, or contributions:
- GitHub Repository: [treasureGrove/MMDUETools](https://github.com/treasureGrove/MMDUETools)
- Issues: Use GitHub Issues
- Discussions: Use GitHub Discussions

---

## License

This implementation is part of the MMDUETools project. See main project license for details.

---

**Happy MMD Physics Development! 🎉**

For any questions, start with the [README](MMD_PHYSICS_README.md) or check the [examples](MMD_PHYSICS_EXAMPLES.md).
