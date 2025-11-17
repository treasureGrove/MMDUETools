#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "TPMXParser.h"
#include "MMDAnimInstance.generated.h"

// 前置声明：避免头文件膨胀
class USkeletalMeshComponent;
class FMMDPhysicsSimulator;
class UMMDPhysicsSimulatorHolder;
class UMMDPhysicsRegistry;

class FMMDAnimInstanceProxy : public FAnimInstanceProxy
{
public:
    FMMDAnimInstanceProxy(UAnimInstance* InAnimInstance)
        : FAnimInstanceProxy(InAnimInstance)
    {}

    TWeakPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> CacheSimulator;
protected:
    virtual void Initialize(UAnimInstance* InAnimInstance) override;

};
// UCLASS 必须加；类名必须以 U 开头
UCLASS(BlueprintType)
class UE5MMDTOOLS_API UMMDAnimInstance : public UAnimInstance
{
    GENERATED_BODY()
public:

    // 原始初始化：外部传已解析数据（运行时构建）
    void ProvideMMDConfigAndInit(const PMXDatas& InPMXData, USkeletalMeshComponent* InSkelComp);

    // 仅记录源文件路径用于编辑器预览自动重建
    UFUNCTION(BlueprintCallable, Category="MMDPhysics")
    void SetSourcePMXFilePath(const FString& InFilePath){ SourcePMXFilePath = InFilePath; }

    UFUNCTION(BlueprintCallable, Category="MMDPhysics")
    void EnsureSimulator();

    TSharedPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> GetSimulator() const;
protected:
    // 生命周期：创建/重建时机（游戏线程）
    virtual void NativeInitializeAnimation() override;

    // 生命周期：销毁/重建前时机（游戏线程）
    virtual void NativeUninitializeAnimation() override;
    virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
    virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* InProxy) override;
    virtual void NativeUpdateAnimation(float DeltaSeconds) override;
private:
    // 弱引用组件，后续构建上下文用
    UPROPERTY(Transient)
    TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshComp;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MMDPhysics", meta=(AllowPrivateAccess="true"))
    FString SourcePMXFilePath; // Persist on CDO for preview rebuild

    UPROPERTY(Transient)
    UMMDPhysicsSimulatorHolder* Holder = nullptr;

    // legacy field kept to avoid breaking API; not used after holder integration
    TSharedPtr<FMMDPhysicsSimulator, ESPMode::ThreadSafe> Simulator;
    void BuildSimulatorNow(const PMXDatas& InPMXData);
    void DestroySimulatorNow();

    // 将当前 Simulator 同步到 Proxy（仅游戏线程调用）
    void SyncProxySimulator();

    void AcquireSharedHolder();
};