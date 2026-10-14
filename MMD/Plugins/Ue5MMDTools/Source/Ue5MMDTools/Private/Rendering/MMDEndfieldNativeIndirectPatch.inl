// 在适配器匿名 namespace 中 include；所有替换都要求固定源码锚点恰好出现一次。
bool MMDEndfieldBackendReplaceIndirectAnchor(FString& Text, const FString& Anchor, FString Replacement)
{
	int32 Index;
	if (!MMDEndfieldBackendSingleAnchor(Text, Anchor, Index)) return false;
	const FString Newline = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
	Replacement.ReplaceInline(TEXT("\r\n"), TEXT("\n"), ESearchCase::CaseSensitive);
	if (Newline != TEXT("\n")) Replacement.ReplaceInline(TEXT("\n"), *Newline, ESearchCase::CaseSensitive);
	Text.RemoveAt(Index, Anchor.Len());
	Text.InsertAt(Index, Replacement);
	return true;
}

bool MMDEndfieldBackendPatchIndirect(FString& Text)
{
	const FString IncludeAnchor = TEXT("#include \"Substrate/SubstrateEvaluation.ush\"");
	const FString AdaptiveAnchor = TEXT("// SSS Checkerboard");
	const FString LegacySnapshotAnchor = TEXT("IndirectLighting.Diffuse = (DiffuseIndirectLighting * DiffuseColor + BackfaceDiffuseIndirectLighting) * Occlusion.DiffuseOcclusion * EnergyPreservationFactor + ShortRangeGI * DiffuseColor;");
	const FString LegacyAnchor = TEXT("#if SUBSTRATE_ENABLED && SUBSTRATE_EXPERIMENTAL_TOON_REFLECTION_QUANTIZATION");
	int32 Index;
	for (const FString& Anchor : { IncludeAnchor, AdaptiveAnchor, LegacySnapshotAnchor, LegacyAnchor })
		if (!MMDEndfieldBackendSingleAnchor(Text, Anchor, Index)) return false;

	const FString Adaptive = TEXT(R"TMMD(if (BSDF_GETTYPE(BSDF) == SUBSTRATE_BSDF_TYPE_TOON)
        {
            TMMDEndfieldNativeIndirectData mmd_native_indirect_data = (TMMDEndfieldNativeIndirectData)0;
            mmd_native_indirect_data.path = uint(DIM_APPLY_DIFFUSE_INDIRECT);
            mmd_native_indirect_data.pixel_coord = PixelPos;
            mmd_native_indirect_data.closure_index = ClosureIndex;
            mmd_native_indirect_data.closure_count = SubstratePixelHeader.ClosureCount;
            mmd_native_indirect_data.reflection_is_ssr = bLumenReflectionInputIsSSR;
            mmd_native_indirect_data.lighting_is_pre_exposed = 1u;
            mmd_native_indirect_data.available_flags = 1u | 2u | 4u | 8u | 16u | 32u | 128u;
            mmd_native_indirect_data.buffer_uv = (float2(PixelPos) + 0.5) * View.BufferSizeAndInvSize.zw;
            mmd_native_indirect_data.normal = SubstrateBSDFContext.N;
            mmd_native_indirect_data.view_direction = V;
            mmd_native_indirect_data.roughness = Roughness;
            mmd_native_indirect_data.n_dot_v = SubstrateBSDFContext.SatNoV;
            mmd_native_indirect_data.pre_exposure = View.PreExposure;
            mmd_native_indirect_data.one_over_pre_exposure = View.OneOverPreExposure;
            mmd_native_indirect_data.diffuse_radiance = InDiffuseLighting;
            mmd_native_indirect_data.rough_specular_radiance = InRoughSpecularLighting;
            mmd_native_indirect_data.traced_reflection_radiance = InSpecularLighting;
            mmd_native_indirect_data.mixed_glossy_radiance = InGlossyLighting;
            mmd_native_indirect_data.short_range_gi = ShortRangeGI;
            mmd_native_indirect_data.diffuse_occlusion = Occlusion.DiffuseOcclusion;
            mmd_native_indirect_data.specular_occlusion = Occlusion.SpecularOcclusion;
            mmd_native_indirect_data.dynamic_ao = InAO;
            mmd_native_indirect_data.material_ao = SubstrateGetIrradianceAndAO(SubstratePixelHeader).MaterialAO;
            mmd_native_indirect_data.indirect_irradiance = SubstrateGetIrradianceAndAO(SubstratePixelHeader).IndirectIrradiance;
            mmd_native_indirect_data.bsdf_throughput = BSDFThroughput;
            mmd_native_indirect_data.diffuse_weight = SubstrateEnvLight.DiffuseWeight;
            mmd_native_indirect_data.specular_weight = SubstrateEnvLight.SpecularWeight;
            mmd_native_indirect_data.native_diffuse_final = OutBSDF.Diffuse;
            mmd_native_indirect_data.native_specular_final = OutBSDF.Specular;
            #if DIM_APPLY_DIFFUSE_INDIRECT == DIM_APPLY_DIFFUSE_INDIRECT_SCREEN_PROBE_GATHER
            mmd_native_indirect_data.available_flags |= 512u;
            mmd_native_indirect_data.final_scene_ao = 1.0;
            #endif
            TMMDEndfieldNativeIndirect mmd_native_indirect;
            mmd_native_indirect.Process(TOON_PROFILEID(BSDF), mmd_native_indirect_data, OutBSDF.Diffuse, OutBSDF.Specular);
        }

        // SSS Checkerboard)TMMD");

	const FString Legacy = TEXT(R"TMMD(#if SUBSTRATE_ENABLED
                if (GBuffer.ShadingModelID == SHADINGMODELID_SUBSTRATE_TOON)
                {
                    TMMDEndfieldNativeIndirectData mmd_native_indirect_data = (TMMDEndfieldNativeIndirectData)0;
                    mmd_native_indirect_data.path = uint(DIM_APPLY_DIFFUSE_INDIRECT);
                    mmd_native_indirect_data.pixel_coord = PixelPos;
                    mmd_native_indirect_data.closure_index = 0u;
                    mmd_native_indirect_data.closure_count = 1u;
                    mmd_native_indirect_data.reflection_is_ssr = bLumenReflectionInputIsSSR;
                    mmd_native_indirect_data.lighting_is_pre_exposed = 1u;
                    mmd_native_indirect_data.available_flags = 1u | 2u | 4u | 16u | 32u | 64u | 128u | 512u;
                    mmd_native_indirect_data.buffer_uv = SceneBufferUV;
                    mmd_native_indirect_data.translated_world_position = TranslatedWorldPosition;
                    mmd_native_indirect_data.normal = N;
                    mmd_native_indirect_data.view_direction = V;
                    mmd_native_indirect_data.roughness = GBuffer.Roughness;
                    mmd_native_indirect_data.n_dot_v = NoV;
                    mmd_native_indirect_data.pre_exposure = View.PreExposure;
                    mmd_native_indirect_data.one_over_pre_exposure = View.OneOverPreExposure;
                    mmd_native_indirect_data.diffuse_radiance = DiffuseIndirectLighting;
                    mmd_native_indirect_data.rough_specular_radiance = mmd_native_raw_rough_specular;
                    mmd_native_indirect_data.traced_reflection_radiance = SpecularIndirectLighting;
                    mmd_native_indirect_data.short_range_gi = ShortRangeGI;
                    mmd_native_indirect_data.diffuse_occlusion = Occlusion.DiffuseOcclusion;
                    mmd_native_indirect_data.specular_occlusion = Occlusion.SpecularOcclusion;
                    mmd_native_indirect_data.dynamic_ao = DynamicAmbientOcclusion;
                    mmd_native_indirect_data.material_ao = Material.MaterialAO;
                    mmd_native_indirect_data.final_scene_ao = FinalAmbientOcclusion;
                    mmd_native_indirect_data.bsdf_throughput = 1.0;
                    mmd_native_indirect_data.diffuse_weight = DiffuseColor * EnergyPreservationFactor;
                    mmd_native_indirect_data.specular_weight = EnvBRDFValue;
                    mmd_native_indirect_data.native_diffuse_final = IndirectLighting.Diffuse;
                    mmd_native_indirect_data.native_specular_final = IndirectLighting.Specular;
                    TMMDEndfieldNativeIndirect mmd_native_indirect;
                    mmd_native_indirect.Process(ToonProfileId, mmd_native_indirect_data, IndirectLighting.Diffuse, IndirectLighting.Specular);
                }
                #endif

            #if SUBSTRATE_ENABLED && SUBSTRATE_EXPERIMENTAL_TOON_REFLECTION_QUANTIZATION)TMMD");
	const FString Snapshot = TEXT("const float3 mmd_native_raw_rough_specular = RoughSpecularIndirectLighting;\n                ") + LegacySnapshotAnchor;
	const FString Include = IncludeAnchor + TEXT("\n#if SUBSTRATE_ENABLED\n#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeIndirect.ush\"\n#endif");
	return MMDEndfieldBackendReplaceIndirectAnchor(Text, AdaptiveAnchor, Adaptive)
		&& MMDEndfieldBackendReplaceIndirectAnchor(Text, LegacySnapshotAnchor, Snapshot)
		&& MMDEndfieldBackendReplaceIndirectAnchor(Text, LegacyAnchor, Legacy)
		&& MMDEndfieldBackendReplaceIndirectAnchor(Text, IncludeAnchor, Include);
}

bool MMDEndfieldBackendPatchReflection(FString& Text)
{
	const FString IncludeAnchor = TEXT("#include \"/Engine/Private/Substrate/SubstrateEvaluation.ush\"");
	const FString CommonCallAnchor = TEXT("\t\t\t\tDiffuseLighting,\n\t\t\t\tSpecularLighting);");
	const FString ReflectionSampleAnchor = TEXT("const FLumenReflectionOnlyOutput LumenReflections = GetLumenReflectionOnly(SubstrateEnvLight, SubstrateBSDFContext.BSDF, BSDFThroughput, Texture2DArraySample(LumenReflectionTexture, LumenReflectionTextureSampler, float3(BufferUV, ClosureIndex)));");
	const FString ProcessAnchor = TEXT("#if SUBSTRATE_EXPERIMENTAL_TOON_REFLECTION_QUANTIZATION");
	const FString Newline = Text.Contains(TEXT("\r\n")) ? TEXT("\r\n") : TEXT("\n");
	FString NativeCommonCall = CommonCallAnchor;
	if (Newline != TEXT("\n")) NativeCommonCall.ReplaceInline(TEXT("\n"), *Newline, ESearchCase::CaseSensitive);
	int32 Index;
	for (const FString& Anchor : { IncludeAnchor, NativeCommonCall, ReflectionSampleAnchor, ProcessAnchor })
		if (!MMDEndfieldBackendSingleAnchor(Text, Anchor, Index)) return false;

	const FString Process = TEXT(R"TMMD(if (BSDF_GETTYPE(SubstrateBSDFContext.BSDF) == SUBSTRATE_BSDF_TYPE_TOON)
            {
                TMMDEndfieldNativeIndirectData mmd_native_indirect_data = (TMMDEndfieldNativeIndirectData)0;
                mmd_native_indirect_data.path = 3u;
                mmd_native_indirect_data.pixel_coord = PixelPos;
                mmd_native_indirect_data.closure_index = ClosureIndex;
                mmd_native_indirect_data.closure_count = 1u;
                #if SUBSTRATE_LOAD_FROM_MATERIALCONTAINER
                mmd_native_indirect_data.closure_count = SubstratePixelHeader.ClosureCount;
                #endif
                mmd_native_indirect_data.available_flags = 4u | 32u | 64u;
                mmd_native_indirect_data.buffer_uv = BufferUV;
                mmd_native_indirect_data.translated_world_position = TranslatedWorldPosition;
                mmd_native_indirect_data.normal = SubstrateBSDFContext.N;
                mmd_native_indirect_data.view_direction = V;
                mmd_native_indirect_data.bent_normal = BentNormal;
                mmd_native_indirect_data.roughness = SubstrateGetBSDFRoughness(SubstrateBSDFContext.BSDF);
                mmd_native_indirect_data.n_dot_v = SubstrateBSDFContext.SatNoV;
                mmd_native_indirect_data.pre_exposure = View.PreExposure;
                mmd_native_indirect_data.one_over_pre_exposure = View.OneOverPreExposure;
                mmd_native_indirect_data.traced_reflection_radiance = mmd_native_reflection_input;
                mmd_native_indirect_data.dynamic_ao = AmbientOcclusion;
                mmd_native_indirect_data.material_ao = SubstrateIrradianceAndOcclusion.MaterialAO;
                mmd_native_indirect_data.combined_screen_material_ao = CombinedScreenAndMaterialAO;
                mmd_native_indirect_data.cloud_ao = CloudVolumetricAOShadow;
                mmd_native_indirect_data.indirect_irradiance = SubstrateIrradianceAndOcclusion.IndirectIrradiance;
                mmd_native_indirect_data.bsdf_throughput = BSDFThroughput;
                mmd_native_indirect_data.diffuse_weight = SubstrateEnvLight.DiffuseWeight;
                mmd_native_indirect_data.specular_weight = SubstrateEnvLight.SpecularWeight;
                mmd_native_indirect_data.native_diffuse_final = DiffuseLighting;
                mmd_native_indirect_data.native_specular_final = SpecularLighting;
                #if REFLECTION_SOURCE_TYPE == REFLECTION_SOURCE_LUMEN_STANDALONE
                mmd_native_indirect_data.path = 4u;
                mmd_native_indirect_data.lighting_is_pre_exposed = 1u;
                #else
                mmd_native_indirect_data.reflection_is_ssr = 1u;
                #endif
                TMMDEndfieldNativeIndirect mmd_native_indirect;
                mmd_native_indirect.Process(TOON_PROFILEID(SubstrateBSDFContext.BSDF), mmd_native_indirect_data, DiffuseLighting, SpecularLighting);
                // SSR 的原生 Toon 权重为零；loop 外的非零自定义 SSR 未接，不能称完整 SSR 改写。
            }

        #if SUBSTRATE_EXPERIMENTAL_TOON_REFLECTION_QUANTIZATION)TMMD");
	const FString Call = CommonCallAnchor + TEXT("\n\n            float4 mmd_native_reflection_input = 0.0;\n            #if REFLECTION_SOURCE_TYPE != REFLECTION_SOURCE_LUMEN_STANDALONE\n            mmd_native_reflection_input = SSRReflection;\n            #endif");
	const FString Sample = TEXT("mmd_native_reflection_input = Texture2DArraySample(LumenReflectionTexture, LumenReflectionTextureSampler, float3(BufferUV, ClosureIndex));\n                const FLumenReflectionOnlyOutput LumenReflections = GetLumenReflectionOnly(SubstrateEnvLight, SubstrateBSDFContext.BSDF, BSDFThroughput, mmd_native_reflection_input);");
	const FString Include = IncludeAnchor + TEXT("\n#if SUBSTRATE_ENABLED\n#include \"/Plugin/Ue5MMDTools/Core/TMMDEndfieldNativeIndirect.ush\"\n#endif");
	return MMDEndfieldBackendReplaceIndirectAnchor(Text, NativeCommonCall, Call)
		&& MMDEndfieldBackendReplaceIndirectAnchor(Text, ReflectionSampleAnchor, Sample)
		&& MMDEndfieldBackendReplaceIndirectAnchor(Text, ProcessAnchor, Process)
		&& MMDEndfieldBackendReplaceIndirectAnchor(Text, IncludeAnchor, Include);
}
