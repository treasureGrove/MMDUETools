#include "Stage/MMDStageLibrary.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/ToonProfile.h"
#include "HAL/IConsoleManager.h"
#include "Rendering/MMDEndfieldNativeBackend.h"
#include "Rendering/MMDEndfieldNativeProfile.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/MMDOutlineLibrary.h"
#include "Rendering/MMDEndfieldFaceData.h"
#include "Rendering/UMMDHeadBoneSubsystem.h"
#include "Engine/Texture2D.h"
#include "Stage/MMDStageMaterialState.h"
#include "Stage/MMDStageRenderPreset.h"
#include "Stage/MMDStageActor.h"
#include "RenderUtils.h"
#include <initializer_list>

namespace
{
	const FName CharacterShadowIncludeTag(TEXT("MMDShadowInclude"));
	const FName CharacterShadowExcludeTag(TEXT("MMDShadowExclude"));
	bool MMDStageToonApplyMaterialSlot(USkeletalMeshComponent* Component, UMMDStageMaterialState* State,
		UMMDStageRenderPreset* Preset, int32 MaterialIndex, bool bReplaceMaterials);

	void RestoreCharacterShadowTag(USkeletalMeshComponent* Component, const UMMDStageMaterialState* State)
	{
		if (State->bHasContactShadowSnapshot) Component->SetCastContactShadow(State->bHadContactShadow);
		if (!State->bHasShadowTagSnapshot) return;
		if (State->bHadShadowIncludeTag) Component->ComponentTags.AddUnique(CharacterShadowIncludeTag);
		else Component->ComponentTags.Remove(CharacterShadowIncludeTag);
		if (State->bHadShadowExcludeTag) Component->ComponentTags.AddUnique(CharacterShadowExcludeTag);
		else Component->ComponentTags.Remove(CharacterShadowExcludeTag);
	}

	void ClearOwnedOverridesAfterMeshChange(USkeletalMeshComponent* Component, UMMDStageMaterialState* State)
	{
		// 换网格时只清掉仍由预设占用的槽，保留用户为新网格另外设置的材质。
		for (int32 Index = 0; Index < Component->OverrideMaterials.Num(); ++Index)
			if (State->AppliedMaterials.IsValidIndex(Index) && Component->OverrideMaterials[Index] == State->AppliedMaterials[Index])
				Component->OverrideMaterials[Index] = nullptr;
		while (!Component->OverrideMaterials.IsEmpty() && !Component->OverrideMaterials.Last()) Component->OverrideMaterials.Pop();
		Component->MarkRenderStateDirty();
	}

	UMMDStageMaterialState* FindMaterialState(USkeletalMeshComponent* Component)
	{
		return Cast<UMMDStageMaterialState>(Component->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass()));
	}

	UMMDStageMaterialState* GetCurrentMaterialState(USkeletalMeshComponent* Component, bool bCreate)
	{
		UMMDStageMaterialState* State = FindMaterialState(Component);
		if (State && State->SourceMesh != Component->GetSkeletalMeshAsset())
		{
			UMMDOutlineLibrary::RestoreMMDOutline(Component);
			ClearOwnedOverridesAfterMeshChange(Component, State);
			RestoreCharacterShadowTag(Component, State);
			Component->RemoveUserDataOfClass(UMMDStageMaterialState::StaticClass());
			State = nullptr;
		}
		if (!State && bCreate)
		{
			State = NewObject<UMMDStageMaterialState>(Component, NAME_None, RF_Transient);
			State->SourceMesh = Component->GetSkeletalMeshAsset();
			Component->AddAssetUserData(State);
		}
		return State;
	}

	bool IsValidMaterialSlot(USkeletalMeshComponent* Component, int32 MaterialIndex)
	{
		return IsValid(Component) && Component->GetSkeletalMeshAsset()
			&& Component->GetSkeletalMeshAsset()->GetMaterials().IsValidIndex(MaterialIndex);
	}

	FString NormalizeRoleName(const FString& Name)
	{
		FString Result;
		Result.Reserve(Name.Len() * 2);
		for (int32 Index = 0; Index < Name.Len(); ++Index)
		{
			const TCHAR Character = Name[Index];
			if (Index > 0 && FChar::IsUpper(Character) && FChar::IsLower(Name[Index - 1])) Result.AppendChar(TEXT(' '));
			Result.AppendChar(FChar::IsAlnum(Character) ? FChar::ToLower(Character) : TEXT(' '));
		}
		return Result;
	}

	bool MatchEnglishRoleWord(const FString& Names, const TCHAR* Word)
	{
		const FString Term(Word);
		int32 SearchFrom = 0;
		while (SearchFrom < Names.Len())
		{
			const int32 Position = Names.Find(Term, ESearchCase::CaseSensitive, ESearchDir::FromStart, SearchFrom);
			if (Position == INDEX_NONE) return false;
			const int32 End = Position + Term.Len();
			// 数字后缀保留命中，例如 Hair01；不把 metallicless 等更长单词拆成部位。
			if ((Position == 0 || !FChar::IsAlpha(Names[Position - 1]))
				&& (End == Names.Len() || !FChar::IsAlpha(Names[End]))) return true;
			SearchFrom = Position + 1;
		}
		return false;
	}

	FString MatchRoleTerms(const FString& Names, std::initializer_list<const TCHAR*> English,
		std::initializer_list<const TCHAR*> Localized)
	{
		for (const TCHAR* Word : English) if (MatchEnglishRoleWord(Names, Word)) return Word;
		for (const TCHAR* Word : Localized) if (Names.Contains(Word, ESearchCase::CaseSensitive)) return Word;
		return FString();
	}

	UMaterialInterface* GetRoleSourceMaterial(USkeletalMeshComponent* Component, int32 MaterialIndex,
		const UMMDStageMaterialState* State)
	{
		if (State && State->SourceMesh == Component->GetSkeletalMeshAsset() && State->SourceMaterials.IsValidIndex(MaterialIndex))
			return State->SourceMaterials[MaterialIndex];
		UMaterialInterface* Source = Component->GetMaterial(MaterialIndex);
		if (State && State->SourceMesh != Component->GetSkeletalMeshAsset()
			&& State->AppliedMaterials.IsValidIndex(MaterialIndex) && Source == State->AppliedMaterials[MaterialIndex])
			return Component->GetSkeletalMeshAsset()->GetMaterials()[MaterialIndex].MaterialInterface;
		return Source;
	}

	EMMDMaterialRole ResolveMaterialRole(USkeletalMeshComponent* Component, int32 MaterialIndex,
		const UMMDStageMaterialState* State, FString& Reason)
	{
		if (!IsValidMaterialSlot(Component, MaterialIndex))
		{
			Reason = TEXT("generic:invalid_slot");
			return EMMDMaterialRole::Generic;
		}
		if (State && State->SourceMesh == Component->GetSkeletalMeshAsset())
		{
			if (const EMMDMaterialRole* Role = State->RoleOverrides.Find(MaterialIndex))
			{
				Reason = TEXT("override:component");
				return *Role;
			}
		}
		const FSkeletalMaterial& Slot = Component->GetSkeletalMeshAsset()->GetMaterials()[MaterialIndex];
		UMaterialInterface* Source = GetRoleSourceMaterial(Component, MaterialIndex, State);
		FString SlotNames = Slot.MaterialSlotName.ToString();
#if WITH_EDITORONLY_DATA
		SlotNames += TEXT(" ") + Slot.ImportedMaterialSlotName.ToString();
#endif
		const FString Names = NormalizeRoleName(SlotNames + TEXT(" ") + (Source ? Source->GetName() : FString()));
		// 睫毛、口腔和绘制高光先排除，不能当成湿润眼球。
		FString Match = MatchRoleTerms(Names,
			{ TEXT("eyelash"), TEXT("lashes"), TEXT("tooth"), TEXT("teeth"), TEXT("tongue"), TEXT("oral"), TEXT("mouth"), TEXT("hl"), TEXT("highlight") },
			{ TEXT("牙齿"), TEXT("歯"),
			  TEXT("口内"), TEXT("睫毛"), TEXT("まつ毛"), TEXT("まつげ"), TEXT("ハイライト") });
		if (!Match.IsEmpty())
		{
			Reason = TEXT("generic:eye_oral:") + Match;
			return EMMDMaterialRole::Generic;
		}
		Match = MatchRoleTerms(Names,
			{ TEXT("eye"), TEXT("eyes"), TEXT("eyeball"), TEXT("iris"), TEXT("pupil") },
			{ TEXT("白目"), TEXT("瞳"), TEXT("虹彩"), TEXT("眼球"), TEXT("眼睛") });
		if (!Match.IsEmpty())
		{
			Reason = TEXT("eye:") + Match;
			return EMMDMaterialRole::Eye;
		}
		// 金属配件先匹配，避免 HairClip、Collar_Ring、Jacket_Button 被归入头发或衣物。
		Match = MatchRoleTerms(Names,
			{ TEXT("metal"), TEXT("metallic"), TEXT("steel"), TEXT("silver"), TEXT("gold"), TEXT("iron"), TEXT("chrome"),
			  TEXT("armor"), TEXT("armour"), TEXT("ring"), TEXT("chain"), TEXT("zipper"), TEXT("buckle"), TEXT("button"),
			  TEXT("clip"), TEXT("hairclip"), TEXT("hairpin"), TEXT("collarring"), TEXT("jacketbutton") },
			{ TEXT("金属"), TEXT("金具"), TEXT("铁"), TEXT("钢"), TEXT("銅"), TEXT("铜"), TEXT("扣环"),
			  TEXT("发夹"), TEXT("髪留め"), TEXT("髪飾り") });
		if (!Match.IsEmpty())
		{
			Reason = TEXT("metal:") + Match;
			return EMMDMaterialRole::Metal;
		}
		// 半透明头发阴影片覆盖皮肤，不能套发丝的各向异性高光。
		Match = MatchRoleTerms(Names, { TEXT("hairshadow"), TEXT("hair shadow") },
			{ TEXT("髪影"), TEXT("头发阴影") });
		if (!Match.IsEmpty())
		{
			Reason = TEXT("generic:shadow_overlay:") + Match;
			return EMMDMaterialRole::Generic;
		}
		Match = MatchRoleTerms(Names,
			{ TEXT("hair"), TEXT("fringe"), TEXT("bangs"), TEXT("ponytail") },
			{ TEXT("髪"), TEXT("前髪"), TEXT("後髪"), TEXT("头发"), TEXT("头髮"), TEXT("髮") });
		if (!Match.IsEmpty())
		{
			Reason = TEXT("hair:") + Match;
			return EMMDMaterialRole::Hair;
		}
		Match = MatchRoleTerms(Names,
			{ TEXT("skin"), TEXT("face"), TEXT("facial"), TEXT("head") },
			{ TEXT("肌"), TEXT("顔"), TEXT("皮肤"), TEXT("脸"), TEXT("頬") });
		if (!Match.IsEmpty())
		{
			Reason = TEXT("skin:") + Match;
			return EMMDMaterialRole::Skin;
		}
		Match = MatchRoleTerms(Names,
			{ TEXT("cloth"), TEXT("clothes"), TEXT("clothing"), TEXT("fabric"), TEXT("shirt"), TEXT("jacket"), TEXT("pants"), TEXT("skirt"), TEXT("gloves"),
			  TEXT("boots"), TEXT("pocket"), TEXT("collar"), TEXT("bow"), TEXT("garters"), TEXT("ribbon"), TEXT("lace"), TEXT("sleeve") },
			{ TEXT("布"), TEXT("衣"), TEXT("服"), TEXT("スカート"), TEXT("靴"), TEXT("手袋"), TEXT("リボン") });
		if (!Match.IsEmpty())
		{
			Reason = TEXT("cloth:") + Match;
			return EMMDMaterialRole::Cloth;
		}
		Reason = TEXT("generic:unrecognized");
		return EMMDMaterialRole::Generic;
	}

	void RecordMaterialRole(UMMDStageMaterialState* State, int32 MaterialIndex, EMMDMaterialRole Role, const FString& Reason)
	{
		if (State->ResolvedRoles.Num() <= MaterialIndex) State->ResolvedRoles.SetNum(MaterialIndex + 1);
		if (State->RoleReasons.Num() <= MaterialIndex) State->RoleReasons.SetNum(MaterialIndex + 1);
		State->ResolvedRoles[MaterialIndex] = Role;
		State->RoleReasons[MaterialIndex] = Reason;
	}

	bool MMDStageIsAppliedFaceOverlay(USkeletalMeshComponent* Component, int32 MaterialIndex, const UMMDStageMaterialState* State)
	{
		if (!State || State->RoleOverrides.Contains(MaterialIndex) || !State->AppliedMaterials.IsValidIndex(MaterialIndex)
			|| State->AppliedMaterials[MaterialIndex] != Component->GetMaterial(MaterialIndex)
			|| !State->RoleReasons.IsValidIndex(MaterialIndex)
			|| !State->RoleReasons[MaterialIndex].StartsWith(TEXT("skin:attached_overlay:"))) return false;
		USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
		const UMMDEndfieldFaceData* Face = Mesh->GetAssetUserData<UMMDEndfieldFaceData>();
		const FMMDGeneratedFaceSdfSlot* Slot = Face ? Face->FindDirectionSlot(MaterialIndex, Mesh->GetMaterials()[MaterialIndex].MaterialSlotName) : nullptr;
		return Slot && Slot->bForwardOverlay;
	}

	void UpdateOwnedMaterialRole(USkeletalMeshComponent* Component, UMMDStageMaterialState* State, int32 MaterialIndex)
	{
		FString Reason;
		const EMMDMaterialRole Role = ResolveMaterialRole(Component, MaterialIndex, State, Reason);
		RecordMaterialRole(State, MaterialIndex, Role, Reason);
		if (State->AppliedMaterials.IsValidIndex(MaterialIndex) && Component->GetMaterial(MaterialIndex) == State->AppliedMaterials[MaterialIndex])
		{
			UMMDStageRenderPreset* Preset = State->AppliedPreset.Get();
			if (State->bAppliedWithReplacement && IsValid(Preset))
			{
				// 只重建仍由预设占用的当前槽，使用最初源输入，保留其他槽上的用户修改。
				if (MMDStageToonApplyMaterialSlot(Component, State, Preset, MaterialIndex, true)) return;
			}
			if (UMaterialInstanceDynamic* Material = Cast<UMaterialInstanceDynamic>(State->AppliedMaterials[MaterialIndex]))
				Material->SetScalarParameterValue(TEXT("MaterialRole"), static_cast<float>(Role));
		}
	}

	UMaterialInterface* ResolveParent(UMaterialInterface* Configured, EBlendMode BlendMode)
	{
		if (Configured) return Configured;
		const bool bTranslucent = BlendMode == BLEND_Translucent || BlendMode == BLEND_Additive || BlendMode == BLEND_AlphaComposite;
		return GetDefault<AMMDStageActor>()->GetDefaultCharacterMaterial(BlendMode == BLEND_Masked, bTranslucent);
	}

	UTexture* FindBaseTexture(UMaterialInterface* Material)
	{
		for (const TCHAR* Name : { TEXT("base_color_tex"), TEXT("BaseColorMap"), TEXT("BaseColorTexture"),
			TEXT("BaseColor"), TEXT("DiffuseTexture"), TEXT("DiffuseTex") })
		{
			UTexture* Texture = nullptr;
			if (Material->GetTextureParameterValue(FHashedMaterialParameterInfo(Name), Texture) && Texture) return Texture;
		}
		return nullptr;
	}

	void ReadSourceBaseAndOpacity(UMaterialInterface* Source, FLinearColor& Base, float& Opacity)
	{
		Base = FLinearColor::White;
		if (!Source->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("DiffuseColor")), Base))
			Source->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("BaseColor")), Base);
		Opacity = Base.A;
		Source->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("Opacity")), Opacity);
	}

	void CopyMaterialInputs(UMaterialInterface* Source, UMaterialInstanceDynamic* Target)
	{
		if (UMaterialInstance* SourceInstance = Cast<UMaterialInstance>(Source)) Target->CopyInterpParameters(SourceInstance);
		if (UTexture* Texture = FindBaseTexture(Source)) Target->SetTextureParameterValue(TEXT("base_color_tex"), Texture);
		FLinearColor Base;
		float Opacity;
		ReadSourceBaseAndOpacity(Source, Base, Opacity);
		Target->SetVectorParameterValue(TEXT("BaseColor"), Base);
		Target->SetScalarParameterValue(TEXT("Opacity"), FMath::Clamp(Opacity, 0.0f, 1.0f));
		UTexture* MatCap = nullptr;
		if (Source->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("mat_cap")), MatCap) && MatCap)
			Target->SetTextureParameterValue(TEXT("mat_cap"), MatCap);
		else if (Source->GetTextureParameterValue(FHashedMaterialParameterInfo(TEXT("SphereTex")), MatCap) && MatCap)
			Target->SetTextureParameterValue(TEXT("mat_cap"), MatCap);
		float SphereMode = 0.0f;
		Source->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("sphere_mode")), SphereMode);
		Target->SetScalarParameterValue(TEXT("sphere_mode"), SphereMode);
	}

	float MMDStageToonClampSetting(float Value, float DefaultValue, float MinValue, float MaxValue)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, MinValue, MaxValue) : DefaultValue;
	}

	void MMDStageToonApplyRoleParameters(UMaterialInstanceDynamic* Material, const FMMDStageToonRoleSettings& Settings)
	{
		Material->SetScalarParameterValue(TEXT("ToonRoughness"), MMDStageToonClampSetting(Settings.Roughness, 0.5f, 0.04f, 1.0f));
		Material->SetScalarParameterValue(TEXT("ToonSpecular"), MMDStageToonClampSetting(Settings.Specular, 0.5f, 0.0f, 1.0f));
		Material->SetScalarParameterValue(TEXT("ToonMetallic"), MMDStageToonClampSetting(Settings.Metallic, 0.0f, 0.0f, 1.0f));
		Material->SetScalarParameterValue(TEXT("ToonAnisotropy"), MMDStageToonClampSetting(Settings.Anisotropy, 0.0f, -1.0f, 1.0f));
		Material->SetScalarParameterValue(TEXT("ToonSphereBlend"), MMDStageToonClampSetting(Settings.SphereBlend, 0.0f, 0.0f, 1.0f));
		Material->SetScalarParameterValue(TEXT("ToonFaceShadowSuppress"), MMDStageToonClampSetting(Settings.FaceShadowSuppress, 0.0f, 0.0f, 1.0f));
	}

	void MMDStageToonApplySkinSurfaceParameters(UMaterialInstanceDynamic* Material, const TCHAR* Prefix,
		const FMMDStageSkinSurfaceSettings& Settings, bool bAllowed)
	{
		const FString ParameterPrefix(Prefix);
		auto Name = [&ParameterPrefix](const TCHAR* Suffix) { return FName(*(ParameterPrefix + Suffix)); };
		Material->SetScalarParameterValue(Name(TEXT("Enabled")), bAllowed && Settings.bEnabled ? 1.0f : 0.0f);
		const FLinearColor Tint(MMDStageToonClampSetting(Settings.Tint.R, 1.0f, 0.0f, 2.0f),
			MMDStageToonClampSetting(Settings.Tint.G, 1.0f, 0.0f, 2.0f),
			MMDStageToonClampSetting(Settings.Tint.B, 1.0f, 0.0f, 2.0f), 1.0f);
		// 只写独立 RGB 参数；Tint.A 不参与原材质的透明度计算。
		Material->SetVectorParameterValue(Name(TEXT("Tint")), Tint);
		Material->SetScalarParameterValue(Name(TEXT("TintStrength")), MMDStageToonClampSetting(Settings.TintStrength, 0.0f, 0.0f, 1.0f));
		Material->SetScalarParameterValue(Name(TEXT("AlbedoScale")), MMDStageToonClampSetting(Settings.AlbedoScale, 1.0f, 0.0f, 2.0f));
		Material->SetScalarParameterValue(Name(TEXT("Saturation")), MMDStageToonClampSetting(Settings.Saturation, 1.0f, 0.0f, 2.0f));
		Material->SetScalarParameterValue(Name(TEXT("Roughness")), MMDStageToonClampSetting(Settings.Roughness, 0.742f, 0.04f, 1.0f));
		Material->SetScalarParameterValue(Name(TEXT("Specular")), MMDStageToonClampSetting(Settings.Specular, 0.454f, 0.0f, 1.0f));
	}

	bool MMDStageToonApplyMaterialSlot(USkeletalMeshComponent* Component, UMMDStageMaterialState* State,
		UMMDStageRenderPreset* Preset, int32 MaterialIndex, bool bReplaceMaterials)
	{
		if (!State->SourceMaterials.IsValidIndex(MaterialIndex)) return false;
		UMaterialInterface* Source = State->SourceMaterials[MaterialIndex];
		if (!Source) return false;
		FString RoleReason;
		EMMDMaterialRole Role = ResolveMaterialRole(Component, MaterialIndex, State, RoleReason);
		EBlendMode Blend = Source->GetBlendMode();
		FLinearColor SourceBase;
		float SourceOpacity;
		ReadSourceBaseAndOpacity(Source, SourceBase, SourceOpacity);
		// 只改变本次预设的父材质，源透明属性和材质恢复快照保持原样。
		if (bReplaceMaterials && Preset->bUseMaskedHair && Blend == BLEND_Translucent && Role == EMMDMaterialRole::Hair
			&& FMath::IsFinite(SourceBase.A) && FMath::IsFinite(SourceOpacity) && SourceBase.A >= 0.999f && SourceOpacity >= 0.999f)
			Blend = BLEND_Masked;
		UMaterialInterface* Configured = Blend == BLEND_Masked ? Preset->CharacterMaterialMasked.Get()
			: Blend == BLEND_Translucent || Blend == BLEND_Additive || Blend == BLEND_AlphaComposite
			? Preset->CharacterMaterialTranslucent.Get() : Preset->CharacterMaterialOpaque.Get();
		UMaterialInterface* ToonConfigured = Blend == BLEND_Masked ? Preset->CharacterToonMaterialMasked.Get()
			: Blend == BLEND_Translucent || Blend == BLEND_Additive || Blend == BLEND_AlphaComposite
			? Preset->CharacterToonMaterialTranslucent.Get() : Preset->CharacterToonMaterialOpaque.Get();
		USkeletalMesh* MeshAsset = Component->GetSkeletalMeshAsset();
		const UMMDEndfieldFaceData* FaceData = MeshAsset->GetAssetUserData<UMMDEndfieldFaceData>();
		const FMMDGeneratedFaceSdfSlot* DirectionBinding = FaceData
			? FaceData->FindDirectionSlot(MaterialIndex, MeshAsset->GetMaterials()[MaterialIndex].MaterialSlotName) : nullptr;
		const auto& NativeBackend = MMDEndfieldNativeBackend::GetStatus();
		const bool bCanReplaceToon = bReplaceMaterials && Substrate::IsToonProfileEnabled();
		UMaterialInterface* FaceConfigured = Blend == BLEND_Opaque ? Preset->CharacterToonFaceMaterialOpaque.Get()
			: Blend == BLEND_Masked ? Preset->CharacterToonFaceMaterialMasked.Get()
			: Blend == BLEND_Translucent ? Preset->CharacterToonFaceMaterialTranslucent.Get() : nullptr;
		if (!IsValid(FaceConfigured) || FaceConfigured->GetBlendMode() != Blend) FaceConfigured = nullptr;
		UMaterialInterface* ForwardCandidate = FaceConfigured ? FaceConfigured : ToonConfigured;
		const bool bForwardFaceLayer = bCanReplaceToon && Blend == BLEND_Translucent && DirectionBinding && DirectionBinding->bForwardOverlay
			&& NativeBackend.bProfileMarkerAllowed && NativeBackend.Mode == 2
			&& IsValid(ForwardCandidate) && ForwardCandidate->GetPathName().StartsWith(TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/"))
			&& (Role == EMMDMaterialRole::Skin || (Role == EMMDMaterialRole::Generic && !State->RoleOverrides.Contains(MaterialIndex)));
		if (bForwardFaceLayer)
		{
			Role = EMMDMaterialRole::Skin;
			RoleReason = TEXT("skin:attached_overlay:") + DirectionBinding->HostSlotName.ToString();
		}
		const bool bFaceSurfaceBinding = DirectionBinding && Role == EMMDMaterialRole::Skin;
		if (bFaceSurfaceBinding && FaceConfigured) ToonConfigured = FaceConfigured;
		const FMMDStageToonRoleSettings* FaceSettings = Preset->CharacterToonRoles.Find(Role);
		UMaterialInterface* MaskedToon = Preset->CharacterToonMaterialMasked.Get();
		const bool bFaceMaskedAvailable = bFaceSurfaceBinding && IsValid(Preset->CharacterToonFaceMaterialMasked.Get())
			&& Preset->CharacterToonFaceMaterialMasked->GetBlendMode() == BLEND_Masked;
		if (bFaceMaskedAvailable)
			MaskedToon = Preset->CharacterToonFaceMaterialMasked.Get();
		if (bCanReplaceToon && Blend == BLEND_Opaque && Role == EMMDMaterialRole::Skin && FaceSettings
			&& FMath::IsFinite(FaceSettings->FaceShadowSuppress) && FaceSettings->FaceShadowSuppress > 0.0f && IsValid(MaskedToon)
			&& (IsValid(ToonConfigured) || bFaceMaskedAvailable))
		{
			USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
			const UMMDEndfieldFaceData* Face = Mesh->GetAssetUserData<UMMDEndfieldFaceData>();
			const auto& ShadowBackend = MMDEndfieldNativeBackend::GetStatus();
			if (Face && (Face->FindTexture(MaterialIndex, Mesh->GetMaterials()[MaterialIndex].MaterialSlotName)
				|| (ShadowBackend.bProfileMarkerAllowed && ShadowBackend.Mode == 2
					&& Face->FindDirectionTexture(MaterialIndex, Mesh->GetMaterials()[MaterialIndex].MaterialSlotName))))
				ToonConfigured = MaskedToon;
		}
		const bool bUseToon = bCanReplaceToon && IsValid(ToonConfigured);
		if (bUseToon) Configured = ToonConfigured;
		UMaterialInterface* Parent = bReplaceMaterials ? ResolveParent(Configured, Blend) : Source;
		if (!Parent) return false;
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(Parent, Component);
		if (!Material) return false;
		const FMMDStageToonRoleSettings DefaultToonSettings;
		const FMMDStageToonRoleSettings* RoleSettings = Preset->CharacterToonRoles.Find(Role);
		if (!RoleSettings) RoleSettings = Preset->CharacterToonRoles.Find(EMMDMaterialRole::Generic);
		const FMMDStageToonRoleSettings& ToonSettings = RoleSettings ? *RoleSettings : DefaultToonSettings;
		if (bUseToon && IsValid(ToonSettings.ToonProfile.Get()))
		{
			UToonProfile* Profile = ToonSettings.ToonProfile.Get();
			if (MMDEndfieldNativeBackend::GetStatus().bProfileMarkerAllowed
				&& Parent->GetPathName().StartsWith(TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/")))
			{
				Profile = MMDEndfieldNativeProfile::GetOrCreate(Profile, Role, &Preset->CharacterSkinLighting);
			}
			Material->ToonProfileOverride = Profile;
			Material->bOverrideToonProfile = true;
			// 新 MID 尚未复制输入；清空参数会初始化渲染代理并推送 Profile 覆盖。
			Material->ClearParameterValues();
		}
		CopyMaterialInputs(Source, Material);
		UMMDStageLibrary::ApplyPresetParameters(Material, Preset);
		if (bUseToon) MMDStageToonApplyRoleParameters(Material, ToonSettings);
		if (Parent->GetPathName().StartsWith(TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/")))
		{
			// 源 MID 可能带入旧开关；先中性重置，再让 shader 按脸部遮罩逐像素选择两组表面参数。
			Material->SetScalarParameterValue(TEXT("ToonSkinEnabled"), 0.0f);
			Material->SetScalarParameterValue(TEXT("ToonFaceSkinEnabled"), 0.0f);
			if (bUseToon && Role == EMMDMaterialRole::Skin)
			{
				MMDStageToonApplySkinSurfaceParameters(Material, TEXT("ToonSkin"), Preset->CharacterSkinSurface, true);
				MMDStageToonApplySkinSurfaceParameters(Material, TEXT("ToonFaceSkin"), Preset->CharacterFaceSkinSurface, bFaceSurfaceBinding);
			}
		}
		// 附着层可能带作者制作的反向阴影法线；高光/GI 使用头部代理，漫反射继承主脸阈值。
		if (bForwardFaceLayer) Material->SetScalarParameterValue(TEXT("ToonSpecularSphereBlend"), 1.0f);
		// 缺少派生数据或切换分类时显式关闭，避免复制源 MID 后残留脸部参数。
		Material->SetScalarParameterValue(TEXT("ToonFaceSdfEnabled"), 0.0f);
		Material->SetScalarParameterValue(TEXT("ToonFaceDirectionEnabled"), 0.0f);
		Material->SetScalarParameterValue(TEXT("ToonFaceDirectionVersion"), 0.0f);
		Material->SetScalarParameterValue(TEXT("ToonNativePayloadEnabled"), 0.0f);
		Material->SetScalarParameterValue(TEXT("UseSdfMap"), 0.0f);
		const IConsoleVariable* FormatVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Substrate.ProjectGBufferFormat"));
		const MMDEndfieldNativeBackend::FStatus& Backend = MMDEndfieldNativeBackend::GetStatus();
		const bool bNativePayload = bUseToon && Role == EMMDMaterialRole::Skin && Backend.bProfileMarkerAllowed
			&& Backend.Mode == 2 && FormatVariable && FormatVariable->GetInt() == 1
			&& Parent->GetPathName().StartsWith(TEXT("/Ue5MMDTools/Stages/Materials/EndfieldV2/"))
			&& (Configured->GetBlendMode() == BLEND_Opaque || Configured->GetBlendMode() == BLEND_Masked || bForwardFaceLayer);
		Material->SetScalarParameterValue(TEXT("ToonNativePayloadEnabled"), bNativePayload ? 1.0f : 0.0f);
		bool bFaceBound = false;
		if (bReplaceMaterials && Role == EMMDMaterialRole::Skin)
		{
			USkeletalMesh* Mesh = Component->GetSkeletalMeshAsset();
			if (const UMMDEndfieldFaceData* Face = Mesh->GetAssetUserData<UMMDEndfieldFaceData>())
			{
				if (bNativePayload)
				{
					if (UTexture2D* Direction = Face->FindDirectionTexture(MaterialIndex, Mesh->GetMaterials()[MaterialIndex].MaterialSlotName))
					{
						Material->SetTextureParameterValue(TEXT("ToonFaceDirectionTexture"), Direction);
						Material->SetScalarParameterValue(TEXT("ToonFaceDirectionEnabled"), 1.0f);
						Material->SetScalarParameterValue(TEXT("ToonFaceDirectionVersion"), static_cast<float>(Face->Version));
						bFaceBound = true;
					}
				}
				if (UTexture2D* Texture = Face->FindTexture(MaterialIndex, Mesh->GetMaterials()[MaterialIndex].MaterialSlotName))
				{
					Material->SetTextureParameterValue(bUseToon ? TEXT("ToonFaceSdfTexture") : TEXT("SDFMap"), Texture);
					Material->SetScalarParameterValue(bUseToon ? TEXT("ToonFaceSdfEnabled") : TEXT("UseSdfMap"), 1.0f);
					if (!bUseToon)
					{
						Material->SetScalarParameterValue(TEXT("SDFShadowCenter"), 0.0f);
						Material->SetScalarParameterValue(TEXT("SDFSharpness"), 1.0f);
						Material->SetScalarParameterValue(TEXT("FlipSDFThreshold"), 0.0f);
					}
					bFaceBound = true;
				}
			}
		}
		RecordMaterialRole(State, MaterialIndex, Role, RoleReason);
		Material->SetScalarParameterValue(TEXT("MaterialRole"), static_cast<float>(Role));
		Component->SetMaterial(MaterialIndex, Material);
		if (State->AppliedMaterials.Num() <= MaterialIndex) State->AppliedMaterials.SetNum(MaterialIndex + 1);
		State->AppliedMaterials[MaterialIndex] = Material;
		if (bUseToon || bFaceBound) UMMDHeadBoneSubsystem::UpdateComponentHeadFrame(Component);
		return true;
	}
}

void UMMDStageLibrary::ApplyPresetParameters(UMaterialInstanceDynamic* Material, const UMMDStageRenderPreset* Preset)
{
	if (!Material || !Preset) return;
	Material->SetScalarParameterValue(TEXT("ShadowThreshold"), FMath::Clamp(Preset->ShadowThreshold, 0.0f, 1.0f));
	Material->SetScalarParameterValue(TEXT("ShadowSoftness"), FMath::Clamp(Preset->ShadowSoftness, 0.001f, 1.0f));
	Material->SetVectorParameterValue(TEXT("ShadowTint"), Preset->ShadowTint);
	Material->SetScalarParameterValue(TEXT("AmbientStrength"), FMath::Clamp(Preset->AmbientStrength, 0.0f, 2.0f));
	Material->SetScalarParameterValue(TEXT("LightScale"), FMath::Clamp(Preset->LightScale, 0.0f, 4.0f));
	Material->SetScalarParameterValue(TEXT("SpecularStrength"), FMath::Clamp(Preset->SpecularStrength, 0.0f, 2.0f));
	Material->SetScalarParameterValue(TEXT("SpecularPower"), FMath::Clamp(Preset->SpecularPower, 1.0f, 256.0f));
	Material->SetScalarParameterValue(TEXT("RimStrength"), FMath::Clamp(Preset->RimStrength, 0.0f, 2.0f));
	Material->SetScalarParameterValue(TEXT("RimPower"), FMath::Clamp(Preset->RimPower, 0.1f, 16.0f));
	Material->SetScalarParameterValue(TEXT("ToonAmount"), FMath::Clamp(Preset->ToonAmount, 0.0f, 1.0f));
	Material->SetScalarParameterValue(TEXT("DiffuseWrap"), FMath::Clamp(Preset->DiffuseWrap, 0.0f, 1.0f));
	Material->SetScalarParameterValue(TEXT("ColorSaturation"), FMath::Clamp(Preset->ColorSaturation, 0.0f, 2.0f));
	Material->SetScalarParameterValue(TEXT("ShadowSaturation"), FMath::Clamp(Preset->ShadowSaturation, 0.0f, 2.0f));
	Material->SetScalarParameterValue(TEXT("Roughness"), FMath::Clamp(Preset->Roughness, 0.04f, 1.0f));
	Material->SetVectorParameterValue(TEXT("SpecularTint"), Preset->SpecularTint);
	Material->SetScalarParameterValue(TEXT("BlackLift"), FMath::Clamp(Preset->BlackLift, 0.0f, 0.10f));
	Material->SetScalarParameterValue(TEXT("SoftSpecularStrength"), FMath::Clamp(Preset->SoftSpecularStrength, 0.0f, 1.0f));
}

bool UMMDStageLibrary::ApplyCharacterRenderPreset(USkeletalMeshComponent* Component, UMMDStageRenderPreset* Preset,
	bool bReplaceMaterials)
{
	if (!IsValid(Component) || !Component->GetSkeletalMeshAsset() || !IsValid(Preset)) return false;
	UMMDStageMaterialState* State = GetCurrentMaterialState(Component, true);
	if (!State->bHasMaterialSnapshot)
	{
		// 单独设置分类时不提前冻结恢复快照，首次套材质才记录用户当前的覆盖。
		State->SourceOverrides = Component->OverrideMaterials;
		for (int32 Index = 0; Index < Component->GetNumMaterials(); ++Index) State->SourceMaterials.Add(Component->GetMaterial(Index));
		State->bHasMaterialSnapshot = true;
	}
	int32 AppliedCount = 0;
	for (int32 Index = 0; Index < State->SourceMaterials.Num(); ++Index)
	{
		if (MMDStageToonApplyMaterialSlot(Component, State, Preset, Index, bReplaceMaterials)) ++AppliedCount;
	}
	if (AppliedCount > 0)
	{
		State->AppliedPreset = Preset;
		State->bAppliedWithReplacement = bReplaceMaterials;
		if (!Preset->bCharacterContactShadow && !State->bHasContactShadowSnapshot)
		{
			State->bHadContactShadow = Component->bCastContactShadow;
			State->bHasContactShadowSnapshot = true;
		}
		if (!Preset->bCharacterContactShadow) Component->SetCastContactShadow(false);
		else if (State->bHasContactShadowSnapshot) Component->SetCastContactShadow(State->bHadContactShadow);
		UMMDOutlineLibrary::ApplyMMDOutlinePreset(Component, Preset);
		if (!State->bHasShadowTagSnapshot)
		{
			State->bHadShadowIncludeTag = Component->ComponentTags.Contains(CharacterShadowIncludeTag);
			State->bHadShadowExcludeTag = Component->ComponentTags.Contains(CharacterShadowExcludeTag);
			State->bHasShadowTagSnapshot = true;
		}
		if (Preset->bCharacterSelfShadow)
		{
			Component->ComponentTags.AddUnique(CharacterShadowIncludeTag);
			if (State->bAddedShadowExcludeTag) Component->ComponentTags.Remove(CharacterShadowExcludeTag);
			State->bAddedShadowExcludeTag = false;
		}
		else
		{
			Component->ComponentTags.Remove(CharacterShadowIncludeTag);
			// 普通 SkeletalMesh 所属 actor 没有默认隐藏，也需要显式关闭投影。
			if (!Component->ComponentTags.Contains(CharacterShadowExcludeTag))
			{
				Component->ComponentTags.Add(CharacterShadowExcludeTag);
				State->bAddedShadowExcludeTag = true;
			}
		}
	}
	return AppliedCount > 0;
}

bool UMMDStageLibrary::RestoreCharacterMaterials(USkeletalMeshComponent* Component)
{
	if (!IsValid(Component)) return false;
	UMMDStageMaterialState* State = Cast<UMMDStageMaterialState>(Component->GetAssetUserDataOfClass(UMMDStageMaterialState::StaticClass()));
	if (!State) return false;
	const bool bSameMesh = State->SourceMesh == Component->GetSkeletalMeshAsset();
	if (bSameMesh && State->bHasMaterialSnapshot)
	{
		Component->OverrideMaterials = State->SourceOverrides;
		Component->MarkRenderStateDirty();
	}
	else if (!bSameMesh) ClearOwnedOverridesAfterMeshChange(Component, State);
	RestoreCharacterShadowTag(Component, State);
	UMMDOutlineLibrary::RestoreMMDOutline(Component);
	Component->RemoveUserDataOfClass(UMMDStageMaterialState::StaticClass());
	return bSameMesh;
}

EMMDMaterialRole UMMDStageLibrary::GetResolvedCharacterMaterialRole(USkeletalMeshComponent* Component, int32 MaterialIndex)
{
	if (!IsValidMaterialSlot(Component, MaterialIndex)) return EMMDMaterialRole::Generic;
	FString Reason;
	const UMMDStageMaterialState* State = FindMaterialState(Component);
	const EMMDMaterialRole Role = ResolveMaterialRole(Component, MaterialIndex, State, Reason);
	return MMDStageIsAppliedFaceOverlay(Component, MaterialIndex, State) ? EMMDMaterialRole::Skin : Role;
}

FString UMMDStageLibrary::GetResolvedCharacterMaterialRoleReason(USkeletalMeshComponent* Component, int32 MaterialIndex)
{
	FString Reason;
	const UMMDStageMaterialState* State = FindMaterialState(Component);
	ResolveMaterialRole(Component, MaterialIndex, State, Reason);
	if (IsValidMaterialSlot(Component, MaterialIndex) && MMDStageIsAppliedFaceOverlay(Component, MaterialIndex, State))
		return State->RoleReasons[MaterialIndex];
	return Reason;
}

bool UMMDStageLibrary::SetCharacterMaterialRoleOverride(USkeletalMeshComponent* Component, int32 MaterialIndex, EMMDMaterialRole Role)
{
	if (!IsValidMaterialSlot(Component, MaterialIndex) || static_cast<uint8>(Role) > static_cast<uint8>(EMMDMaterialRole::Eye)) return false;
	UMMDStageMaterialState* State = GetCurrentMaterialState(Component, true);
	State->RoleOverrides.Add(MaterialIndex, Role);
	UpdateOwnedMaterialRole(Component, State, MaterialIndex);
	UMMDOutlineLibrary::RefreshMMDOutlinePreset(Component);
	return true;
}

bool UMMDStageLibrary::ClearCharacterMaterialRoleOverride(USkeletalMeshComponent* Component, int32 MaterialIndex)
{
	if (!IsValidMaterialSlot(Component, MaterialIndex)) return false;
	UMMDStageMaterialState* State = GetCurrentMaterialState(Component, false);
	if (!State || State->RoleOverrides.Remove(MaterialIndex) == 0) return false;
	UpdateOwnedMaterialRole(Component, State, MaterialIndex);
	UMMDOutlineLibrary::RefreshMMDOutlinePreset(Component);
	return true;
}
