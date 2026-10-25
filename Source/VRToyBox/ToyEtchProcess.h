#pragma once

#include "ToyEtchCore.h"

#include <cstdint>
#include <vector>

/**
 * 干法刻蚀机跑一片晶圆的 15 步工艺（C++ 版），照 ITBasics 干法刻蚀机/web/app.js 的 STEPS、physicsTick、recordSample、
 * startOrNext / runStep / resetAll / finish 翻译。和 ToyEtchCore 一样只算数、不碰 UE 的类型，云端能用 g++ 单独编译、跑完 5 个预置配方对答案。
 *
 * 网页版每一步是一串 await：wait(秒数, 每帧回调) / until(条件) / 直接改状态。这里把每一步写成一张"段"的表（FSegment），
 * 时间到了进下一段。时序和网页一致：一段结束以后，下一段从"结束的那一刻"开始计时，但下一段的即时动作和第一次回调
 * 要等这一小步的物理算完才跑（网页里 await 后面的代码是在这一帧算完以后才执行的）。
 * 机械手在网页里有三维动画，这里只保留时长和"晶圆交到谁手里"（Holder），外加真空机械手进出工艺腔的伸缩量（Arm，桌上的小模型要画）。
 */
namespace ToyEtch
{
	/** 晶圆现在在哪（网页版 S.holder）：晶圆盒、大气机械手、对准器、负载锁、真空机械手、工艺腔（卡盘 / 升降针上） */
	enum class EHolder : uint8_t { Foup, AtmRobot, Aligner, LoadLock, VtmRobot, Chamber };

	/**
	 * 机台上能看到的量（网页版 S）。门 / 阀：0 = 关，1 = 开；负载锁压力 Torr，腔压 mTorr，流量 sccm，功率 W，卡盘电压 V，背氦 Torr，温度 °C。
	 * Plasma 0~1 是辉光强度；Oes 是这一刻光谱的原始信号（趋势图画的是滤波后的 E.OesF）。
	 */
	struct FToolState
	{
		EHolder Holder = EHolder::Foup;
		double FoupOpen = 0, LlOuter = 0, LlInner = 0, Slit = 0, PinH = 0, AlignRot = 0;
		/** 真空机械手往工艺腔里伸到哪：0 = 手指在闸阀外面，1 = 晶圆正对卡盘中心。bArm = 现在正在工艺腔门口伸缩（小模型上画出手指） */
		double Arm = 0;
		bool bArm = false;
		double LlP = 760, ChP = 0.02, Valve = 100, Cl2 = 0, HBr = 0, O2 = 0;
		double SrcF = 0, SrcR = 0, BiasF = 0, EscV = 0, He = 0, WaferT = 22;
		double Plasma = 0, Oes = 0;
		bool bEtching = false;
	};

	/** 趋势图的一个采样点（工艺时间每 0.2 秒一个）。T = 从开始算起的秒数，Oes 存滤波后的值 */
	struct FSample
	{
		double T, LlP, ChP, SrcF, SrcR, BiasF, Oes;
	};

	/** 跑完一片的结果：5 项判定 + 当时的配方、模型和刻蚀状态。下一片开始之前一直留着（结果板要显示） */
	struct FRunResult
	{
		FRecipe R;
		FModel M;
		FEtch E;
		FEvaluation V;
	};

	constexpr int StepCount = 15;
	/** 推进时每一小步最多多少秒工艺时间。标准答案也是 0.05 秒一步算的，头显帧率、倍速不同，结果都一样 */
	constexpr double MaxSubstep = 0.05;

	class FProcess;

	/** 一步里的一段：等多少秒（每一小步调 Tick(进度 0~1)）、等到条件成立、或者只是改一下状态（即时动作，只调 Begin） */
	struct FSegment
	{
		double Dur = 0;
		/** 时长要到这一段开始时才知道（过刻 = 主刻时间 × 过刻%） */
		double (*DurFn)(const FProcess&) = nullptr;
		void (*Begin)(FProcess&) = nullptr;
		void (*Tick)(FProcess&, double P) = nullptr;
		bool (*Until)(const FProcess&) = nullptr;
		/** 门 / 阀：从这一段开始时的值缓动到 Target（网页版 door()） */
		double FToolState::*Door = nullptr;
		double Target = 0;

		bool IsInstant() const { return Dur <= 0 && !DurFn && !Until && !Door; }
	};

	/** 网页版"开始 / 下一步 / 再跑一片"按钮按下去发生了什么 */
	enum class EStartResult : uint8_t { Busy, Started, Advanced, Reset, BadRecipe };

	/** 第 Index 步做完是不是自动接着下一步（主刻 → 过刻 → 关射频，不用再按） */
	bool StepChains(int Index);

	class FProcess
	{
	public:
		FProcess();

		FRecipe R;
		FModel M;
		FToolState S;
		FEtch E;

		/** 总的工艺时间（秒）。暂停时不走 */
		double SimT = 0;
		/** 正在做（或刚做完）第几步，-1 = 还没开始 */
		int StepIdx = -1;
		bool bBusy = false;
		bool bDone = false;
		/** 自动模式：一步做完 0.6 秒后自己开始下一步 */
		bool bAuto = false;
		/** 光谱原始信号的噪声（峰峰值）。网页版 0.02；自检用 0，结果才和标准答案一样 */
		double NoiseAmp = 0.02;

		std::vector<FSample> Samples;
		/** 工艺开始时的 SimT，< 0 = 还没开始 */
		double ProcT0 = -1;
		/** 判到终点时是整个工艺的第几秒（趋势图上画竖线），< 0 = 还没有 */
		double EpT = -1;

		bool bHasResult = false;
		FRunResult Result;

		/** 段里的回调存"这一段开始时"的值（关射频前的功率、停气前的腔压和摆阀开度） */
		double Snap[3] = {};

		/** 运行中配方锁住，改不了 */
		bool IsLocked() const { return StepIdx >= 0 && !bDone; }
		bool SetRecipe(const FRecipe& NewR);

		/** 网页版的"开始 / 下一步 / 再跑一片"按钮 */
		EStartResult StartOrNext();
		/** 自动模式。打开时如果正停在两步之间，马上开始下一步 */
		void SetAuto(bool bOn);
		/** 回到待机（网页版 resetAll）：新的一片在晶圆盒里，曲线清空。上一片的结果留着 */
		void Reset();
		/** 推进 Dt 秒工艺时间（内部按 MaxSubstep 切小步） */
		void Advance(double Dt);

		/** 通气稳压时摆阀关到多少 % */
		double ValveTarget() const;
		/** 过刻要刻多少秒（讲解文字里的 {oeSec}） */
		double OverEtchSeconds() const { return E.tMain * R.Oe / 100; }

	private:
		void Substep(double H);
		void RunTimers();
		void RunContinuations();
		void RunStep(int Index);
		void StartSegments();
		void OnStepDone();
		void PhysicsTick(double Dt);
		void RecordSample();
		double Random01();

		// 当前段：下标、开始时刻、时长、门的起始值；这一小步里到点了（bResolved）就等物理算完再进下一段
		int Seg = -1;
		double SegT0 = 0, SegDur = 0, SegFrom = 0;
		bool bSegActive = false;
		bool bResolved = false;
		// 一步做完到下一步之间的等待：连着的步 0.05 秒，自动模式 0.6 秒
		bool bGap = false;
		bool bGapResolved = false;
		int GapStep = -1;
		double GapT0 = 0, GapDur = 0;
		double LastSample = -1;
		uint32_t Seed = 20261025u;
	};

	/** 自动模式、0.05 秒一步、没有噪声，把一片从头跑到尾。跑完 15 步返回 true。自检和云端对答案用 */
	bool RunWholeProcess(FProcess& P, const FRecipe& R, double MaxSeconds = 1200);
}
