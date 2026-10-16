// 由 NativeBackend.cpp 在 SingleAnchor/ReplaceIndirectAnchor 定义后 include。
// 只改变 Shader 虚拟源副本；完整引擎文件 SHA1 必须在调用前校验。
bool MMDEndfieldBackendPatchForwardInlinePayload(FString& Text)
{
    const FString FunctionAnchor = TEXT("float3 SubstrateForwardLighting(");
    const FString TreeAnchor = TEXT("SubstratePixelHeader.SubstrateUpdateTree(SubstrateData, V, Settings, OutCoverage, OutTransmittancePreCoverage);");
    int32 FunctionIndex, TreeIndex;
    if (!MMDEndfieldBackendSingleAnchor(Text, FunctionAnchor, FunctionIndex)
        || !MMDEndfieldBackendSingleAnchor(Text, TreeAnchor, TreeIndex) || TreeIndex <= FunctionIndex) return false;
    const FString Patch = TreeAnchor + TEXT(R"TMMD(
        TMMDEndfieldNativeForwardPayload mmd_forward_payload;
        mmd_forward_payload.Apply(MaterialParameters, SubstratePixelHeader);
)TMMD");
    if (!MMDEndfieldBackendReplaceIndirectAnchor(Text, TreeAnchor, Patch)) return false;
    const FString Newline = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
    Text.InsertAt(FunctionIndex, TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeForwardPayload.ush\"") + Newline + Newline);
    return true;
}

bool MMDEndfieldBackendPatchForwardLightData(FString& Text)
{
    const FString FunctionAnchor = TEXT("float3 SubstrateForwardLightingCommon(");
    const FString RoughnessAnchor = TEXT("float Roughness = SubstrateGetBSDFRoughness(SubstrateBSDFContext.BSDF);");
    int32 FunctionIndex, RoughnessIndex;
    if (!MMDEndfieldBackendSingleAnchor(Text, FunctionAnchor, FunctionIndex)
        || !MMDEndfieldBackendSingleAnchor(Text, RoughnessAnchor, RoughnessIndex) || RoughnessIndex <= FunctionIndex) return false;
    // 该 anchor 紧随原生 GetShadowTerms；保留所有原生 VSM/CSM/静态/接触阴影处理。
    const FString Patch = TEXT(R"TMMD(TMMDEndfieldNativeForwardLight mmd_forward_light;
        mmd_forward_light.Capture(SubstrateBSDFContext, LightData, ToLight, LightMask,
            LightAttenuation, PrecomputedShadowFactors, TranslatedWorldPosition, SceneDepth, Dither);

        )TMMD") + RoughnessAnchor;
    if (!MMDEndfieldBackendReplaceIndirectAnchor(Text, RoughnessAnchor, Patch)) return false;
    const FString Newline = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
    Text.InsertAt(FunctionIndex, TEXT("#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeForwardLight.ush\"") + Newline + Newline);
    return true;
}
