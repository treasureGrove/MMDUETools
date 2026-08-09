#include "MMDMaterialPickerMode.h"

#include "Actors/AMMDActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "EditorViewportClient.h"
#include "EngineUtils.h"
#include "HitProxies.h"
#include "EditorModeManager.h"
#include "Engine/Selection.h"
#include "Engine/World.h"
#include "MaterialDomain.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "RawIndexBuffer.h"
#include "PrimitiveSceneProxy.h"
#include "UObject/Package.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "AssetRegistry/AssetData.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

const FEditorModeID FMMDMaterialPickerMode::EM_MaterialPicker = TEXT("EM_MMDMaterialPicker");

FMMDMaterialPickerMode::FMMDMaterialPickerMode()
{
}

FMMDMaterialPickerMode::~FMMDMaterialPickerMode()
{
	// 兜底：如果 Exit 没被调用（编辑器关闭等极端情况），尽力恢复描边色
	if (GEngine && bColorOverridden)
	{
		GEngine->SetSelectionOutlineColor(SavedOutlineColor);
		bColorOverridden = false;
	}
}

bool FMMDMaterialPickerMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	return OtherModeID != EM_MaterialPicker;
}

// ---------------- 材质 ----------------

// 全裁剪材质（非选中 section）：Masked Mask=0，base pass 和 EditorSelection pass 都被裁剪
static UMaterial* GetClipMaterial()
{
	static UMaterial* Mat = nullptr;
	if (!Mat)
	{
		Mat = NewObject<UMaterial>(GetTransientPackage(), TEXT("MMD_OverlayClip"));
		Mat->SetFlags(RF_Transient);
		Mat->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
		Mat->BlendMode = BLEND_Masked;
		Mat->bUsedWithSkeletalMesh = true;

		UMaterialExpressionConstant* Mask = NewObject<UMaterialExpressionConstant>(Mat);
		Mask->R = 0.0f;
		Mat->GetExpressionCollection().Expressions.Add(Mask);
		Mat->GetEditorOnlyData()->OpacityMask.Expression = Mask;

		Mat->PostEditChange();
	}
	return Mat;
}

// 选中 section 的材质：Translucent + Opacity=0 → base pass 不可见（不挡原网格），
// 但 EditorSelection pass 不会对半透明做 mask 裁剪，会完整渲染 → 引擎原生描边出蓝色轮廓
static UMaterial* GetInvisibleTranslucentMaterial()
{
	static UMaterial* Mat = nullptr;
	if (!Mat)
	{
		Mat = NewObject<UMaterial>(GetTransientPackage(), TEXT("MMD_OverlayInvisible"));
		Mat->SetFlags(RF_Transient);
		Mat->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
		Mat->BlendMode = BLEND_Translucent;
		Mat->bUsedWithSkeletalMesh = true;

		// Opacity = 0（base pass 不输出颜色，但 EditorSelection pass 仍处理几何）
		UMaterialExpressionConstant* Op = NewObject<UMaterialExpressionConstant>(Mat);
		Op->R = 0.0f;
		Mat->GetExpressionCollection().Expressions.Add(Op);
		Mat->GetEditorOnlyData()->Opacity.Expression = Op;

		Mat->PostEditChange();
	}
	return Mat;
}

// ---------------- overlay ----------------

void FMMDMaterialPickerMode::ApplyHighlight(USkeletalMeshComponent* SkelMesh, int32 MaterialSlot)
{
	if (!OverlayMesh)
	{
		AActor* ActorOwner = SkelMesh->GetOwner();
		OverlayMesh = NewObject<UPoseableMeshComponent>(ActorOwner, NAME_None, RF_Transient);
		OverlayMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OverlayMesh->SetCastShadow(false);
		OverlayMesh->SetHiddenInGame(true);     // 编辑器可见，运行时不可见
		OverlayMesh->bSelectable = false;       // 不可点选，避免挡住后续点击
		OverlayMesh->SetVisibility(true);
		OverlayMesh->bRenderCustomDepth = false; // 不污染 Custom Stencil，原生描边走 EditorPrimitivesStencil
		if (USceneComponent* Root = ActorOwner->GetRootComponent())
		{
			OverlayMesh->SetupAttachment(Root);
		}
		OverlayMesh->RegisterComponent();

		// 首次创建 overlay 时把描边色改成亮品红（每帧 FSceneView 从 GEngine 读，下帧生效）
		if (GEngine && !bColorOverridden)
		{
			SavedOutlineColor = GEngine->GetSelectionOutlineColor();
			// 亮品红色：饱和度高、辨识度强，区别于编辑器默认黄
			GEngine->SetSelectionOutlineColor(FLinearColor(1.0f, 0.12f, 1.0f, 1.0f));
			bColorOverridden = true;
		}
	}

	if (USkeletalMesh* Src = SkelMesh->GetSkeletalMeshAsset())
	{
		OverlayMesh->SetSkinnedAssetAndUpdate(Src, true);

		// 非选中 section 全裁剪（不进 EditorSelection pass），选中 section 用透明材质渲染几何
		const int32 Num = SkelMesh->GetNumMaterials();
		for (int32 i = 0; i < Num; i++)
		{
			OverlayMesh->SetMaterial(i, GetClipMaterial());
		}
		OverlayMesh->SetMaterial(MaterialSlot, GetInvisibleTranslucentMaterial());
		OverlayMesh->SetVisibility(true);
	}

	PickedMaterialSlot = MaterialSlot;
	SourceMesh = SkelMesh; // 记住源网格，每帧同步姿势 + 抑制源网格描边
	RefreshSelection();    // 立刻刷一次 selection（Tick 也会每帧重刷）
}

void FMMDMaterialPickerMode::RefreshSelection()
{
	if (!OverlayMesh)
	{
		return;
	}

	// overlay 标记为 individually selected → 引擎分配描边 stencil color 0（蓝），画原生描边
	if (FPrimitiveSceneProxy* OverlayProxy = OverlayMesh->SceneProxy)
	{
		OverlayProxy->SetSelection_GameThread(false, true);
	}

	// 源 mesh 抑制描边（仅渲染 proxy 状态，不影响 actor 逻辑选中）
	if (SourceMesh)
	{
		if (FPrimitiveSceneProxy* SrcProxy = SourceMesh->SceneProxy)
		{
			SrcProxy->SetSelection_GameThread(false, false);
		}
	}
}

void FMMDMaterialPickerMode::ClearOverlay()
{
	// 退出前把源 mesh 的 selection 恢复成 actor 实际选中状态（actor 仍被选中 → 描边回归原生）
	if (SourceMesh && SourceMesh->SceneProxy)
	{
		SourceMesh->PushSelectionToProxy();
	}

	if (OverlayMesh)
	{
		OverlayMesh->DestroyComponent();
		OverlayMesh = nullptr;
	}

	// 恢复原描边色
	if (GEngine && bColorOverridden)
	{
		GEngine->SetSelectionOutlineColor(SavedOutlineColor);
		bColorOverridden = false;
	}

	PickedMaterialSlot = -1;
	LastPrimarySlot = -1;
	LastHitSlots.Reset();
	CycleIndex = 0;
	SourceMesh = nullptr;
}

void FMMDMaterialPickerMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	if (OverlayMesh && SourceMesh)
	{
		// 每帧同步 overlay 姿势和变换，跟随源网格动画
		OverlayMesh->CopyPoseFromSkeletalComponent(SourceMesh);
		OverlayMesh->SetWorldTransform(SourceMesh->GetComponentTransform());

		// 引擎 selection reconciler 可能每帧把代理状态复位，所以每帧重刷
		RefreshSelection();
	}
	FEdMode::Tick(ViewportClient, DeltaTime);
}

void FMMDMaterialPickerMode::Exit()
{
	ClearOverlay();
	FEdMode::Exit();
}

void FMMDMaterialPickerMode::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(OverlayMesh);
	Collector.AddReferencedObject(SourceMesh);
	FEdMode::AddReferencedObjects(Collector);
}

bool FMMDMaterialPickerMode::HandleClick(FEditorViewportClient* InViewportClient, HHitProxy* HitProxy, const FViewportClick& Click)
{
	if (!InViewportClient || !InViewportClient->GetWorld())
	{
		return true;
	}

	// 确认点到的是 MMD 角色的骨骼网格
	HActor* ActorProxy = HitProxyCast<HActor>(HitProxy);
	USkeletalMeshComponent* SkelMesh = ActorProxy
		? const_cast<USkeletalMeshComponent*>(Cast<USkeletalMeshComponent>(ActorProxy->PrimComponent))
		: nullptr;
	// overlay（PoseableMeshComponent）若被点到（bSelectable=false 之外的兜底）→ 改回源网格，避免误清高亮
	if (!SkelMesh && ActorProxy && OverlayMesh && ActorProxy->PrimComponent == OverlayMesh)
	{
		SkelMesh = SourceMesh;
	}
	if (!SkelMesh || !SkelMesh->GetOwner() || !SkelMesh->GetOwner()->IsA<AMMDActor>())
	{
		ClearOverlay(); // 没点中角色 → 还原
		return false;
	}

	// ---- 权威拾取：CPU 蒙皮射线对全部 section 求命中，按距离排序 ----
	// 渲染 hit proxy 对半透明 section（如眼睛）不保证产生命中，
	// CPU 射线遍历全部三角形更可靠，可见的眼睛必然命中。
	TArray<int32> HitSlots;

	USkeletalMesh* Mesh = SkelMesh->GetSkeletalMeshAsset();
	if (Mesh)
	{
		const FVector Start = Click.GetOrigin();
		const FVector End = Start + Click.GetDirection() * 100000.0f;
		// 强制 LOD=0：最高精度，避免低 LOD 把眼睛等小 section 合并掉导致选不中
		FSkeletalMeshRenderData* RD = Mesh->GetResourceForRendering();
		if (RD && RD->LODRenderData.Num() > 0)
		{
			FSkeletalMeshLODRenderData& LODData = RD->LODRenderData[0];
			FRawStaticIndexBuffer16or32Interface* IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
			FSkinWeightVertexBuffer& SkinBuffer = LODData.SkinWeightVertexBuffer;
			const FTransform CompToWorld = SkelMesh->GetComponentTransform();
			TMap<uint32, FVector> PosCache;
			TArray<TTuple<int32, float>> HitDists; // <section index, 最近命中距离平方>
			const FVector StartToEnd = End - Start;

			for (int32 si = 0; si < LODData.RenderSections.Num(); si++)
			{
				const FSkelMeshRenderSection& Sec = LODData.RenderSections[si];
				if (Sec.NumTriangles == 0)
				{
					continue;
				}

				// ---- 第一遍：遍历顶点构建该 section 的世界 AABB，并做射线-AABB 预筛选 ----
				// 没命中 AABB 直接跳过，省掉海量三角形相交测试；命中 AABB 再做精确三角形测试
				FBox SectionBox(EForceInit::ForceInit);
				bool bFirst = true;
				for (uint32 vIdx = 0; vIdx < Sec.NumVertices; vIdx++)
				{
					const uint32 GVI = Sec.BaseVertexIndex + vIdx;
					FVector W;
					if (const FVector* C = PosCache.Find(GVI))
					{
						W = *C;
					}
					else
					{
						const FVector3f CompPos = USkinnedMeshComponent::GetSkinnedVertexPosition(SkelMesh, (int32)GVI, LODData, SkinBuffer);
						W = CompToWorld.TransformPosition(FVector(CompPos));
						PosCache.Add(GVI, W);
					}
					if (bFirst) { SectionBox = FBox(W, W); bFirst = false; }
					else { SectionBox += W; }
				}
				if (bFirst)
				{
					continue;
				}
				if (!FMath::LineBoxIntersection(SectionBox, Start, End, StartToEnd))
				{
					continue;
				}

				// ---- 第二遍：精确三角形相交测试 ----
				// 注意：骨骼网格 IndexBuffer 的值已是 LOD 全局顶点索引（在 [BaseVertexIndex, BaseVertexIndex+NumVertices) 范围），
				// 不需要再加 BaseVertexIndex（旧代码这里加错了导致拾取位置错误 + 越界崩溃）
				float MinDistSq = FLT_MAX;
				for (uint32 t = 0; t < Sec.NumTriangles; t++)
				{
					const uint32 IdxBase = Sec.BaseIndex + t * 3;
					FVector P[3];
					for (int32 v = 0; v < 3; v++)
					{
						const uint32 GVI = IndexBuffer->Get(IdxBase + v);
						// 防御性访问：理论必在缓存里，万一不在就用正确 GVI 现算并缓存
						if (const FVector* C = PosCache.Find(GVI))
						{
							P[v] = *C;
						}
						else
						{
							const FVector3f CompPos = USkinnedMeshComponent::GetSkinnedVertexPosition(SkelMesh, (int32)GVI, LODData, SkinBuffer);
							const FVector W = CompToWorld.TransformPosition(FVector(CompPos));
							PosCache.Add(GVI, W);
							P[v] = W;
						}
					}
					FVector HitPt, TriangleNormal;
					if (FMath::SegmentTriangleIntersection(Start, End, P[0], P[1], P[2], HitPt, TriangleNormal))
					{
						MinDistSq = FMath::Min(MinDistSq, FVector::DistSquared(HitPt, Start));
					}
				}
				if (MinDistSq < FLT_MAX)
				{
					HitDists.Add(MakeTuple(si, MinDistSq));
				}
			}

			HitDists.Sort([](const TTuple<int32, float>& A, const TTuple<int32, float>& B)
			{
				return A.Get<1>() < B.Get<1>();
			});
			for (const auto& HD : HitDists)
			{
				const int32 Slot = LODData.RenderSections[HD.Get<0>()].MaterialIndex;
				if (!HitSlots.Contains(Slot))
				{
					HitSlots.Add(Slot);
				}
			}
		}
	}

	// 兜底：CPU 射线一条都没命中时，退回渲染 hit proxy 的材质槽
	if (HitSlots.Num() == 0 && ActorProxy)
	{
		HitSlots.Add(ActorProxy->MaterialIndex);
	}

	if (HitSlots.Num() == 0)
	{
		ClearOverlay();
		return false;
	}

	// 重叠切换（类 Unity）：基于"上次选中的 slot 是否在当前命中集里"判定是否同一区域连点。
	// 比基于 HitSlots[0] 的旧判定鲁棒：鼠标轻微抖动导致 HitSlots[0] 变化时不会错误重置。
	if (PickedMaterialSlot >= 0 && HitSlots.Contains(PickedMaterialSlot))
	{
		// 上次选中的 slot 在当前 HitSlots 里 → 推进到下一个（按距离排序的下一个）
		int32 LastIdx = HitSlots.IndexOfByKey(PickedMaterialSlot);
		CycleIndex = (LastIdx + 1) % HitSlots.Num();
	}
	else
	{
		// 新区域或第一次 → 取最近的
		CycleIndex = 0;
	}
	LastHitSlots = HitSlots;
	LastPrimarySlot = HitSlots[0];

	const int32 MaterialSlot = HitSlots[CycleIndex];

	// 1. 原材质（替换前取）
	UMaterialInterface* RealMat = SkelMesh->GetMaterial(MaterialSlot);
	const FString MatName = RealMat ? RealMat->GetName() : TEXT("None");

	// 2. 先 Content Browser 自动定位材质资产
	if (RealMat)
	{
		FContentBrowserModule& CBModule = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		TArray<FAssetData> Assets;
		Assets.Add(FAssetData(RealMat));
		CBModule.Get().SyncBrowserToAssets(Assets, true);
	}

	// 3. overlay 描边高亮（不替换原材质）
	ApplyHighlight(SkelMesh, MaterialSlot);

	// 4. 右上角提示（重叠时提示可再点切换）
	UE_LOG(LogTemp, Warning, TEXT("[MMDPick] Actor=%s 材质槽=%d 材质=%s (重叠=%d)"),
		*SkelMesh->GetOwner()->GetName(), MaterialSlot, *MatName, HitSlots.Num());

	FString NotifText = FString::Printf(TEXT("材质槽 %d\n%s"), MaterialSlot, *MatName);
	if (HitSlots.Num() > 1)
	{
		NotifText += FString::Printf(TEXT("\n（再点同位置切换 %d 个重叠）"), HitSlots.Num());
	}
	FNotificationInfo NotifInfo(FText::FromString(NotifText));
	NotifInfo.bUseLargeFont = true;
	NotifInfo.FadeInDuration = 0.1f;
	NotifInfo.ExpireDuration = 3.0f;
	FSlateNotificationManager::Get().AddNotification(NotifInfo);

	return true;
}

// ---------------- FFactory ----------------

FMMDMaterialPickerMode::FFactory::FFactory()
	: ModeInfo(FMMDMaterialPickerMode::EM_MaterialPicker, FText::FromString(TEXT("MMD Material Picker")))
{
}

void FMMDMaterialPickerMode::FFactory::OnSelectionChanged(FEditorModeTools& ModeTools, UObject* ItemUndergoingChange) const
{
	// 选中了 AMMDActor 就进入拾取模式，否则退出
	AActor* SelectedActor = nullptr;
	if (USelection* Sel = ModeTools.GetSelectedActors())
	{
		SelectedActor = Sel->GetTop<AActor>();
	}

	const bool bShouldEnter = Cast<AMMDActor>(SelectedActor) != nullptr;
	if (bShouldEnter && !ModeTools.IsModeActive(FMMDMaterialPickerMode::EM_MaterialPicker))
	{
		ModeTools.ActivateMode(FMMDMaterialPickerMode::EM_MaterialPicker);
	}
	else if (!bShouldEnter && ModeTools.IsModeActive(FMMDMaterialPickerMode::EM_MaterialPicker))
	{
		ModeTools.DeactivateMode(FMMDMaterialPickerMode::EM_MaterialPicker);
	}
}
