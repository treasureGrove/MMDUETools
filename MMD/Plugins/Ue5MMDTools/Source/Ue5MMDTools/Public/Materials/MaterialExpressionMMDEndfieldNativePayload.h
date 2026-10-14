#pragma once

#include "CoreMinimal.h"
#include "MaterialExpressionIO.h"
#include "MaterialValueType.h"
#include "Materials/MaterialExpressionCustomOutput.h"
#include "MaterialExpressionMMDEndfieldNativePayload.generated.h"

/** 在现有 BasePass 中传输脸部方向场；不产生额外 mesh pass。 */
UCLASS(collapsecategories, hidecategories = Object)
class UE5MMDTOOLS_API UMaterialExpressionMMDEndfieldNativePayload : public UMaterialExpressionCustomOutput
{
    GENERATED_BODY()

public:
    UMaterialExpressionMMDEndfieldNativePayload(const FObjectInitializer& object_initializer = FObjectInitializer::Get());

    UPROPERTY(meta = (ToolTip = "头部局部空间单位漫反射方向 xyz，有效覆盖 mask 放在 w；未连接时关闭。"))
    FExpressionInput FaceDirection;

    UPROPERTY(meta = (ToolTip = "头部局部空间到世界空间的单位 quaternion，顺序 xyzw。"))
    FExpressionInput HeadQuaternion;

    UPROPERTY(meta = (ToolTip = "精确 1 允许 Skin payload，其余值关闭。模式 0/1 应设为 0。"))
    FExpressionInput Enabled;

    virtual int32 GetNumOutputs() const override { return 3; }
    virtual FString GetFunctionName() const override { return TEXT("GetMMDEndfieldNativePayload"); }
    virtual FString GetDisplayName() const override { return TEXT("MMD Endfield Native Payload"); }

#if WITH_EDITOR
    virtual int32 Compile(FMaterialCompiler* compiler, int32 output_index) override;
    virtual void GetCaption(TArray<FString>& out_captions) const override;
    virtual FExpressionInput* GetInput(int32 input_index) override;
    virtual FName GetInputName(int32 input_index) const override;
    virtual EMaterialValueType GetInputValueType(int32 input_index) override;
#endif
};
