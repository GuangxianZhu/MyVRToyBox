#include "ToyShapeComponent.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

UToyShapeComponent::UToyShapeComponent()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereFinder(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	// BasicShapeMaterial 带一个名为 "Color" 的向量参数，动态材质实例改它就能换色
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

	BoxMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	SphereMesh = SphereFinder.Object;
	BaseMaterial = MaterialFinder.Object;

	// 场景是运行时拼的、灯也全是可移动的，所以形状统一用 Movable，不依赖烘焙光照
	SetMobility(EComponentMobility::Movable);
	SetStaticMesh(BoxMesh);
}

void UToyShapeComponent::SetShape(EToyShape InShape, const FVector& SizeCm)
{
	switch (InShape)
	{
	case EToyShape::Box:      SetStaticMesh(BoxMesh); break;
	case EToyShape::Cylinder: SetStaticMesh(CylinderMesh); break;
	case EToyShape::Sphere:   SetStaticMesh(SphereMesh); break;
	}
	// 三个基础形状都是 100 单位大小、中心在原点
	SetRelativeScale3D(SizeCm / 100.f);
}

UToyShapeComponent* UToyShapeComponent::Create(AActor* Owner, FName Name, USceneComponent* Parent, EToyShape Shape,
	const FVector& SizeCm, const FVector& Location, const FLinearColor& InColor, const FRotator& Rotation)
{
	UToyShapeComponent* Comp = Owner->CreateDefaultSubobject<UToyShapeComponent>(Name);
	Comp->SetupAttachment(Parent);
	Comp->SetShape(Shape, SizeCm);
	Comp->SetRelativeLocationAndRotation(Location, Rotation);
	Comp->Color = InColor;
	return Comp;
}

void UToyShapeComponent::SetColor(const FLinearColor& InColor)
{
	Color = InColor;
	ApplyColor();
}

void UToyShapeComponent::OnRegister()
{
	Super::OnRegister();
	ApplyColor();
}

void UToyShapeComponent::ApplyColor()
{
	if (!ColorMaterial)
	{
		if (!BaseMaterial)
		{
			return;
		}
		ColorMaterial = CreateDynamicMaterialInstance(0, BaseMaterial);
	}
	if (ColorMaterial)
	{
		ColorMaterial->SetVectorParameterValue(TEXT("Color"), Color);
	}
}
