// UMMDMaterialEnsure.h —— MMD 父材质校验/自愈子系统
//
// 编辑器启动后（延迟数秒）自动校验 /Ue5MMDTools/Resources/MaterialInstance 下的
// 父材质 Custom 节点，修复"根因 #2"类问题：
//   - Custom 输入按各 usf 头注释的输入表补齐（漏连/缺失的输入接默认常量或纹理）；
//   - Custom 输出接入 EmissiveColor（Unlit 材质唯一生效输出）；
//   - ShadingModel 强制 Unlit、SkeletalMesh 用法标记；
//   - 缺失的父材质（M_MMD_Face / M_MMD_Hair）从零程序化创建。
// 幂等：可反复运行。控制台命令：MMD.EnsureMaterials
#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "UMMDMaterialEnsure.generated.h"

UCLASS()
class UMMDMaterialEnsure : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** 校验/修复全部 MMD 父材质。返回有修改的材质数。 */
	static int32 EnsureAllMaterials();
};
