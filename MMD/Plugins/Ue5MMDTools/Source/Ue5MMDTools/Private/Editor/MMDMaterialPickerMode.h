#pragma once

#include "EdMode.h"
#include "EditorModeRegistry.h"

class AMMDActor;
class UPoseableMeshComponent;
class USkeletalMeshComponent;

/**
 * 材质拾取模式：点击骨骼网格上的任意部位，预览并选中该材质槽的 submesh。
 *
 * 实现（仅编辑器）：
 *   - 不替换原材质槽。用一个 PoseableMeshComponent 副本做 overlay，
 *     仅选中 section 用半透明 Opacity=0（base pass 不可见但 EditorSelection pass 渲染），
 *     其他 section 用 Masked Mask=0（全部裁剪）。
 *   - 复用 UE 原生选区描边（EMeshPass::EditorSelection + EditorPrimitivesStencil +
 *     PostProcessSelectionOutline.usf）：
 *       overlay proxy  → SetSelection_GameThread(false, true) → IsIndividuallySelected → 描边
 *       源 mesh proxy  → SetSelection_GameThread(false, false) → 抑制源 mesh 自身描边
 *     每 Tick 重刷（引擎的 selection reconciler 可能每帧复位）。
 *   - 进入拾取模式时把 GEngine->SelectionOutlineColor 临时改为亮品红，
 *     退出时恢复（每帧 FSceneView 重建读 GEngine，下一帧立即生效）。
 *   - 点击时对全部 section 做 CPU 蒙皮射线测试，收集命中集（按距离排序），
 *     同位置连点可在重叠 section 间循环切换（类 Unity 右键/连点循环）。
 *   - 自动导航到对应材质资产（Content Browser）。
 *
 * 用法：选中 AMMDActor 自动进入拾取，点击部位预览+选中，Esc/取消选中退出。
 */
class FMMDMaterialPickerMode : public FEdMode
{
public:
	static const FEditorModeID EM_MaterialPicker;

	FMMDMaterialPickerMode();
	virtual ~FMMDMaterialPickerMode();

	virtual bool HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click) override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual void Exit() override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	/** 自动进入/退出用的工厂：选中 AMMDActor 进入，否则退出 */
	class FFactory : public IEditorModeFactory
	{
	public:
		FFactory();
		virtual void OnSelectionChanged(FEditorModeTools& Tools, UObject* ItemUndergoingChange) const override;
		virtual FEditorModeInfo GetModeInfo() const override { return ModeInfo; }
		virtual TSharedRef<FEdMode> CreateMode() const override { return MakeShared<FMMDMaterialPickerMode>(); }
	private:
		FEditorModeInfo ModeInfo;
	};

private:
	void ApplyHighlight(USkeletalMeshComponent* SkelMesh, int32 MaterialSlot);
	void ClearOverlay();

	/** 每帧把 overlay 设为"individually selected"，源 mesh 设为未选中，触发原生蓝色描边 */
	void RefreshSelection();

	/** overlay 副本：仅选中 section 渲染并由 EditorSelection pass 画原生描边 */
	TObjectPtr<UPoseableMeshComponent> OverlayMesh;
	TObjectPtr<USkeletalMeshComponent> SourceMesh;
	int32 PickedMaterialSlot = -1;

	/** 进入拾取模式时备份原 SelectionOutlineColor，退出时恢复 */
	bool bColorOverridden = false;
	FLinearColor SavedOutlineColor;

	// 重叠切换状态：连续点击"当前最前面材质"相同 → 推进循环
	int32 LastPrimarySlot = -1;
	TArray<int32> LastHitSlots;
	int32 CycleIndex = 0;
};
