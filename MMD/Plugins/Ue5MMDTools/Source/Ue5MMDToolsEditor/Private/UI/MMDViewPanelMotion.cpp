#include "UI/MMDViewPanel.h"
#include "Animation/AnimSequence.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Import/MMDModelDataAsset.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "TMMDMeshBuilder.h"
#include "TVMDParser.h"
#include "UI/MMDPreviewMotionPresets.h"
#include "UI/MMDPreviewFrameTime.h"

USkeletalMeshComponent* MMDViewPanel::GetPreviewMeshComponent() const
{
	return IsValid(PreviewActor) ? PreviewActor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

void MMDViewPanel::ResetPreviewMotionState()
{
	BuiltinMotions.Reset();
	CurrentBuiltinMotion = NAME_None;
	bPreviewPoseHeld = false;
	bPreviewScrubbing = false;
	CurrentPreviewView.Reset();
}

bool MMDViewPanel::PlayBuiltinMotion(FName Preset, FText& OutMessage)
{
	const FMMDPreviewMotionPreset* Motion = MMDFindPreviewMotionPreset(Preset);
	USkeletalMeshComponent* Component = GetPreviewMeshComponent();
	USkeletalMesh* Mesh = Component ? Component->GetSkeletalMeshAsset() : nullptr;
	if (!Motion || !Mesh || !Mesh->GetSkeleton())
	{
		OutMessage = FText::FromString(TEXT("请先加载带骨架的 MMD 角色。"));
		return false;
	}
	if (CurrentBuiltinMotion == Preset && HasPreviewAnimation())
	{
		OutMessage = FText::FromString(FString(Motion->Label) + TEXT(" · 当前动作；需要从头播放可点预览区的「重播」。"));
		return true;
	}
	UAnimSequence* Animation = nullptr;
	if (auto* Cached = BuiltinMotions.Find(Preset)) Animation = Cached->Get();
	if (!Animation)
	{
		const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
		const FString Path = Plugin ? Plugin->GetBaseDir() / TEXT("Resources/PreviewMotions") / Motion->RelativePath : FString();
		TVMDParser Parser;
		if (Path.IsEmpty() || !Parser.ParseVMDFile(Path))
		{
			OutMessage = FText::FromString(TEXT("内置 VMD 文件缺失或无法读取。"));
			return false;
		}
		FMMDAnimationImportContext Context;
		FMMDAnimationImportReport Report;
		TMMDMeshBuilder::BuildAnimationImportContext(Mesh, nullptr, FString(), Path, Context, &Report);
		FMMDAnimationImportSettings Settings;
		Settings.bTransientPreview = true;
		Settings.bImportMorphCurves = false;
		if (!TMMDMeshBuilder::AnalyzeVMDAnimationImport(Parser.VMDInfo, Context, Settings, Report)
			|| Report.MatchedBoneTrackCount < 6)
		{
			OutMessage = FText::FromString(TEXT("此骨架缺少标准 MMD 躯干／手臂骨骼，请使用适配它的动画。"));
			return false;
		}
		Animation = TMMDMeshBuilder::BuildVMDAnimation(Parser.VMDInfo, Context, Settings, &Report);
		if (!Animation)
		{
			OutMessage = FText::FromString(TEXT("未能生成当前骨架的预览动作，原动作保留。"));
			return false;
		}
		BuiltinMotions.Add(Preset, TStrongObjectPtr<UAnimSequence>(Animation));
	}
	if (!SetPreviewAnimation(Animation))
	{
		OutMessage = FText::FromString(TEXT("预览播放未能启动。"));
		return false;
	}
	CurrentBuiltinMotion = Preset;
	OutMessage = FText::FromString(FString(Motion->Label) + TEXT(" · ") + GetPreviewPlaybackStatus().ToString());
	return true;
}

void MMDViewPanel::ClearPreviewAnimation()
{
	if (USkeletalMeshComponent* Component = GetPreviewMeshComponent())
	{
		Component->SetAnimInstanceClass(nullptr);
		Component->SetAnimationMode(EAnimationMode::AnimationSingleNode, true);
		Component->SetAnimation(nullptr);
		Component->RefreshBoneTransforms();
	}
	CurrentBuiltinMotion = NAME_None;
	bPreviewPoseHeld = false;
	bPreviewScrubbing = false;
}

bool MMDViewPanel::HasPreviewModel() const
{
	const auto* Component = GetPreviewMeshComponent();
	return Component && Component->GetSkeletalMeshAsset();
}

bool MMDViewPanel::HasPreviewPhysics() const
{
	const auto* Component = GetPreviewMeshComponent();
	const auto* Instance = Component ? Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance()) : nullptr;
	return HasPreviewAnimation() && Instance && Instance->GetPhysicsRigidBodyCount() > 0;
}

void MMDViewPanel::SetPreviewPoseHeld(bool bHeld)
{
	if (!HasPreviewAnimation()) return;
	bPreviewPoseHeld = bHeld;
	if (auto* Instance = GetPreviewMeshComponent()->GetSingleNodeInstance())
		Instance->SetPlayRate(bHeld ? 0.0f : PreviewPlaybackSpeed);
}

void MMDViewPanel::SetPreviewPlaybackSpeed(float Speed)
{
	if (!FMath::IsFinite(Speed)) return;
	PreviewPlaybackSpeed = FMath::Clamp(Speed, 0.25f, 2.0f);
	if (auto* Component = GetPreviewMeshComponent())
		if (auto* Instance = Component->GetSingleNodeInstance())
			Instance->SetPlayRate(bPreviewPoseHeld ? 0.0f : PreviewPlaybackSpeed);
}

void MMDViewPanel::RestartPreviewAnimation()
{
	if (!HasPreviewAnimation()) return;
	bPreviewScrubbing = false;
	SetPreviewPoseHeld(false);
	SeekPreviewAnimation(0.0f);
	SetPreviewPlaying(true);
}

void MMDViewPanel::BeginPreviewScrub()
{
	if (bPreviewScrubbing || !HasPreviewAnimation()) return;
	bPreviewScrubWasPlaying = IsPreviewPlaying();
	bPreviewScrubbing = true;
	SetPreviewPlaying(false);
}

void MMDViewPanel::EndPreviewScrub()
{
	if (!bPreviewScrubbing) return;
	bPreviewScrubbing = false;
	SetPreviewPlaying(bPreviewScrubWasPlaying);
}

FText MMDViewPanel::GetPreviewPlaybackStatus() const
{
	if (!HasPreviewAnimation()) return FText::FromString(HasPreviewModel() ? TEXT("选择动作开始预览") : TEXT("先加载角色"));
	if (bPreviewScrubbing) return FText::FromString(TEXT("正在定位姿势"));
	if (!IsPreviewPlaying()) return FText::FromString(TEXT("已暂停"));
	return FText::FromString(bPreviewPoseHeld ? TEXT("姿势已定格") : TEXT("循环播放中"));
}

bool MMDViewPanel::HandlePreviewShortcut(FKey Key)
{
	if (!HasPreviewModel()) return false;
	if (Key == EKeys::SpaceBar && HasPreviewAnimation()) { SetPreviewPlaying(!IsPreviewPlaying()); return true; }
	if (Key == EKeys::Home && HasPreviewAnimation()) { RestartPreviewAnimation(); return true; }
	if (Key == EKeys::F) { SetPreviewView(EMMDPreviewView::FullBody); return true; }
	const FKey Keys[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five };
	for (int32 Index = 0; Index < 5; ++Index)
		if (Key == Keys[Index]) { SetPreviewView(static_cast<EMMDPreviewView>(Index)); return true; }
	return false;
}

bool MMDViewPanel::HasPreviewAnimation() const
{
	const auto* Component = GetPreviewMeshComponent();
	auto* Instance = Component ? Component->GetSingleNodeInstance() : nullptr;
	return Instance && Instance->GetCurrentAsset();
}

bool MMDViewPanel::IsPreviewPlaying() const
{
	const auto* Component = GetPreviewMeshComponent();
	auto* Instance = Component ? Component->GetSingleNodeInstance() : nullptr;
	return HasPreviewAnimation() && Instance->IsPlaying();
}

float MMDViewPanel::GetPreviewTime() const
{
	const auto* Component = GetPreviewMeshComponent();
	auto* Instance = Component ? Component->GetSingleNodeInstance() : nullptr;
	return Instance ? Instance->GetCurrentTime() : 0.0f;
}

float MMDViewPanel::GetPreviewDuration() const
{
	const auto* Component = GetPreviewMeshComponent();
	auto* Instance = Component ? Component->GetSingleNodeInstance() : nullptr;
	return Instance ? Instance->GetLength() : 0.0f;
}

FFrameRate MMDViewPanel::GetPreviewFrameRate() const
{
	const auto* Component = GetPreviewMeshComponent();
	auto* Instance = Component ? Component->GetSingleNodeInstance() : nullptr;
	const auto* Animation = Instance ? Cast<UAnimSequence>(Instance->GetCurrentAsset()) : nullptr;
	if (Animation)
	{
		const FFrameRate Rate = Animation->GetSamplingFrameRate();
		if (Rate.Numerator > 0 && Rate.Denominator > 0) return Rate;
	}
	return FFrameRate(30, 1);
}

float MMDViewPanel::GetPreviewFrameInterval() const
{
	return static_cast<float>(GetPreviewFrameRate().AsInterval());
}

void MMDViewPanel::StepPreviewFrame(int32 Direction)
{
	const float Length = GetPreviewDuration();
	if (Direction == 0 || !HasPreviewAnimation() || Length <= 0.0f) return;
	const float TargetTime = MMDGetAdjacentPreviewFrameTime(GetPreviewTime(), Length, GetPreviewFrameRate(), Direction);
	bPreviewScrubbing = false;
	SetPreviewPlaying(false);
	if (!FMath::IsNearlyEqual(GetPreviewTime(), TargetTime, UE_SMALL_NUMBER)) SeekPreviewAnimation(TargetTime);
	if (CustomViewportClient.IsValid()) CustomViewportClient->Invalidate();
}

void MMDViewPanel::SeekPreviewAnimation(float Time)
{
	USkeletalMeshComponent* Component = GetPreviewMeshComponent();
	if (auto* Instance = Component ? Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance()) : nullptr)
		Instance->SeekMMDAnimation(Time);
}

FText MMDViewPanel::GetPreviewPhysicsStatus() const
{
	const auto* Component = GetPreviewMeshComponent();
	auto* Instance = Component ? Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance()) : nullptr;
	FString Status;
	if (!Component) Status = TEXT("加载角色后可预览 MMD 物理");
	else if (!HasPreviewAnimation()) Status = TEXT("选择姿势／动作以启动实时物理");
	else if (!Instance || Instance->GetPhysicsRigidBodyCount() == 0) Status = TEXT("此模型没有 MMD 刚体，仅播放骨骼动画");
	else if (!bPreviewPhysicsEnabled) Status = TEXT("物理已关闭 · 仅骨骼动画");
	else if (!Instance->IsPhysicsInitialized()) Status = TEXT("正在初始化 MMD 物理…");
	else Status = FString::Printf(TEXT("%s · %d 个刚体"), IsPreviewPlaying() ? TEXT("物理运行中") : TEXT("物理已暂停"), Instance->GetPhysicsRigidBodyCount());
	return FText::FromString(Status);
}

FText MMDViewPanel::GetPreviewAnimationLabel() const
{
	if (!HasPreviewModel()) return FText::FromString(TEXT("未加载角色"));
	if (const auto* Motion = MMDFindPreviewMotionPreset(CurrentBuiltinMotion))
		return FText::FromString(FString(TEXT("Mei · ")) + Motion->Label);
	const auto* Component = GetPreviewMeshComponent();
	auto* Instance = Component ? Component->GetSingleNodeInstance() : nullptr;
	UAnimationAsset* Animation = Instance ? Instance->GetCurrentAsset() : nullptr;
	return FText::FromString(Animation ? Animation->GetName() : TEXT("原始姿态"));
}
