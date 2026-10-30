#include "ToyArchVizTools.h"

#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Subsystems/WorldSubsystem.h"
#include "StaticMeshResources.h"
#include "PhysicsEngine/BodySetup.h"
#if WITH_EDITOR
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#endif

namespace ToyArchVizLocal
{
	/** 凸包最多取这么多个顶点（均匀抽），够包住花瓶、花盆的外形 */
	constexpr int32 MaxHullPoints = 2000;
	/** 我们加的碰撞形状起这个名字，下次生成认得出来 */
	const FName OurShapeName(TEXT("ToyProp"));

	void Rebuild(UStaticMesh* Mesh)
	{
		UBodySetup* Body = Mesh->GetBodySetup();
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
		Mesh->MarkPackageDirty();
	}
}

int32 UToyArchVizTools::GetSimpleCollisionCount(UStaticMesh* Mesh)
{
	const UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
	return Body ? Body->AggGeom.GetElementCount() - Body->AggGeom.LevelSetElems.Num() - Body->AggGeom.SkinnedLevelSetElems.Num() : 0;
}

FString UToyArchVizTools::EnsurePropCollision(UStaticMesh* Mesh, bool bBox)
{
#if WITH_EDITOR
	using namespace ToyArchVizLocal;
	if (!Mesh)
	{
		return TEXT("没有网格");
	}
	if (!Mesh->GetBodySetup())
	{
		Mesh->CreateBodySetup();
	}
	UBodySetup* Body = Mesh->GetBodySetup();
	FKAggregateGeom& Geom = Body->AggGeom;
	// 上次生成加的（名字是 ToyProp）先去掉再重新加，反复跑结果一样
	auto Ours = [](const FKShapeElem& Elem) { return Elem.GetName() == OurShapeName; };
	const bool bHadOurs = Geom.BoxElems.ContainsByPredicate(Ours) || Geom.ConvexElems.ContainsByPredicate(Ours);
	Body->Modify();
	Geom.BoxElems.RemoveAll(Ours);
	Geom.ConvexElems.RemoveAll(Ours);
	if (GetSimpleCollisionCount(Mesh))
	{
		if (bHadOurs)
		{
			Rebuild(Mesh);
		}
		return FString::Printf(TEXT("网格自带简单碰撞（盒 %d、球 %d、胶囊 %d、凸包 %d）"), Geom.BoxElems.Num(), Geom.SphereElems.Num(),
			Geom.SphylElems.Num(), Geom.ConvexElems.Num());
	}
	Body->CollisionTraceFlag = CTF_UseDefault;
	// 用渲染网格（LOD0）的顶点：已经套过导入时的缩放（BuildScale3D），和看到的一样大；源网格（MeshDescription）没套
	TArray<FVector> Points;
	const FStaticMeshRenderData* RenderData = Mesh->GetRenderData();
	if (RenderData && RenderData->LODResources.Num() > 0)
	{
		const FPositionVertexBuffer& Buffer = RenderData->LODResources[0].VertexBuffers.PositionVertexBuffer;
		if (Buffer.GetVertexData())
		{
			for (uint32 I = 0; I < Buffer.GetNumVertices(); ++I)
			{
				Points.Add(FVector(Buffer.VertexPosition(I)));
			}
		}
	}
	if (Points.Num() == 0)
	{
		if (const FMeshDescription* Description = Mesh->GetMeshDescription(0))
		{
			const FVector Scale = Mesh->GetSourceModel(0).BuildSettings.BuildScale3D;
			const TVertexAttributesConstRef<FVector3f> Positions = FStaticMeshConstAttributes(*Description).GetVertexPositions();
			for (const FVertexID Vertex : Description->Vertices().GetElementIDs())
			{
				Points.Add(FVector(Positions[Vertex]) * Scale);
			}
		}
	}
	if (Points.Num() < 4)
	{
		Rebuild(Mesh);
		return TEXT("拿不到网格顶点，没加碰撞");
	}
	const int32 Total = Points.Num();
	const int32 Stride = FMath::Max(1, Total / MaxHullPoints);
	FKConvexElem Hull;
	FBox Bounds(ForceInit);
	for (int32 Index = 0; Index < Total; ++Index)
	{
		Bounds += Points[Index];
		if (Index % Stride == 0)
		{
			Hull.VertexData.Add(Points[Index]);
		}
	}
	const TCHAR* Again = bHadOurs ? TEXT("重新") : TEXT("");
	if (bBox)
	{
		FKBoxElem Box(Bounds.GetSize().X, Bounds.GetSize().Y, Bounds.GetSize().Z);
		Box.Center = Bounds.GetCenter();
		Box.SetName(OurShapeName);
		Geom.BoxElems.Add(Box);
		Rebuild(Mesh);
		return FString::Printf(TEXT("%s加了盒子碰撞 %.1f × %.1f × %.1f（网格自身尺寸，渲染包围盒 %.1f × %.1f × %.1f）"), Again, Bounds.GetSize().X, Bounds.GetSize().Y, Bounds.GetSize().Z, Mesh->GetBoundingBox().GetSize().X, Mesh->GetBoundingBox().GetSize().Y, Mesh->GetBoundingBox().GetSize().Z);
	}
	Hull.UpdateElemBox();
	Hull.SetName(OurShapeName);
	Body->AggGeom.ConvexElems.Add(Hull);
	Rebuild(Mesh);
	const int32 HullPoints = Body->AggGeom.ConvexElems.Last().VertexData.Num();
	return FString::Printf(TEXT("%s加了凸包碰撞（%d 个顶点里抽 %d 个，%.1f × %.1f × %.1f）"), Again, Total, HullPoints, Bounds.GetSize().X, Bounds.GetSize().Y, Bounds.GetSize().Z);
#else
	return FString();
#endif
}

bool UToyArchVizTools::UseComplexAsSimple(UStaticMesh* Mesh)
{
#if WITH_EDITOR
	UBodySetup* Body = Mesh ? Mesh->GetBodySetup() : nullptr;
	if (!Body || Body->CollisionTraceFlag == CTF_UseComplexAsSimple)
	{
		return false;
	}
	Body->Modify();
	Body->CollisionTraceFlag = CTF_UseComplexAsSimple;
	ToyArchVizLocal::Rebuild(Mesh);
	return true;
#else
	return false;
#endif
}

UWorldSubsystem* UToyArchVizTools::GetGPULightmassSubsystem(UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/GPULightmass.GPULightmassSubsystem"));
	return World && Class ? World->GetSubsystemBase(Class) : nullptr;
}
