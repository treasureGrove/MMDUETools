#include "Materials/MaterialExpressionMMDEndfieldNativePayload.h"

#if WITH_EDITOR
#include "MaterialCompiler.h"
#endif

UMaterialExpressionMMDEndfieldNativePayload::UMaterialExpressionMMDEndfieldNativePayload(
    const FObjectInitializer& object_initializer)
    : Super(object_initializer)
{
#if WITH_EDITORONLY_DATA
    bCollapsed = true;
    Outputs.Reset();
    MenuCategories.Add(FText::FromString(TEXT("MMD")));
#endif
}

#if WITH_EDITOR
int32 UMaterialExpressionMMDEndfieldNativePayload::Compile(FMaterialCompiler* compiler, int32 output_index)
{
    int32 output_code = INDEX_NONE;
    if (output_index == 0)
        output_code = FaceDirection.GetTracedInput().Expression
            ? compiler->ForceCast(FaceDirection.Compile(compiler), MCT_Float4)
            : compiler->Constant4(0.0f, 1.0f, 0.0f, 0.0f);
    else if (output_index == 1)
        output_code = HeadQuaternion.GetTracedInput().Expression
            ? compiler->ForceCast(HeadQuaternion.Compile(compiler), MCT_Float4)
            : compiler->Constant4(0.0f, 0.0f, 0.0f, 1.0f);
    else if (output_index == 2)
        output_code = Enabled.GetTracedInput().Expression
            ? compiler->ForceCast(Enabled.Compile(compiler), MCT_Float1) : compiler->Constant(0.0f);
    else
        return compiler->Errorf(TEXT("MMD Endfield Native Payload output index must be 0, 1 or 2."));
    return compiler->CustomOutput(this, output_index, output_code);
}

void UMaterialExpressionMMDEndfieldNativePayload::GetCaption(TArray<FString>& out_captions) const
{
    out_captions.Add(TEXT("MMD Endfield Native Payload"));
}

FExpressionInput* UMaterialExpressionMMDEndfieldNativePayload::GetInput(int32 input_index)
{
    return input_index == 0 ? &FaceDirection : input_index == 1 ? &HeadQuaternion
        : input_index == 2 ? &Enabled : nullptr;
}

FName UMaterialExpressionMMDEndfieldNativePayload::GetInputName(int32 input_index) const
{
    return input_index == 0 ? FName(TEXT("Face direction + mask")) : input_index == 1 ? FName(TEXT("Head quaternion"))
        : input_index == 2 ? FName(TEXT("Enabled / Skin")) : NAME_None;
}

EMaterialValueType UMaterialExpressionMMDEndfieldNativePayload::GetInputValueType(int32 input_index)
{
    return input_index == 2 ? MCT_Float1 : MCT_Float4;
}
#endif
