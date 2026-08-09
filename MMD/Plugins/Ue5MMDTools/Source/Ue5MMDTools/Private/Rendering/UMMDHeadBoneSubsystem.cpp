#include "Rendering/UMMDHeadBoneSubsystem.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CoreMisc.h"

// 定义静态头骨备选名数组（constexpr 数组成员要在 cpp 里定义一次 ODR）
constexpr const TCHAR* UMMDHeadBoneSubsystem::HeadBoneCandidates[];

void UMMDHeadBoneSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogTemp, Log, TEXT("[MMDHeadBone] Subsystem initialized. Auto-fills MMDHeadForward/MMDHeadRight for skel meshes that need it."));
}

void UMMDHeadBoneSubsystem::Deinitialize()
{
	BoneIndexCache.Reset();
	WarnedComponents.Reset();
	Super::Deinitialize();
}

void UMMDHeadBoneSubsystem::Tick(float DeltaTime)
{
	// 注意：不要调 Super::Tick —— UTickableWorldSubsystem 的 Tick 来自 FTickableGameObject
	// 是纯虚函数，调了会 pure virtual function call 直接 crash。

	// 调试：确认 Tick 有没有被调用（编辑器非 PIE 视口里 TickableWorldSubsystem 不一定 tick）
	static int32 TickCount = 0;
	if ((TickCount++ % 120) == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[MMDHeadBone] Tick called, World=%s"), GetWorld() ? *GetWorld()->GetName() : TEXT("NULL"));
	}

	if (!GetWorld())
	{
		return;
	}

	// 清理已 GC 的弱引用，避免 TMap 无限增长
	for (auto It = BoneIndexCache.CreateIterator(); It; ++It)
	{
		if (!It.Key().Get()) It.RemoveCurrent();
	}
	for (auto It = WarnedComponents.CreateIterator(); It; ++It)
	{
		if (!It->Get()) It.RemoveCurrent();
	}

	ProcessedCount = 0;

	// 遍历世界里所有 SkeletalMeshComponent
	for (TActorIterator<AActor> ActorIt(GetWorld()); ActorIt; ++ActorIt)
	{
		AActor* Actor = *ActorIt;
		if (!Actor) continue;

		TArray<USkeletalMeshComponent*> Comps;
		Actor->GetComponents<USkeletalMeshComponent>(Comps);

		for (USkeletalMeshComponent* SkelComp : Comps)
		{
			if (!SkelComp || !SkelComp->GetSkeletalMeshAsset())
			{
				continue;
			}

			// 找头骨索引（缓存）
			int32* CachedIdx = BoneIndexCache.Find(SkelComp);
			int32 HeadIdx = CachedIdx ? *CachedIdx : INDEX_NONE;

			if (HeadIdx == INDEX_NONE && !CachedIdx)
			{
				// 第一次找：尝试所有备选名
				for (const TCHAR* BoneName : HeadBoneCandidates)
				{
					HeadIdx = SkelComp->GetBoneIndex(FName(BoneName));
					if (HeadIdx != INDEX_NONE) break;
				}

				// 缓存结果（包括 INDEX_NONE，避免每帧重复查找）
				BoneIndexCache.Add(SkelComp, HeadIdx);

				if (HeadIdx == INDEX_NONE && !WarnedComponents.Contains(SkelComp))
				{
					// 列出所有骨骼名，方便确认该用哪个头骨名
					FString BoneList;
					if (USkeletalMesh* SM = SkelComp->GetSkeletalMeshAsset())
					{
						const FReferenceSkeleton& RefSkel = SM->GetRefSkeleton();
						for (int32 i = 0; i < RefSkel.GetNum(); ++i)
						{
							BoneList += RefSkel.GetBoneName(i).ToString() + TEXT(" ");
						}
					}
					UE_LOG(LogTemp, Warning,
						TEXT("[MMDHeadBone] %s 上找不到头骨（试过 頭/Head/head/C_HEAD/head_top/HeadTop）。骨骼列表: %s"),
						*SkelComp->GetName(), *BoneList);
					WarnedComponents.Add(SkelComp);
				}
			}

			if (HeadIdx == INDEX_NONE)
			{
				continue;
			}

			// 拿头骨世界变换 → forward / right 向量
			const FTransform BoneTM = SkelComp->GetBoneTransform(HeadIdx);
			const FQuat BoneQuat = BoneTM.GetRotation();

			// 头朝向轴：UE 标准骨骼是 X=forward，但 MMD 模型导入后头骨通常朝前的是 Y 轴，
			// 所以用 GetAxisY() 当头 forward。up 取骨骼 Z 轴，right = cross(forward, up)
			// （UE 左手系：forward×up = +X = 右侧；cross(up, forward) 会是左，别搞反）。
			// 如果换标准 X 轴向模型，改回 BoneQuat.GetForwardVector() / GetRightVector() 即可。
			const FVector HeadForwardWS = BoneQuat.GetAxisY().GetSafeNormal();
			const FVector HeadRightWS = FVector::CrossProduct(BoneQuat.GetAxisY(), BoneQuat.GetAxisZ()).GetSafeNormal();

			// 写入材质向量参数（找不到参数的材质会自动忽略，零副作用）。
			// 优化假设：SetVectorParameterValueOnMaterials 内部很快，且大多数项目里只有
			// 真正用到脸部 SDF 阴影的材质才会创建这个参数，所以不会污染其他材质。
			SkelComp->SetVectorParameterValueOnMaterials(ParamHeadForward, HeadForwardWS);
			SkelComp->SetVectorParameterValueOnMaterials(ParamHeadRight, HeadRightWS);

			// 调试：每 120 帧打一次头骨朝向，确认轴向写入是否正确
			static int32 FrameCounter = 0;
			if ((++FrameCounter & 0x7F) == 1)
			{
				UE_LOG(LogTemp, Warning, TEXT("[MMDHeadBone] %s head forward=(%.2f,%.2f,%.2f) right=(%.2f,%.2f,%.2f)"),
					*SkelComp->GetName(),
					HeadForwardWS.X, HeadForwardWS.Y, HeadForwardWS.Z,
					HeadRightWS.X, HeadRightWS.Y, HeadRightWS.Z);
			}

			++ProcessedCount;
		}
	}
}
