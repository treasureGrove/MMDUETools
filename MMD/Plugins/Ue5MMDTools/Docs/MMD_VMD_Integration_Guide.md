# UE5 MMD/PMX/VMD 集成指导文档

> 目的：为实现与 MMD 编辑器行为一致的 **模型导入 (PMX)**、**动作/表情播放 (VMD)**、**物理模拟** 及 **Sequencer 时间线控制** 提供架构与操作流程参考。

---
## 1. 总体目标
- PMX 初始化数据一次解析后持久化，避免重复读文件。
- VMD 作为时间驱动资产（骨骼轨迹 + Morph 权重），可像普通动画那样引用或在 Sequencer 中拖拽。
- 动画蓝图中按确定顺序：骨骼动画 → IK/约束 → Morph → 物理。
- 时间线跳转 / Scrub 时：物理状态重置，当前帧仍执行一次物理步进（即时可视化）。
- 保证多帧确定性（固定步长 + 重置策略），行为与 MMD 接近：不延续跨跳转的物理状态。

---
## 2. 资产层设计
| 资产 | 内容 | 说明 |
|------|------|------|
| `UMMDPmxPhysicsAsset` | Rigids + Joints 精简参数 | 物理初始化；不含顶点/材质等大数据 |
| `UMMDPmxMorphAsset` | Morph 通道（类型、骨骼增量、UE MorphTarget 映射） | 表情/骨骼/材质扩展基础 |
| `UVMDMotion` | 骨骼轨迹、Morph 轨迹、时长、帧率、Loop、(可选相机/灯光) | VMD 数据源（不烘焙成 `UAnimSequence`） |

> 资产仅保存“初始化与映射”数据，不保存运行期物理状态。

---
## 3. 构建 / 导入流程（Editor）
1. 解析 PMX：生成 SkeletalMesh（含 MorphTarget）。
2. 构建并保存 `UMMDPmxPhysicsAsset` / `UMMDPmxMorphAsset`。
3. 创建 AnimBlueprint，插入 `MMDPhysicsNode`、计划后续的 `VMDPoseNode`、`ApplyBoneMorphNode`。
4. 解析 VMD：生成 `UVMDMotion`（骨骼关键帧 + Morph 权重表）。
5. 在 AnimBlueprint 变量面板中设置：`CurrentVMD` / 播放参数（PlayRate、Loop）。
6. 可在 Sequencer 内添加 Track 驱动 ExternalTimeSec。

---
## 4. 播放控制层：`UVMDPlaybackController`
职责：
- 输入：`ExternalTimeSec`（绝对播放时间）。
- 输出：
  - `BonePoseBuffer`（局部姿势覆盖集或差分）。
  - `BoneMorphBuffer`（骨骼增量）。
  - `VertexMorphWeights`（MorphTarget 权重）。
  - 跳转判定标志 `bDiscontinuous`。
- 处理：Loop / Seek / Group morph 展开 / Impulse morph 队列（可选）。

跳转判定：
```
ExternalTimeSec < LastExternalTimeSec
ExternalTimeSec - LastExternalTimeSec > MaxContinuousDelta
DeltaSeconds <= 0
```

---
## 5. AnimBlueprint 流水线（Evaluate 顺序）
1. Base Pose（Idle / Locomotion / 空）。
2. IK / 约束修正。
3. `VMDPoseNode`：应用骨骼轨迹（覆盖 / Partial / Additive）。
4. `ApplyBoneMorphNode`：叠加 Bone Morph。
5. LocalToComponent。
6. `MMDPhysicsNode`：
   - 若 `bDiscontinuous`：
     - `ResetSimulator()` → `PreSyncKinematicFromBones()` → `StepSimulation(FixedTimeStep)` → `PostSyncBonesFromPhysics()`。
   - 否则：`PreSync` → `Step(FixedTimeStep × SubSteps)` → `PostSync`。
7. ComponentToLocal → 输出最终 Pose。
8. 顶点 Morph：在 `UAnimInstance::NativeUpdateAnimation`（GameThread）里调用 `SetMorphTarget()` 使用 `VertexMorphWeights`。

> 保证物理永远与最新骨骼 + 表情同步；跳转帧也有物理响应。

---
## 6. 物理模拟策略
- 固定步长：`FixedTimeStep = 1/60`；低帧率使用 `MaxSubSteps` 拆分。
- 跳转：不回放历史；重置后在目标帧执行单步模拟（ImmediateOneStep）。
- 可配置模式（Editor）：`FullResimulate`（从 0 累积，限时/限步）。

状态重置：
```
ResetSimulator(): 清除刚体速度/缓存 → bInitialized=false → 清空内部队列
```
首帧步进：避免“静态骨骼 + 动态附属物不匹配”的闪断。

---
## 7. Sequencer 集成
方式 A（简易）：Property Track 驱动 `ExternalTimeSec`，Event Track 触发重置标记。
方式 B（推荐）：自定义 `UMovieSceneVMDTrack` / `UMovieSceneVMDSection`：
- `Evaluate()` 中：`Controller.Seek(LocalTime)` → 设置 AnimInstance 时间 → 检测跳转 → 物理节点下一帧响应。
- Scrub：始终重置 + 单步实时显示物理。

相机/灯光轨迹：同一时间源下，独立 Track → 从 `UVMDMotion` 插值。

---
## 8. 骨骼覆盖与混合
- VMDPose 仅覆盖有轨迹骨骼集合（Sparse 覆盖）。
- 与 Locomotion 混合：使用 `LayeredBlendPerBone` 或 Additive。
- Additive：计算差分 Pose（VMDPose 与参考基准）。

---
## 9. 确定性与线程安全
| 方面 | 机制 |
|------|------|
| 时间 | 外部绝对时间 `ExternalTimeSec`，不依赖浮动累积误差 |
| 物理步进 | 固定步长 + 子步；跳转单步不累计历史 |
| Morph 应用 | 顶点 Morph 在 GameThread；骨骼 Morph 在 AnyThread 只读缓冲 |
| 数据访问 | AnimGraph 节点不访问 Actor；Controller 提供只读缓冲 |
| 状态持久化 | 仅初始化与映射持久化，运行态不写回资产 |

---
## 10. 关键字段/变量
```
ExternalTimeSec        // 当前播放时间（Sequencer 或内部驱动）
LastExternalTimeSec    // 上一帧时间，用于跳转判定
bDiscontinuous         // 跳帧/倒退标志
FixedTimeStep          // 物理固定步长
MaxSubSteps            // 低帧率时的补偿子步数
BonePoseBuffer         // 骨骼轨迹插值输出
BoneMorphBuffer        // 骨骼 morph 增量
VertexMorphWeights     // MorphTarget 权重表
PhysicsAsset / MorphAsset
CurrentVMD             // 当前 VMD 动作资产指针
PlayRate, bLoop        // 播放控制参数
```

---
## 11. 用户工作流（从零到播放）
1. 导入 `.pmx` → 自动生成 SkeletalMesh + PhysicsAsset + MorphAsset + AnimBP。
2. 导入 `.vmd` → 生成 `UVMDMotion`。
3. 在 AnimBP 设置 `CurrentVMD`；拖角色到场景即可自动播放。
4. 使用 Sequencer：添加角色 Track → 添加 VMD Track 或驱动 ExternalTimeSec → 播放 / Scrub。
5. 需要切换动作：替换 AnimBP 中的 `CurrentVMD` 或在 Sequencer 切换 Section。
6. 混合移动：在 AnimGraph 添加 Locomotion Pose 与 VMDPose 的层级混合。

---
## 12. 扩展方向
- 多 VMD 混合（多个 `UVMDMotion` + 权重）
- Retarget（骨骼名称映射）
- Impulse morph → 物理节点预队列消费
- 全局物理世界（跨角色交互）
- Editor 调试：`FullResimulateOnScrub`、物理统计面板

---
## 13. 避免的错误模式
- AnyThread 中直接访问 Actor/UObject。
- 跳转后沿用旧刚体速度（未重置）。
- 每次打开重新解析 PMX（应使用持久化 DataAsset）。
- 将 VMD 强制转换为大体积 `UAnimSequence`（失去动态编辑优势）。
- 顶点 Morph 在多线程 Pose 阶段写入。

---
## 14. 行为一致性对齐（与 MMD）
| 行为 | MMD | 本方案 |
|------|-----|--------|
| 首帧物理 | 重置后立即响应 | Reset + 单步 |
| 时间跳转 | 重置状态 | Reset + 单步 |
| 连续播放 | 固定步累积 | 固定步 + SubSteps |
| 编辑 Scrub | 姿势 + 单帧物理 | 姿势 + 单步 |
| 表情/Morph | 权重插值叠加 | Controller 插值 + 分层应用 |

---
## 15. 快速集成清单
- [ ] PMX 解析 → PhysicsAsset / MorphAsset
- [ ] VMD 解析 → UVMDMotion
- [ ] UVMDPlaybackController 实现（Seek/插值/缓冲）
- [ ] AnimBlueprint：VMDPoseNode / ApplyBoneMorphNode / MMDPhysicsNode
- [ ] 顶点 Morph 在 AnimInstance Update 中应用
- [ ] 跳转判定 + Reset + 单步逻辑
- [ ] Sequencer Track（驱动 ExternalTimeSec）
- [ ] 文档与调试面板

---
## 16. 示例伪代码（核心逻辑片段）
```cpp
if (bDiscontinuous) {
    Physics.ResetSimulator();
    Physics.PreSyncKinematicFromBones(CurrentPose);
    Physics.StepSimulation(FixedTimeStep); // 单步
    Physics.PostSyncBonesFromPhysics(OutBoneTransforms);
} else {
    Physics.PreSyncKinematicFromBones(CurrentPose);
    Physics.StepSimulation(FixedTimeStep * SubSteps, SubSteps);
    Physics.PostSyncBonesFromPhysics(OutBoneTransforms);
}
```

---
## 17. 结论
通过资产分层（Physics/Morph/VMD）、流式插值控制器、确定性物理节点与 Sequencer 时间驱动，可在 UE5 中实现与 MMD 编辑器近似的工作流：高效、可跳转、物理即时响应、配置持久化、运行稳定。该设计兼顾扩展性（多 VMD、Retarget、全局物理）与性能（避免重复解析大文件）。

> 后续可在此基础上添加调试 UI、性能统计、跨角色物理交互等高级功能。
