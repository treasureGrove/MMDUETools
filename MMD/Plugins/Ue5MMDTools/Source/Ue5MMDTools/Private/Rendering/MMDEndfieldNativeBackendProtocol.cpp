#include "Rendering/MMDEndfieldNativeBackend.h"

#include "GlobalShader.h"
#include "RHI.h"
#include "ShaderCore.h"
#include "ShaderParameters.h"

class FMMDEndfieldNativeBackendProtocolCS : public FGlobalShader
{
	DECLARE_GLOBAL_SHADER(FMMDEndfieldNativeBackendProtocolCS);

public:
	FMMDEndfieldNativeBackendProtocolCS() = default;

	FMMDEndfieldNativeBackendProtocolCS(const ShaderMetaType::CompiledShaderInitializerType& Initializer)
		: FGlobalShader(Initializer)
	{
		// 只在编译构造时从真实反射写入；Cooked 加载由内存镜像恢复，默认值永远是零。
		cooked_protocol = Initializer.ParameterMap.ContainsParameterAllocation(TEXT("mmd_endfield_cooked_protocol_14"))
			? MMDEndfieldNativeBackend::CookedGlobalShaderProtocolVersion : 0u;
		protocol_parameter.Bind(Initializer.ParameterMap, TEXT("mmd_endfield_cooked_protocol_14"), SPF_Optional);
		output_parameter.Bind(Initializer.ParameterMap, TEXT("mmd_protocol_output"), SPF_Mandatory);
	}

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}

	uint32 GetCookedProtocol() const { return cooked_protocol; }

private:
	LAYOUT_FIELD_INITIALIZED(uint32, cooked_protocol, 0u);
	LAYOUT_FIELD(FShaderParameter, protocol_parameter);
	LAYOUT_FIELD(FShaderResourceParameter, output_parameter);
};

IMPLEMENT_GLOBAL_SHADER(FMMDEndfieldNativeBackendProtocolCS,
	"/Plugin/Ue5MMDTools/Private/MMDEndfieldNativeBackendProtocol.usf", "MainCS", SF_Compute);

namespace MMDEndfieldNativeBackend
{
	bool ReadGlobalShaderProtocol(uint32& OutProtocol, FString& OutReason)
	{
		OutProtocol = 0u;
		if (!IsInGameThread())
		{
			OutReason = TEXT("Global shader protocol must be read on the game thread after engine initialization.");
			return false;
		}
		if (GUsingNullRHI || GMaxRHIFeatureLevel < ERHIFeatureLevel::SM5
			|| static_cast<uint32>(GMaxRHIShaderPlatform) >= static_cast<uint32>(SP_NumPlatforms))
		{
			OutReason = TEXT("The active RHI has no supported SM5+ global shader map.");
			return false;
		}
		// 不调用可能创建 map 或触发 checkf 的模板入口；缺少库时保持关闭。
		const FGlobalShaderMap* ShaderMap = GGlobalShaderMap[GMaxRHIShaderPlatform];
		if (!ShaderMap)
		{
			OutReason = TEXT("The active RHI global shader map is not loaded.");
			return false;
		}
		const TShaderRef<FShader> Shader = ShaderMap->GetShader(&FMMDEndfieldNativeBackendProtocolCS::GetStaticType(), 0);
		if (!Shader.IsValid())
		{
			OutReason = TEXT("The global shader library has no Endfield backend protocol shader.");
			return false;
		}
		OutProtocol = TShaderRef<FMMDEndfieldNativeBackendProtocolCS>::Cast(Shader)->GetCookedProtocol();
		OutReason = FString::Printf(TEXT("Loaded global shader reflection recorded protocol %u; expected %u. This does not verify all material kernels or GPU execution."),
			OutProtocol, CookedGlobalShaderProtocolVersion);
		return true;
	}
}
