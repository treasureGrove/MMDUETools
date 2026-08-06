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
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionTwoSidedSign.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "RawIndexBuffer.h"
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
}

bool FMMDMaterialPickerMode::IsCompatibleWith(FEditorModeID OtherModeID) const
{
	return OtherModeID != EM_MaterialPicker;
}

// ---------------- 材质 ----------------

// 隐形材质：完全透明，用于隐藏 overlay 上非选中的 section
static UMaterial* GetInvisibleMaterial()
{
	static UMaterial* Mat = nullptr;
	if (!Mat)
	{
		Mat = NewObject<UMaterial>(GetTransientPackage(), TEXT("MMD_OverlayInvisible"));
		Mat->SetFlags(RF_Transient);
		Mat->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
		Mat->BlendMode = BLEND_Translucent;

		UMaterialExpressionConstant* Op = NewObject<UMaterialExpressionConstant>(Mat);
		Op->R = 0.0f;
		Mat->GetExpressionCollection().Expressions.Add(Op);
		Mat->GetEditorOnlyData()->Opacity.Expression = Op;

		Mat->PostEditChange();
	}
	return Mat;
}

// 法线外拓描边（inverted hull）：顶点沿法线外扩，Masked + TwoSided，
// 用 TwoSidedSign 遮罩只保留背面 → 得到沿 silhouette 的清晰洋红描边
static UMaterial* GetOverlayOutlineMaterial()
{
	static UMaterial* Mat = nullptr;
	if (!Mat)
	{
		Mat = NewObject<UMaterial>(GetTransientPackage(), TEXT("MMD_OverlayOutline"));
		Mat->SetFlags(RF_Transient);
		Mat->SetShadingModel(EMaterialShadingModel::MSM_Unlit);
		Mat->BlendMode = BLEND_Masked;
		Mat->TwoSided = true;
		Mat->bDisableDepthTest = true; // 始终可见，类 UE 选区描边
		Mat->bUsedWithSkeletalMesh = true;

		auto& Col = Mat->GetExpressionCollection();

		// 洋红
		UMaterialExpressionConstant3Vector* Color = NewObject<UMaterialExpressionConstant3Vector>(Mat);
		Color->Constant = FLinearColor(1.0f, 0.0f, 1.0f, 1.0f);
		Col.Expressions.Add(Color);

		// WPO = VertexNormalWS * 描边宽度
		UMaterialExpressionVertexNormalWS* Nrm = NewObject<UMaterialExpressionVertexNormalWS>(Mat);
		Col.Expressions.Add(Nrm);
		UMaterialExpressionConstant* Width = NewObject<UMaterialExpressionConstant>(Mat);
		Width->R = 0.5f;
		Col.Expressions.Add(Width);
		UMaterialExpressionMultiply* WpoMul = NewObject<UMaterialExpressionMultiply>(Mat);
		WpoMul->A.Expression = Nrm;
		WpoMul->B.Expression = Width;
		Col.Expressions.Add(WpoMul);

		// OpacityMask = 0.5 - 0.5*TwoSidedSign：正面(1)→0(裁剪)，背面(-1)→1(保留)
		UMaterialExpressionTwoSidedSign* Sign = NewObject<UMaterialExpressionTwoSidedSign>(Mat);
		Col.Expressions.Add(Sign);
		UMaterialExpressionConstant* NegHalf = NewObject<UMaterialExpressionConstant>(Mat);
		NegHalf->R = -0.5f;
		Col.Expressions.Add(NegHalf);
		UMaterialExpressionMultiply* SignMul = NewObject<UMaterialExpressionMultiply>(Mat);
		SignMul->A.Expression = Sign;
		SignMul->B.Expression = NegHalf;
		Col.Expressions.Add(SignMul);
		UMaterialExpressionConstant* Half = NewObject<UMaterialExpressionConstant>(Mat);
		Half->R = 0.5f;
		Col.Expressions.Add(Half);
		UMaterialExpressionAdd* SignAdd = NewObject<UMaterialExpressionAdd>(Mat);
		SignAdd->A.Expression = SignMul;
		SignAdd->B.Expression = Half;
		Col.Expressions.Add(SignAdd);

		Mat->GetEditorOnlyData()->EmissiveColor.Expression = Color;
		Mat->GetEditorOnlyData()->WorldPositionOffset.Expression = WpoMul;
		Mat->GetEditorOnlyData()->OpacityMask.Expression = SignAdd;

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
		OverlayMesh->SetHiddenInGame(true); // 只在编辑器可见
		if (USceneComponent* Root = ActorOwner->GetRootComponent())
		{
			OverlayMesh->SetupAttachment(Root);
		}
		OverlayMesh->RegisterComponent();
	}

	if (USkeletalMesh* Src = SkelMesh->GetSkeletalMeshAsset())
	{
		OverlayMesh->SetSkinnedAssetAndUpdate(Src, true);

		// 除选中槽位外全部隐形，选中槽位叠加外拓描边
		const int32 Num = SkelMesh->GetNumMaterials();
		for (int32 i = 0; i < Num; i++)
		{
			OverlayMesh->SetMaterial(i, GetInvisibleMaterial());
		}
		OverlayMesh->SetMaterial(MaterialSlot, GetOverlayOutlineMaterial());
		OverlayMesh->SetVisibility(true);
	}

	PickedMaterialSlot = MaterialSlot;
}

void FMMDMaterialPickerMode::ClearOverlay()
{
	if (OverlayMesh)
	{
		OverlayMesh->DestroyComponent();
		OverlayMesh = nullptr;
	}
	PickedMaterialSlot = -1;
	LastPrimarySlot = -1;
	LastHitSlots.Reset();
	CycleIndex = 0;
}

void FMMDMaterialPickerMode::Tick(FEditorViewportClient* ViewportClient, float DeltaTime)
{
	// 每帧同步 overlay 的姿势和变换，跟随源网格动画
	if (OverlayMesh && SourceMesh)
	{
		OverlayMesh->CopyPoseFromSkeletalComponent(SourceMesh);
		OverlayMesh->SetWorldTransform(SourceMesh->GetComponentTransform());
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
	if (!SkelMesh || !SkelMesh->GetOwner() || !SkelMesh->GetOwner()->IsA<AMMDActor>())
	{
		ClearOverlay(); // 没点中角色 → 还原
		return false;
	}

	// ---- 主拾取：渲染 hit proxy 的材质槽（渲染级精确，眼睛等半透明也正常）----
	const int32 PrimarySlot = ActorProxy->MaterialIndex;

	// ---- 循环列表：CPU 蒙皮射线收集命中 section（布料区域近似）----
	// 主槽位永远排第一（最前面）
	TArray<int32> HitSlots;
	HitSlots.Add(PrimarySlot);

	USkeletalMesh* Mesh = SkelMesh->GetSkeletalMeshAsset();
	if (Mesh)
	{
		const FVector Start = Click.GetOrigin();
		const FVector End = Start + Click.GetDirection() * 100000.0f;
		const int32 LOD = FMath::Max(SkelMesh->GetPredictedLODLevel(), 0);
		FSkeletalMeshRenderData* RD = Mesh->GetResourceForRendering();
		if (RD && RD->LODRenderData.IsValidIndex(LOD))
		{
			FSkeletalMeshLODRenderData& LODData = RD->LODRenderData[LOD];
			FRawStaticIndexBuffer16or32Interface* IndexBuffer = LODData.MultiSizeIndexContainer.GetIndexBuffer();
			FSkinWeightVertexBuffer& SkinBuffer = LODData.SkinWeightVertexBuffer;
			const FTransform CompToWorld = SkelMesh->GetComponentTransform();

			TMap<uint32, FVector> PosCache;
			TArray<TTuple<int32, float>> HitDists; // <section, 最小距离平方>

			for (int32 si = 0; si < LODData.RenderSections.Num(); si++)
			{
				const FSkelMeshRenderSection& Sec = LODData.RenderSections[si];
				float MinDistSq = FLT_MAX;
				for (uint32 t = 0; t < Sec.NumTriangles; t++)
				{
					const uint32 IdxBase = Sec.BaseIndex + t * 3;
					FVector P[3];
					for (int32 v = 0; v < 3; v++)
					{
						const uint32 GVI = IndexBuffer->Get(IdxBase + v) + Sec.BaseVertexIndex;
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

	// 重叠切换（类 Unity）：连续点"当前最前面材质"相同 → 推进循环；否则取最前面
	if (HitSlots.Num() > 0 && HitSlots[0] == LastPrimarySlot)
	{
		CycleIndex = (CycleIndex + 1) % HitSlots.Num();
	}
	else
	{
		CycleIndex = 0;
	}
	LastHitSlots = HitSlots;
	LastPrimarySlot = HitSlots.Num() > 0 ? HitSlots[0] : -1;

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
