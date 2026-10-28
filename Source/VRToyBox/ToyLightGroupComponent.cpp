#include "ToyLightGroupComponent.h"

#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "PrimitiveSceneProxy.h"
#include "SceneInterface.h"

namespace ToyLightGroupLocal
{
	/** 什么都不画；只要让渲染器觉得"它投影、它看得见、它不透明"，整组的逐物体阴影和预阴影就挂在它身上建。 */
	class FToyLightGroupSceneProxy final : public FPrimitiveSceneProxy
	{
	public:
		explicit FToyLightGroupSceneProxy(const UPrimitiveComponent* Component)
			: FPrimitiveSceneProxy(Component)
		{
		}

		virtual SIZE_T GetTypeHash() const override
		{
			static size_t UniquePointer;
			return reinterpret_cast<size_t>(&UniquePointer);
		}

		virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
		{
			FPrimitiveViewRelevance Result;
			Result.bDrawRelevance = IsShown(View);
			Result.bShadowRelevance = IsShadowCast(View);
			// 渲染器只看组根的不透明标记来决定建不建不透明的逐物体阴影
			Result.bOpaque = true;
			return Result;
		}

		// 包围盒是模型轴心上的一个小球，会被模型自己挡住；被判成看不见的话预阴影就不渲了
		virtual bool CanBeOccluded() const override { return false; }

		virtual uint32 GetMemoryFootprint() const override { return sizeof(*this) + GetAllocatedSize(); }
	};
}

UToyLightGroupComponent::UToyLightGroupComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	Mobility = EComponentMobility::Movable;
	bLightAttachmentsAsGroup = true;
	CastShadow = true;
	bCastDynamicShadow = true;
	bUseAsOccluder = false;
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
}

FPrimitiveSceneProxy* UToyLightGroupComponent::CreateSceneProxy()
{
	return new ToyLightGroupLocal::FToyLightGroupSceneProxy(this);
}

FBoxSphereBounds UToyLightGroupComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// 整组阴影的范围 = 组根 + 每个投影零件的包围盒，组根只占一个点，不把范围撑大
	return FBoxSphereBounds(LocalToWorld.GetLocation(), FVector(1.f), 1.f);
}

void UToyLightGroupComponent::RefreshLightingGroup(AActor* Actor)
{
	UWorld* World = Actor ? Actor->GetWorld() : nullptr;
	if (!World || !World->Scene || Cast<UPrimitiveComponent>(Actor->GetRootComponent()))
	{
		return;
	}
	TInlineComponentArray<UPrimitiveComponent*> Primitives(Actor);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive->IsRenderStateCreated())
		{
			World->Scene->UpdatePrimitiveAttachment(Primitive);
		}
	}
}
