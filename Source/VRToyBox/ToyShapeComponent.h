#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "ToyShapeComponent.generated.h"

class UMaterialInstanceDynamic;

UENUM()
enum class EToyShape : uint8
{
	Box,
	Cylinder,
	Sphere
};

/**
 * 引擎自带的基础形状（/Engine/BasicShapes）+ 一个可调颜色。
 * 整个场景都用它拼出来，所以仓库里不需要任何 .uasset，全部是可以 diff 的文本。
 */
UCLASS(ClassGroup = (ToyBox), meta = (BlueprintSpawnableComponent))
class VRTOYBOX_API UToyShapeComponent : public UStaticMeshComponent
{
	GENERATED_BODY()

public:
	UToyShapeComponent();

	/** 尺寸单位是厘米：Box = 长宽高，Cylinder = (直径, 直径, 高)，Sphere = (直径, 直径, 直径)。 */
	void SetShape(EToyShape InShape, const FVector& SizeCm);

	UFUNCTION(BlueprintCallable, Category = "ToyBox")
	void SetColor(const FLinearColor& InColor);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ToyBox")
	FLinearColor Color = FLinearColor::White;

	/** 在某个 Actor 的构造函数里一行建出一个带颜色的形状。 */
	static UToyShapeComponent* Create(AActor* Owner, FName Name, USceneComponent* Parent, EToyShape Shape,
		const FVector& SizeCm, const FVector& Location, const FLinearColor& InColor, const FRotator& Rotation);

protected:
	virtual void OnRegister() override;

private:
	void ApplyColor();

	UPROPERTY()
	TObjectPtr<UStaticMesh> BoxMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CylinderMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> ColorMaterial;
};
