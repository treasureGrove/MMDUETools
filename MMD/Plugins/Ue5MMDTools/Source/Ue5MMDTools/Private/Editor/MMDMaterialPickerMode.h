#pragma once

#include "EdMode.h"
#include "EditorModeRegistry.h"

class AMMDActor;
class UPoseableMeshComponent;
class USkeletalMeshComponent;

/**
 * 材质拾取模式：点击骨骼网格上的任意部位，预览并选中该材质槽的 submesh。
 *
 * 实现：
 *   - 不替换原材质槽。用一个编辑器 PoseableMeshComponent 副本做 overlay，
 *     只让选中的 section 显示半透明品红 + Fresnel 描边，其余 section 隐形。
 *     原材质完整渲染 → 可同时预览材质 + 看到高亮。
 *   - 点击时对全部 section 做射线三角形测试，收集命中集（按距离排序），
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

	/** overlay 副本：原材质保持渲染，只有选中 section 叠加外拓描边 */
	TObjectPtr<UPoseableMeshComponent> OverlayMesh;
	TObjectPtr<USkeletalMeshComponent> SourceMesh;
	int32 PickedMaterialSlot = -1;

	// 重叠切换状态：连续点击"当前最前面材质"相同 → 推进循环
	int32 LastPrimarySlot = -1;
	TArray<int32> LastHitSlots;
	int32 CycleIndex = 0;
};
