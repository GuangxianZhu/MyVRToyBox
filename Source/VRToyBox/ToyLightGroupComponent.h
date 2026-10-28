#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "ToyLightGroupComponent.generated.h"

/**
 * 一整个模型共用一份阴影的"光照组根"：本身不画任何东西，bLightAttachmentsAsGroup 打开，挂在它下面的零件网格都算进这一组。
 *
 * 样板间的太阳是固定（Stationary）平行光，可移动的网格每个都要单独渲一张逐物体阴影（外加一张预阴影），投影时每张都要在屏幕上铺一遍。
 * 发动机这种几十上百个零件的模型，2700×3000 每眼时光阴影投影就要 3.7ms（模型藏起来 1.2ms），头显里 90 帧掉到 70~80。
 * 挂到这个组根下面以后整个模型只有一张，GPU 时间回到和不显示模型差不多。
 *
 * 引擎只在"被挂的那个组件"是网格时才通知它换组；零件单元的根是 SceneComponent，所以抓起 / 放回之后要调 RefreshLightingGroup。
 */
UCLASS(ClassGroup = ToyBox)
class VRTOYBOX_API UToyLightGroupComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UToyLightGroupComponent();

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

	/** Actor 换了挂点以后，让它身上的网格重新找自己属于哪个光照组（根组件本身是网格的不用调，引擎会做）。 */
	static void RefreshLightingGroup(AActor* Actor);
};
