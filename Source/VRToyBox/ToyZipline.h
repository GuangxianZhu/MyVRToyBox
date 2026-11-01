#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ToyGrabbable.h"
#include "ToyProp.h"
#include "ToyZipline.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class AToyZipPulley;

/**
 * 小人国的滑索：两头钉在家具 / 墙上的一根细绳，高的那头挂着一个滑轮把手。
 *
 * 小身体走到高的那头，捏住把手（扳机）就吊在上面往低的那头滑（AToyBoxPawn::StartZipRide）：沿绳子方向的重力分量加速，
 * 有阻力和最高速度（都按比例缩，小人眼里是正常的快）；松开扳机就带着当时的速度掉下去；滑到底轻轻停住，放到底下的台面上，
 * 把手自己回到上面。
 *
 * 大身体用线轴（AToyZipSpool，菜单 OBJECTS 的 SPOOL，工作台上也有一个）拉：贴着一个表面松手钉住第一头，
 * 再拿起来贴着另一个表面松手钉住第二头。两头之间不能穿过家具，最长 3 米，两头至少差 5cm 高（不然是平的，滑不动）。
 * 最多 MaxLines 根，再拉就拿掉最早的；菜单 CLEAR ALL 也清掉滑索。样板间开局有一根预设的（工作台面 → 地上）。
 */
UCLASS()
class VRTOYBOX_API AToyZipline : public AActor
{
	GENERATED_BODY()

public:
	AToyZipline();

	static constexpr int32 MaxLines = 5;

	/**
	 * 在两个钉点之间拉一根。钉点是表面上的点和表面法线：朝上的表面（台面、地板）立一根柱子，绳子挂在柱顶；墙上钉一个短桩。
	 * 不合格（穿过家具、太长、太平）返回空，OutReason 是给人看的原因（英文，TextRender 没有中文字形）。
	 */
	static AToyZipline* Create(UWorld* World, const FVector& PointA, const FVector& NormalA, const FVector& PointB, const FVector& NormalB,
		FString& OutReason, bool bPreset = false);

	/** 全部删掉（预设的那根也删），返回删了几根。 */
	static int32 ClearAll(UWorld* World);

	/** 开局的预设滑索：工作台面上靠里的地方 → 地上，用射线挑一条合格的。已经有了就不再建。 */
	static AToyZipline* CreatePreset(UWorld* World);

	/** 最近建的一根（toy.Zip ride 用）。 */
	static AToyZipline* FindLatest(UWorld* World);

	/** 钉点（表面上的点 + 法线）→ 绳子挂的点（柱顶 / 桩头）。 */
	static FVector AttachPoint(const FVector& Point, const FVector& Normal);

	/** 绳子上离高的那头 Distance 厘米的点。 */
	FVector PointAt(float Distance) const { return Top + Direction * FMath::Clamp(Distance, 0.f, Length); }

	FVector GetTop() const { return Top; }
	FVector GetBottom() const { return Bottom; }
	FVector GetDirection() const { return Direction; }
	float GetLength() const { return Length; }
	AToyZipPulley* GetPulley() const { return Pulley; }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> Rope;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> PostTop;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> PostBottom;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void Build(const FVector& PointA, const FVector& NormalA, const FVector& PointB, const FVector& NormalB);

	FVector Top = FVector::ZeroVector;
	FVector Bottom = FVector::ZeroVector;
	FVector Direction = FVector::ForwardVector;
	float Length = 0.f;
	int64 Serial = 0;
	bool bPreset = false;

	UPROPERTY(Transient)
	TObjectPtr<AToyZipPulley> Pulley;
};

/**
 * 滑索上的滑轮把手。小身体捏住就开始滑（手不把它挂到手上，而是按"约束抓取"每帧报告捏合点，把手自己沿绳子走、人跟着把手走）。
 * 大身体不能抓（大人一拉，把手就飞了）。
 */
UCLASS()
class VRTOYBOX_API AToyZipPulley : public AActor, public IToyGrabbable
{
	GENERATED_BODY()

public:
	AToyZipPulley();

	void Init(AToyZipline* InLine);

	/** 把手挂在绳子上离高的那头 Distance 厘米的地方。 */
	void SetDistance(float Distance);
	float GetDistance() const { return Distance; }

	/** 手捏的那一点（把手横杆的中间）。 */
	FVector GetGripLocation() const;

	AToyZipline* GetLine() const { return Line.Get(); }

	virtual bool IsConstrained() const override { return true; }
	virtual void UpdateConstrainedGrab(const FVector& HandLocation) override {}
	virtual bool CanGrab() const override;
	virtual FTransform ComputeHoldTransform(const FToyGripContext& Grip) const override { return FTransform::Identity; }
	virtual void OnGrabbed(AToyHand* Hand) override;
	virtual void OnReleased(AToyHand* Hand, const FVector& Velocity) override;
	virtual AToyHand* GetHolder() const override { return Holder.Get(); }
	virtual void AddHover() override;
	virtual void RemoveHover() override;
	virtual EToyGripStyle GetGripStyle() const override { return EToyGripStyle::Power; }
	virtual bool GetGrabInfo(FText& OutTitle, FText& OutBody) const override;
	virtual FString GetMaterialText() const override { return TEXT("塑料"); }

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> Wheel;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> Rod;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> Bar;

	/** 手捏的检测盒（WorldDynamic、只查询，不挡小身体）。 */
	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UBoxComponent> GrabZone;

private:
	TWeakObjectPtr<AToyZipline> Line;
	TWeakObjectPtr<AToyHand> Holder;
	float Distance = 0.f;
	int32 HoverCount = 0;
};

/**
 * 拉滑索的线轴（大身体用）。贴着一个表面松手 → 钉住第一头（线轴留在那里当桩，亮着），再拿起来，绳子从第一头连到手里的线轴；
 * 贴着另一个表面松手 → 拉成一根滑索。不合格就提示原因，第一头留着，可以换个地方再钉。
 * 菜单摆出来的线轴用完就消失；工作台上那个用完回到工作台上。
 */
UCLASS()
class VRTOYBOX_API AToyZipSpool : public AToyProp
{
	GENERATED_BODY()

public:
	AToyZipSpool();

	/** 菜单 OBJECTS 的 SPOOL：悬在面前右侧，捏起来以后才有重力。 */
	static AToyZipSpool* SpawnInFrontOfPlayer(UWorld* World);

	/** 开局摆在工作台上的那个（用完回到这里）。 */
	static AToyZipSpool* SpawnOnWorkbench(UWorld* World);

	virtual void Tick(float DeltaSeconds) override;
	virtual void OnGrabbed(AToyHand* Hand) override;
	virtual void OnReleased(AToyHand* Hand, const FVector& Velocity) override;
	virtual bool GetGrabInfo(FText& OutTitle, FText& OutBody) const override;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> PreviewRope;

	UPROPERTY(VisibleAnywhere, Category = "ToyBox")
	TObjectPtr<UStaticMeshComponent> PinMarker;

protected:
	virtual void BeginPlay() override;

private:
	/** 线轴附近 SpoolReach 以内最近的家具 / 墙表面（6 个方向打射线），没有返回 false。 */
	bool FindSurface(FVector& OutPoint, FVector& OutNormal) const;
	void ShowToastToPlayer(const FString& Title, const FString& Body) const;
	void Finish();

	bool bHasPin = false;
	bool bFloating = false;
	bool bReturnHome = false;
	FVector PinPoint = FVector::ZeroVector;
	FVector PinNormal = FVector::UpVector;
};
