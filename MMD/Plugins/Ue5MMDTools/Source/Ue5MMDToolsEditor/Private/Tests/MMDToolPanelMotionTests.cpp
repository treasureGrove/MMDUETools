#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "UI/MMDImportSetting.h"
#include "UI/MMDViewPanel.h"
#include "Animation/AnimSequence.h"
#include "Animation/MMDPhysicsPlaybackAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Import/MMDModelDataAsset.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "ImageUtils.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SWindow.h"
#include "Tests/AutomationEditorCommon.h"
#include "Editor.h"
#include "UI/MMDPreviewMotionPresets.h"
#include "UI/MMDPreviewFrameTime.h"
#include "Rendering/UMMDLightingEnvironmentLibrary.h"
#include "TVMDParser.h"
#include "Interfaces/IPluginManager.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "InputKeyEventArgs.h"
#include "Widgets/Input/SSlider.h"
#include <cmath>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDToolPanelMotionTest, "Ue5MMDTools.UI.MotionPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMMDPreviewFrameTimeTest, "Ue5MMDTools.UI.FrameStepping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMMDPreviewFrameTimeTest::RunTest(const FString& Parameters)
{
	for (const auto& Sample : { TPair<FFrameRate, int64>(FFrameRate(30, 1), 2850),
		TPair<FFrameRate, int64>(FFrameRate(60, 1), 216000),
		TPair<FFrameRate, int64>(FFrameRate(240, 1), 864000),
		TPair<FFrameRate, int64>(FFrameRate(30000, 1001), 107892) })
	{
		const auto TimeAt = [&Sample](int64 Frame) { return static_cast<float>(static_cast<double>(Frame) * Sample.Key.Denominator / Sample.Key.Numerator); };
		const float Time = TimeAt(Sample.Value);
		const float Duration = TimeAt(Sample.Value + 600);
		const FString Label = FString::Printf(TEXT("%d/%d fps，第 %lld 帧"), Sample.Key.Numerator, Sample.Key.Denominator, Sample.Value);
		TestEqual(TEXT("长片精确前进一帧：") + Label, MMDGetAdjacentPreviewFrameTime(Time, Duration, Sample.Key, 1), TimeAt(Sample.Value + 1));
		TestEqual(TEXT("长片精确后退一帧：") + Label, MMDGetAdjacentPreviewFrameTime(Time, Duration, Sample.Key, -1), TimeAt(Sample.Value - 1));
		TestEqual(TEXT("不吞掉采样点之前的相邻 float：") + Label,
			MMDGetAdjacentPreviewFrameTime(std::nextafter(Time, 0.0f), Duration, Sample.Key, 1), Time);
		TestEqual(TEXT("不吞掉采样点之后的相邻 float：") + Label,
			MMDGetAdjacentPreviewFrameTime(std::nextafter(Time, Duration), Duration, Sample.Key, -1), Time);
		TestEqual(TEXT("长片尾端前进不循环：") + Label, MMDGetAdjacentPreviewFrameTime(Duration, Duration, Sample.Key, 1), Duration);
		TestEqual(TEXT("长片尾端准确后退一帧：") + Label, MMDGetAdjacentPreviewFrameTime(Duration, Duration, Sample.Key, -1), TimeAt(Sample.Value + 599));
	}
	TestEqual(TEXT("首帧后退不循环"), MMDGetAdjacentPreviewFrameTime(0.0f, 100.0f, FFrameRate(30, 1), -1), 0.0f);
	return true;
}

namespace
{
	TSharedPtr<SWidget> FindMotionWidget(TSharedRef<SWidget> Root, FName Tag)
	{
		if (Root->GetTag() == Tag) return Root;
		FChildren* Children = Root->GetChildren();
		for (int32 Index = 0; Index < Children->Num(); ++Index)
			if (auto Found = FindMotionWidget(Children->GetChildAt(Index), Tag)) return Found;
		return nullptr;
	}
	bool ClickMotionWidget(TSharedRef<SWidget> Root, FName Tag)
	{
		auto Widget = FindMotionWidget(Root, Tag);
		if (!Widget || !Widget->IsEnabled() || Widget->GetCachedGeometry().GetLocalSize().IsNearlyZero()) return false;
		const auto& Geometry = Widget->GetCachedGeometry();
		const FVector2D Position = Geometry.LocalToAbsolute(Geometry.GetLocalSize() * 0.5f);
		FSlateApplication& Slate = FSlateApplication::Get();
		Slate.ProcessMouseMoveEvent(FPointerEvent(0, Position, Position, TSet<FKey>(), EKeys::Invalid, 0, FModifierKeysState()), true);
		Slate.ProcessMouseButtonDownEvent(nullptr, FPointerEvent(0, Position, Position, TSet<FKey>{EKeys::LeftMouseButton}, EKeys::LeftMouseButton, 0, FModifierKeysState()));
		Slate.ProcessMouseButtonUpEvent(FPointerEvent(0, Position, Position, TSet<FKey>(), EKeys::LeftMouseButton, 0, FModifierKeysState()));
		return true;
	}
	struct FMotionSampleMount
	{
		FString Root, Directory;
		~FMotionSampleMount() { if (!Root.IsEmpty()) FPackageName::UnRegisterMountPoint(Root, Directory); }
	};
	void StepMotion(USkeletalMeshComponent* Component, int32 Frames)
	{
		for (int32 Index = 0; Index < Frames; ++Index)
		{
			Component->TickAnimation(1.0f / 60.0f, false);
			Component->RefreshBoneTransforms();
		}
	}
	float MaxPoseRotation(const TArray<FTransform>& A, const TArray<FTransform>& B)
	{
		float Degrees = 0;
		for (int32 Index = 0; Index < FMath::Min(A.Num(), B.Num()); ++Index)
			Degrees = FMath::Max(Degrees, float(FMath::RadiansToDegrees(A[Index].GetRotation().AngularDistance(B[Index].GetRotation()))));
		return Degrees;
	}
	class FMotionCaptureWarmup : public IAutomationLatentCommand
	{
	public:
		explicit FMotionCaptureWarmup(TSharedRef<FEditorViewportClient> InClient) : Client(InClient) {}
		bool Update() override
		{
			Client->Tick(1.0f / 60.0f);
			Client->Invalidate();
			if (Client->Viewport) Client->Viewport->Draw();
			return --Frames <= 0;
		}
	private:
		TSharedRef<FEditorViewportClient> Client;
		int32 Frames = 60;
	};
}

bool FMMDToolPanelMotionTest::RunTest(const FString& Parameters)
{
	FAutomationEditorCommonUtils::CreateNewMap();
	TSharedRef<MMDImportSetting> Panel = SNew(MMDImportSetting);
	auto View = Panel->ViewPanel;
	FText Message;
	TestFalse(TEXT("空预览不播放动作"), View->PlayBuiltinMotion(TEXT("Clothes"), Message));
	View->StepPreviewFrame(1);
	TestFalse(TEXT("空预览逐帧操作无副作用"), View->HasPreviewAnimation());
	TestEqual(TEXT("空预览采样间隔使用安全兜底"), View->GetPreviewFrameInterval(), 1.0f / 30.0f);
	const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("Ue5MMDTools"));
	for (const auto& Preset : MMDGetPreviewMotionPresets())
	{
		TVMDParser Parser;
		TestTrue(TEXT("配布 VMD 可由正式解析器读取：") + Preset.Id.ToString(),
			Plugin && Parser.ParseVMDFile(Plugin->GetBaseDir() / TEXT("Resources/PreviewMotions") / Preset.RelativePath));
		TestTrue(TEXT("作者动作含完整骨骼关键帧"), Parser.VMDInfo.BoneKeyframes.Num() >= 300);
	}
	FString SampleDirectory;
	if (!FParse::Value(FCommandLine::Get(), TEXT("MMDPanelSampleDir="), SampleDirectory))
	{
		AddWarning(TEXT("已检查内置 VMD；未提供 MMDPanelSampleDir，跳过依赖本地 MMD 物理样本的集成验收。"));
		return true;
	}
	SampleDirectory = FPaths::ConvertRelativePathToFull(SampleDirectory);
	FPaths::NormalizeDirectoryName(SampleDirectory);
	if (!TestTrue(TEXT("样本只读挂载且不复制到内容目录"), FPaths::IsUnderDirectory(SampleDirectory, FPaths::ProjectSavedDir() / TEXT("BackgroundValidation/ImportedAssets")))) return false;
	const FString Name = FPaths::GetCleanFilename(SampleDirectory);
	TSharedRef<FMotionSampleMount> Mount = MakeShared<FMotionSampleMount>();
	Mount->Directory = SampleDirectory + TEXT("/");
	Mount->Root = TEXT("/Game/MMDModels/") + Name + TEXT("/");
	FPackageName::RegisterMountPoint(Mount->Root, Mount->Directory);
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, *(Mount->Root + TEXT("Model/") + Name + TEXT(".") + Name));
	if (!TestNotNull(TEXT("本地 MMD 样本"), Mesh)) return false;
	const bool bMeshDirty = Mesh->GetOutermost()->IsDirty();
	const bool bSkeletonDirty = Mesh->GetSkeleton()->GetOutermost()->IsDirty();
	const auto Materials = Mesh->GetMaterials();
	View->ShowImportedSkeletalMesh(Mesh);
	Panel->SetSelectedModelTextUI(FText::FromString(TEXT("本地 MMD 验证角色")));
	USkeletalMeshComponent* Component = View->GetPreviewMeshComponent();
	TestEqual(TEXT("加载 MMD 默认进入恬静待机"), View->GetBuiltinMotion(), FName(TEXT("Idle")));
	TStrongObjectPtr<UAnimSequence> FirstAnimation;
	for (const auto& Preset : MMDGetPreviewMotionPresets())
	{
		if (!TestTrue(TEXT("配布动作可映射并播放：") + Preset.Id.ToString(), View->PlayBuiltinMotion(Preset.Id, Message))) return false;
		auto* Animation = Cast<UAnimSequence>(Component->GetSingleNodeInstance()->GetCurrentAsset());
		if (!TestNotNull(TEXT("实际生成动画"), Animation)) return false;
		TestTrue(TEXT("预设只存在临时包，没有生成内容资产"), Animation->HasAnyFlags(RF_Transient) && Animation->GetOutermost() == GetTransientPackage());
		TestEqual(TEXT("保留作者原始动作时长"), View->GetPreviewDuration(), Preset.Duration, 0.001f);
		StepMotion(Component, 2);
	}
	FirstAnimation.Reset(Cast<UAnimSequence>(Component->GetSingleNodeInstance()->GetCurrentAsset()));
	View->PlayBuiltinMotion(TEXT("Idle"), Message);
	View->PlayBuiltinMotion(TEXT("Clothes"), Message);
	TestEqual(TEXT("重复选择复用当前骨架的临时动画"), Component->GetSingleNodeInstance()->GetCurrentAsset(), static_cast<UAnimationAsset*>(FirstAnimation.Get()));
	View->SetPreviewPhysicsEnabled(false);
	View->PlayBuiltinMotion(TEXT("Idle"), Message);
	auto* Playback = Cast<UMMDPhysicsPlaybackAnimInstance>(Component->GetAnimInstance());
	if (!TestNotNull(TEXT("使用实时 MMD 播放实例"), Playback)) return false;
	TestFalse(TEXT("切换动作保留物理关闭选择"), Playback->IsPhysicsEnabled());
	StepMotion(Component, 2);
	View->SetPreviewPhysicsEnabled(true);
	StepMotion(Component, 120);
	TestTrue(TEXT("真实 Bullet 世界初始化"), Playback->IsPhysicsInitialized());
	TestTrue(TEXT("样本有真实 MMD 刚体"), Playback->GetPhysicsRigidBodyCount() > 0);
	const int32 RigidCount = Playback->GetPhysicsRigidBodyCount();
	for (const auto& Transform : Component->GetComponentSpaceTransforms())
		TestFalse(TEXT("物理计算没有产生非有限骨骼变换"), Transform.ContainsNaN());
	View->SetPreviewPlaying(false);
	const auto WithPhysics = Component->GetComponentSpaceTransforms();
	View->SetPreviewPhysicsEnabled(false);
	StepMotion(Component, 1);
	const float PhysicsDelta = MaxPoseRotation(WithPhysics, Component->GetComponentSpaceTransforms());
	TestTrue(TEXT("同一动画时间下 Bullet 实际改变头发／衣物骨骼"), PhysicsDelta > 0.1f);
	View->SetPreviewPhysicsEnabled(true);
	View->SetPreviewPlaying(true);
	StepMotion(Component, 60);
	View->SetPreviewPlaying(false);
	const float PausedTime = View->GetPreviewTime();
	const auto PausedPose = Component->GetComponentSpaceTransforms();
	StepMotion(Component, 20);
	TestEqual(TEXT("暂停冻结动作时间"), View->GetPreviewTime(), PausedTime);
	TestTrue(TEXT("暂停保留物理姿态"), MaxPoseRotation(PausedPose, Component->GetComponentSpaceTransforms()) < 0.02f);
	View->SeekPreviewAnimation(View->GetPreviewDuration() - 0.02f);
	TestFalse(TEXT("拖动时间重置旧物理状态"), Playback->IsPhysicsInitialized());
	StepMotion(Component, 1);
	View->SetPreviewPlaying(true);
	StepMotion(Component, 1);
	TestTrue(TEXT("循环前 Bullet 已初始化"), Playback->IsPhysicsInitialized());
	StepMotion(Component, 1);
	TestTrue(TEXT("片尾循环不误判为时间跳转"), Playback->IsPhysicsInitialized());
	TestTrue(TEXT("时间已自然循环"), View->GetPreviewTime() < 0.1f);
	const auto PoseA = Component->GetComponentSpaceTransforms();
	View->PlayBuiltinMotion(TEXT("Clothes"), Message);
	StepMotion(Component, 60);
	TestTrue(TEXT("整理衣摆动作有真实骨骼变化"), MaxPoseRotation(PoseA, Component->GetComponentSpaceTransforms()) > 10.0f);
	const auto Environment = View->GetLightingEnvironment();
	View->SetPreviewView(EMMDPreviewView::FullBody, true);
	const FVector Full = View->CustomViewportClient->GetViewLocation();
	for (auto Camera : {EMMDPreviewView::UpperBody, EMMDPreviewView::Face, EMMDPreviewView::Side, EMMDPreviewView::Back})
	{
		View->SetPreviewView(Camera, true);
		TestFalse(TEXT("视角按钮实际改变取景"), View->CustomViewportClient->GetViewLocation().Equals(Full));
		TestEqual(TEXT("切镜头不改变 LookDev 环境"), View->GetLightingEnvironment(), Environment);
	}
	View->SetPreviewView(EMMDPreviewView::Face, true);
	const FVector Face = View->CustomViewportClient->GetViewLocation();
	View->SetPreviewView(EMMDPreviewView::FullBody, true);
	View->SetPreviewView(EMMDPreviewView::Face);
	for (int32 Index = 0; Index < 30; ++Index) View->CustomViewportClient->Tick(1.0f / 60.0f);
	TestEqual(TEXT("平滑镜头抵达目标"), View->CustomViewportClient->GetViewLocation(), Face, 1.0f);
	for (FKey PlaybackKey : { EKeys::SpaceBar, EKeys::Home })
	{
		View->SetPreviewPlaying(true);
		View->SetPreviewView(EMMDPreviewView::Face, true);
		const FVector CameraTarget = View->CustomViewportClient->GetViewLocation();
		View->SetPreviewView(EMMDPreviewView::FullBody, true);
		View->SetPreviewView(EMMDPreviewView::Face);
		View->CustomViewportClient->Tick(0.08f);
		TestFalse(TEXT("快捷键在镜头过渡中途输入"), View->CustomViewportClient->GetViewLocation().Equals(CameraTarget, 1.0f));
		TestTrue(TEXT("视口实际处理播放快捷键：") + PlaybackKey.ToString(), View->CustomViewportClient->InputKey(
			FInputKeyEventArgs::CreateSimulated(PlaybackKey, IE_Pressed, 1.0f, -1, INPUTDEVICEID_NONE, false, View->CustomViewportClient->Viewport)));
		TestEqual(TEXT("过渡中快捷键仍切换播放状态：") + PlaybackKey.ToString(), View->IsPreviewPlaying(), PlaybackKey == EKeys::Home);
		for (int32 Index = 0; Index < 30; ++Index) View->CustomViewportClient->Tick(1.0f / 60.0f);
		TestEqual(TEXT("播放快捷键不截断镜头过渡：") + PlaybackKey.ToString(), View->CustomViewportClient->GetViewLocation(), CameraTarget, 1.0f);
		TestTrue(TEXT("过渡结束仍选中目标机位"), View->GetPreviewView() == EMMDPreviewView::Face);
	}
	View->ResetPreviewPhysics();
	TestFalse(TEXT("重置物理立即使旧世界失效"), Playback->IsPhysicsInitialized());
	StepMotion(Component, 2);
	TestTrue(TEXT("下一帧恢复实时物理"), Playback->IsPhysicsInitialized());
	TestFalse(TEXT("无效预设不替换当前动作"), View->PlayBuiltinMotion(TEXT("Unknown"), Message));
	TestEqual(TEXT("无效预设保留现有选择"), View->GetBuiltinMotion(), FName(TEXT("Clothes")));

	View->SeekPreviewAnimation(0.65f);
	View->SetPreviewPlaying(false);
	View->SetPreviewPlaybackSpeed(0.5f);
	View->PlayBuiltinMotion(TEXT("Clothes"), Message);
	TestEqual(TEXT("重复点击当前动作不回零"), View->GetPreviewTime(), 0.65f);
	TestFalse(TEXT("重复点击不改变暂停"), View->IsPreviewPlaying());
	View->PlayBuiltinMotion(TEXT("Think"), Message);
	TestFalse(TEXT("切换新动作保留暂停"), View->IsPreviewPlaying());
	TestEqual(TEXT("切换动作保留倍速"), Component->GetSingleNodeInstance()->GetPlayRate(), 0.5f);
	View->RestartPreviewAnimation();
	StepMotion(Component, 30);
	TestEqual(TEXT("半速实际推进动画时间"), View->GetPreviewTime(), 0.25f, 0.002f);
	View->SeekPreviewAnimation(0.8f);
	View->SetPreviewPoseHeld(true);
	StepMotion(Component, 2);
	const float HoldTime = View->GetPreviewTime();
	const auto HoldPose = Component->GetComponentSpaceTransforms();
	StepMotion(Component, 90);
	TestEqual(TEXT("定格固定动作时间"), View->GetPreviewTime(), HoldTime);
	TestTrue(TEXT("定格时仍处于物理播放状态"), View->IsPreviewPlaying() && Playback->IsPhysicsInitialized());
	const float HoldPhysicsDelta = MaxPoseRotation(HoldPose, Component->GetComponentSpaceTransforms());
	TestTrue(TEXT("定格时 Bullet 仍改变头发／衣物"), HoldPhysicsDelta > 0.1f);
	const float RelativeHold = HoldTime / View->GetPreviewDuration();
	View->PlayBuiltinMotion(TEXT("Breath"), Message);
	TestTrue(TEXT("定格切动作仍保持定格"), View->IsPreviewPoseHeld());
	TestEqual(TEXT("定格切动作保留相对时间位置"), View->GetPreviewTime() / View->GetPreviewDuration(), RelativeHold, 0.002f);
	View->BeginPreviewScrub();
	View->SeekPreviewAnimation(0.4f);
	TestFalse(TEXT("拖动时临时暂停"), View->IsPreviewPlaying());
	View->EndPreviewScrub();
	TestTrue(TEXT("拖动结束恢复播放"), View->IsPreviewPlaying());
	TestTrue(TEXT("拖动不会解除定格"), View->IsPreviewPoseHeld());
	View->SetPreviewPlaying(false);
	View->BeginPreviewScrub();
	View->SeekPreviewAnimation(0.6f);
	View->EndPreviewScrub();
	TestFalse(TEXT("原先暂停时拖动结束仍暂停"), View->IsPreviewPlaying());
	TestTrue(TEXT("空格快捷键被预览接收"), View->HandlePreviewShortcut(EKeys::SpaceBar));
	TestTrue(TEXT("空格恢复播放"), View->IsPreviewPlaying());
	View->HandlePreviewShortcut(EKeys::Home);
	TestFalse(TEXT("重播退出定格"), View->IsPreviewPoseHeld());
	TestEqual(TEXT("重播回到起点"), View->GetPreviewTime(), 0.0f);
	View->HandlePreviewShortcut(EKeys::Three);
	TestTrue(TEXT("数字键选择面部机位并记录状态"), View->GetPreviewView() == EMMDPreviewView::Face);
	View->ClearPreviewViewSelection();
	TestFalse(TEXT("手动导航清除预设机位选中态"), View->GetPreviewView().IsSet());
	const float FrameInterval = View->GetPreviewFrameInterval();
	TestEqual(TEXT("逐帧间隔取自实际 VMD 动画的 30fps 采样率"), FrameInterval, 1.0f / 30.0f);
	View->StepPreviewFrame(0);
	TestTrue(TEXT("零方向逐帧不改变播放状态"), View->IsPreviewPlaying());
	View->SetPreviewPoseHeld(true);
	View->BeginPreviewScrub();
	View->SeekPreviewAnimation(0.115f);
	View->StepPreviewFrame(1);
	TestEqual(TEXT("从帧间位置前进到下一采样帧"), View->GetPreviewTime(), FrameInterval * 4, 0.0001f);
	TestFalse(TEXT("逐帧检查暂停动作与物理"), View->IsPreviewPlaying());
	TestTrue(TEXT("逐帧保留定格和物理开关"), View->IsPreviewPoseHeld() && View->IsPreviewPhysicsEnabled());
	TestEqual(TEXT("逐帧保留所选倍速"), View->GetPreviewPlaybackSpeed(), 0.5f);
	View->EndPreviewScrub();
	TestFalse(TEXT("逐帧后拖动结束不会恢复旧播放状态"), View->IsPreviewPlaying());
	StepMotion(Component, 2);
	const auto FramePose = Component->GetComponentSpaceTransforms();
	StepMotion(Component, 20);
	TestEqual(TEXT("逐帧检查期间时间保持稳定"), View->GetPreviewTime(), FrameInterval * 4, 0.0001f);
	TestTrue(TEXT("逐帧检查期间物理姿态保持稳定"), MaxPoseRotation(FramePose, Component->GetComponentSpaceTransforms()) < 0.02f);
	View->SeekPreviewAnimation(0.115f);
	View->StepPreviewFrame(-1);
	TestEqual(TEXT("从帧间位置后退到上一采样帧"), View->GetPreviewTime(), FrameInterval * 3, 0.0001f);
	View->SeekPreviewAnimation(0.0f);
	View->StepPreviewFrame(-1);
	TestEqual(TEXT("首帧后退不会绕到片尾"), View->GetPreviewTime(), 0.0f);
	View->SeekPreviewAnimation(View->GetPreviewDuration() - FrameInterval * 0.25f);
	View->StepPreviewFrame(1);
	TestEqual(TEXT("末帧前进夹住真实片尾"), View->GetPreviewTime(), View->GetPreviewDuration(), 0.0001f);
	StepMotion(Component, 2);
	View->StepPreviewFrame(1);
	TestTrue(TEXT("重复片尾前进不重建物理"), Playback->IsPhysicsInitialized());
	StepMotion(Component, 20);
	TestEqual(TEXT("片尾暂停后更新不会循环回零"), View->GetPreviewTime(), View->GetPreviewDuration(), 0.0001f);
	View->StepPreviewFrame(-1);
	TestEqual(TEXT("从片尾可准确后退一帧"), View->GetPreviewTime(), View->GetPreviewDuration() - FrameInterval, 0.0001f);
	View->SetPreviewPoseHeld(false);
	View->SetPreviewPlaybackSpeed(1.0f);
	AddInfo(FString::Printf(TEXT("Hold-pose physics evidence: time=%.4f, rotation_delta=%.4f degrees"), HoldTime, HoldPhysicsDelta));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"held_time_seconds\":%.5f,\"held_physics_rotation_delta_degrees\":%.5f}\n"), HoldTime, HoldPhysicsDelta),
		*(FPaths::ProjectSavedDir() / TEXT("UIValidation/interaction_metrics.json")));
	View->ClearPreviewAnimation();
	TestFalse(TEXT("原始姿态清除动作"), View->HasPreviewAnimation());
	TestFalse(TEXT("原始姿态停止播放"), View->IsPreviewPlaying());
	View->ShowImportedSkeletalMesh(Mesh);
	View->PlayBuiltinMotion(TEXT("Clothes"), Message);
	TestTrue(TEXT("换角色重新生成骨架专属动画"), Component != View->GetPreviewMeshComponent()
		&& View->GetPreviewMeshComponent()->GetSingleNodeInstance()->GetCurrentAsset() != FirstAnimation.Get());
	TestEqual(TEXT("不修改模型资产脏状态"), Mesh->GetOutermost()->IsDirty(), bMeshDirty);
	TestEqual(TEXT("不修改 Skeleton 资产脏状态"), Mesh->GetSkeleton()->GetOutermost()->IsDirty(), bSkeletonDirty);
	for (int32 Index = 0; Index < Materials.Num(); ++Index)
		TestEqual(TEXT("动作不修改模型材质"), Mesh->GetMaterials()[Index].MaterialInterface, Materials[Index].MaterialInterface);
	AddInfo(FString::Printf(TEXT("Motion physics evidence: rigid_bodies=%d, same_time_physics_rotation_delta=%.4f degrees"), RigidCount, PhysicsDelta));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("{\"rigid_bodies\":%d,\"same_time_physics_rotation_delta_degrees\":%.5f}\n"), RigidCount, PhysicsDelta),
		*(FPaths::ProjectSavedDir() / TEXT("UIValidation/motion_metrics.json")));
	const bool bInteractionCapture = FParse::Param(FCommandLine::Get(), TEXT("MMDPanelInteractionCapture"));
	if (FParse::Param(FCommandLine::Get(), TEXT("MMDPanelMotionCapture")) || bInteractionCapture)
	{
		Panel->ApplyRenderPreset(TEXT("/Ue5MMDTools/Stages/Presets/DA_Render_InfinityNikki"));
		TSharedRef<SWindow> Window = SNew(SWindow).Title(FText::FromString(TEXT("MMD 姿势与动作"))).ClientSize(FVector2D(1280, 900))[Panel];
		FSlateApplication::Get().AddWindow(Window);
		for (int32 Page = 0; Page < (bInteractionCapture ? 4 : 5); ++Page)
		{
			ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Panel, View, Page, Window, bInteractionCapture]()
			{
				FText Message;
				View->PlayBuiltinMotion(MMDGetPreviewMotionPresets()[Page < 4 ? Page : 0].Id, Message);
				View->SetPreviewView(Page == 0 ? EMMDPreviewView::FullBody : Page == 4 ? EMMDPreviewView::Face : EMMDPreviewView::UpperBody, true);
				if (Page == 4) Window->Resize(FVector2D(960, 720));
				View->SeekPreviewAnimation(Page == 2 ? 0.25f : 0.0f);
				if (bInteractionCapture)
				{
					Panel->WorkflowSwitcher->SetActiveWidgetIndex(Page == 0 ? 0 : Page == 2 ? 2 : 1);
					View->RestartPreviewAnimation();
					if (Page == 1 || Page == 2)
					{
						View->PlayBuiltinMotion(TEXT("Think"), Message);
						View->SeekPreviewAnimation(1.2f);
						View->SetPreviewPoseHeld(true);
						View->SetPreviewView(EMMDPreviewView::UpperBody, true);
					}
					if (Page == 3) { Window->Resize(FVector2D(960, 720)); View->SetPreviewPlaying(false); }
				}
				Panel->ShowImportProgress(Message.ToString(), EMMDMessageType::Info);
				FAssetCompilingManager::Get().FinishAllCompilation();
				if (GShaderCompilingManager) GShaderCompilingManager->FinishAllCompilation();
			}, 0.1f));
			ADD_LATENT_AUTOMATION_COMMAND(FMotionCaptureWarmup(View->CustomViewportClient.ToSharedRef()));
			ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([this, Panel, Page, View, Window, bInteractionCapture]()
			{
				if (bInteractionCapture)
				{
					TestTrue(TEXT("三个页签和窄窗口都能找到固定播放栏"), FindMotionWidget(Panel, TEXT("MMDPreviewControls")).IsValid());
					if (Page == 0)
					{
						TestTrue(TEXT("真实 Slate 指针事件点击暂停"), ClickMotionWidget(Panel, TEXT("MMDPreviewTogglePlay")));
						TestFalse(TEXT("按钮点击实际暂停"), View->IsPreviewPlaying());
						ClickMotionWidget(Panel, TEXT("MMDPreviewTogglePlay"));
						TestTrue(TEXT("按钮点击实际继续"), View->IsPreviewPlaying());
						ClickMotionWidget(Panel, TEXT("MMDPreviewHoldPose"));
						TestTrue(TEXT("按钮点击实际定格"), View->IsPreviewPoseHeld());
						ClickMotionWidget(Panel, TEXT("MMDPreviewHoldPose"));
						TestFalse(TEXT("按钮点击实际解除定格"), View->IsPreviewPoseHeld());
						View->SeekPreviewAnimation(0.1f);
						TestTrue(TEXT("真实 Slate 点击下一帧"), ClickMotionWidget(Panel, TEXT("MMDPreviewNextFrame")));
						TestEqual(TEXT("下一帧按钮对齐采样位置"), View->GetPreviewTime(), 4.0f / 30.0f, 0.0001f);
						TestFalse(TEXT("下一帧按钮暂停播放"), View->IsPreviewPlaying());
						TestTrue(TEXT("真实 Slate 点击上一帧"), ClickMotionWidget(Panel, TEXT("MMDPreviewPreviousFrame")));
						TestEqual(TEXT("上一帧按钮可回到前一采样位置"), View->GetPreviewTime(), 0.1f, 0.0001f);
						const auto Timeline = FindMotionWidget(Panel, TEXT("MMDPreviewTimeline"));
						if (TestTrue(TEXT("时间轴可定位"), Timeline.IsValid()))
						{
							TestEqual(TEXT("时间轴键盘步长为归一化的一帧"), StaticCastSharedPtr<SSlider>(Timeline)->GetStepSize(), View->GetPreviewFrameInterval() / View->GetPreviewDuration());
							FSlateApplication& Slate = FSlateApplication::Get();
							// 每次输入之间经过真实 UI 更新，刷新 SSlider 缓存的绑定值。
							Slate.Tick(ESlateTickType::Widgets);
							Slate.SetKeyboardFocus(Timeline, EFocusCause::SetDirectly);
							View->SetPreviewPlaying(true);
							auto PressKey = [&Slate](FKey Key)
							{
								const FKeyEvent Event(Key, FModifierKeysState(), 0, false, 0, 0);
								Slate.ProcessKeyDownEvent(Event);
								Slate.ProcessKeyUpEvent(Event);
								Slate.Tick(ESlateTickType::Widgets);
							};
							PressKey(EKeys::Enter);
							TestFalse(TEXT("时间轴 Enter 捕获暂时暂停播放"), View->IsPreviewPlaying());
							PressKey(EKeys::Right);
							TestEqual(TEXT("真实时间轴方向键前进一帧"), View->GetPreviewTime(), 4.0f / 30.0f, 0.0001f);
							PressKey(EKeys::Left);
							TestEqual(TEXT("真实时间轴方向键后退一帧"), View->GetPreviewTime(), 0.1f, 0.0001f);
							PressKey(EKeys::Enter);
							TestTrue(TEXT("结束键盘微调恢复原播放状态"), View->IsPreviewPlaying());
							View->SetPreviewPlaying(false);
						}
						const bool bWasPlayingBeforeResize = View->IsPreviewPlaying();
						const TOptional<EMMDPreviewView> ViewBeforeResize = View->GetPreviewView();
						const FVector2D WindowSizeBeforeResize = Window->GetClientSizeInScreen();
						View->SetPreviewPlaying(false);
						TestEqual(TEXT("窗口取景回归使用默认白色影棚"), View->GetLightingEnvironment(), EMMDLightingEnvironment::Studio3Point);
						TestNull(TEXT("窗口取景回归不覆盖舞台固定机位"), View->GetStagePreviewPreset());
						auto UpdateWindowLayout = [&View]()
						{
							FSlateApplication::Get().Tick();
							View->CustomViewportClient->Tick(0.0f);
							FSlateApplication::Get().Tick(ESlateTickType::Widgets);
							View->CustomViewportClient->Tick(0.0f);
						};
						UpdateWindowLayout();
						View->SetPreviewView(EMMDPreviewView::FullBody, true);
						const FIntPoint ViewportSizeBeforeResize = View->CustomViewportClient->Viewport->GetSizeXY();
						const FVector CameraBeforeResize = View->CustomViewportClient->GetViewLocation();
						const float TimeBeforeResize = View->GetPreviewTime();
						Window->Resize(FVector2D(960, 900));
						UpdateWindowLayout();
						TestNotEqual(TEXT("真实窗口缩放改变预览视口尺寸"), View->CustomViewportClient->Viewport->GetSizeXY(), ViewportSizeBeforeResize);
						const FVector CameraAfterResize = View->CustomViewportClient->GetViewLocation();
						TestFalse(TEXT("全身机位按新窗口比例重新取景"), CameraAfterResize.Equals(CameraBeforeResize, 1.0f));
						View->SetPreviewView(EMMDPreviewView::FullBody, true);
						TestEqual(TEXT("自动取景与当前实际比例计算一致"), CameraAfterResize, View->CustomViewportClient->GetViewLocation(), 1.0f);
						const FVector CameraWithoutResize = CameraAfterResize + FVector(7, 3, 2);
						View->CustomViewportClient->SetViewLocation(CameraWithoutResize);
						for (int32 Index = 0; Index < 3; ++Index) View->CustomViewportClient->Tick(0.0f);
						TestEqual(TEXT("尺寸稳定时不逐帧重复重取景"), View->CustomViewportClient->GetViewLocation(), CameraWithoutResize, 0.01f);
						View->ClearPreviewViewSelection();
						const FRotator ManualRotation = View->CustomViewportClient->GetViewRotation();
						Window->Resize(WindowSizeBeforeResize);
						UpdateWindowLayout();
						TestEqual(TEXT("手动机位在真实窗口缩放后保留位置"), View->CustomViewportClient->GetViewLocation(), CameraWithoutResize, 0.01f);
						TestEqual(TEXT("手动机位在窗口缩放后保留旋转"), View->CustomViewportClient->GetViewRotation(), ManualRotation, 0.01f);
						TestEqual(TEXT("窗口缩放不推进暂停动作"), View->GetPreviewTime(), TimeBeforeResize);
						if (ViewBeforeResize.IsSet()) View->SetPreviewView(ViewBeforeResize.GetValue(), true);
						View->SetPreviewPlaying(bWasPlayingBeforeResize);
					}
				}
				TArray<FColor> Pixels;
				FIntVector Size;
				if (!TestTrue(TEXT("实际 Slate 画面"), FSlateApplication::Get().TakeScreenshot(Panel, Pixels, Size))) return;
				TArray64<uint8> PNG;
				FImageUtils::PNGCompressImageArray(Size.X, Size.Y, Pixels, PNG);
				FFileHelper::SaveArrayToFile(PNG, *(FPaths::ProjectSavedDir() / TEXT("UIValidation") / FString::Printf(TEXT("%s_%d.png"), bInteractionCapture ? TEXT("interaction") : TEXT("motion"), Page)));
			}, 0.2f));
		}
		ADD_LATENT_AUTOMATION_COMMAND(FDelayedFunctionLatentCommand([Window, Mount]() { GEditor->SelectNone(false, true); Window->RequestDestroyWindow(); }, 0.1f));
	}
	else GEditor->SelectNone(false, true);
	return true;
}
#endif
