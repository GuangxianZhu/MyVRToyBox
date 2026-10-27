#include "ToyEtchProcess.h"

#include <algorithm>
#include <cmath>

// 每一步的段表照 app.js 的 STEPS 逐行写；数字（秒数、电压、气压）都和网页一样

namespace
{
	using namespace ToyEtch;

	constexpr double Pi = 3.14159265358979323846;

	double Ease(double P) { return P < 0.5 ? 2 * P * P : 1 - std::pow(-2 * P + 2, 2) / 2; }
	double Lerp(double A, double B, double T) { return A + (B - A) * T; }

	FSegment Wait(double Dur, void (*Tick)(FProcess&, double) = nullptr, void (*Begin)(FProcess&) = nullptr)
	{
		FSegment G;
		G.Dur = Dur;
		G.Tick = Tick;
		G.Begin = Begin;
		return G;
	}

	FSegment Door(double FToolState::*Key, double Target, double Dur = 0.8)
	{
		FSegment G;
		G.Door = Key;
		G.Target = Target;
		G.Dur = Dur;
		return G;
	}

	FSegment Act(void (*Begin)(FProcess&))
	{
		FSegment G;
		G.Begin = Begin;
		return G;
	}

	FSegment Until(bool (*Cond)(const FProcess&))
	{
		FSegment G;
		G.Until = Cond;
		return G;
	}

	template <EHolder H>
	void SetHolder(FProcess& P) { P.S.Holder = H; }

	// 网页版 pick() / place()：转过去、伸手、抬 / 放（这时晶圆换手）、缩回，四段机械手动作
	template <EHolder Robot>
	void Pick(std::vector<FSegment>& Out)
	{
		Out.push_back(Wait(0.8));
		Out.push_back(Wait(1.0));
		Out.push_back(Wait(0.4));
		Out.push_back(Act(&SetHolder<Robot>));
		Out.push_back(Wait(1.0));
	}

	template <EHolder Station>
	void Place(std::vector<FSegment>& Out)
	{
		Out.push_back(Wait(0.8));
		Out.push_back(Wait(1.0));
		Out.push_back(Wait(0.4));
		Out.push_back(Act(&SetHolder<Station>));
		Out.push_back(Wait(1.0));
	}

	// 真空机械手进出工艺腔：转向闸阀、伸进去（Arm 0→1）……缩回来（Arm 1→0）
	FSegment ArmIn()
	{
		return Wait(1.4, [](FProcess& P, double p) { P.S.Arm = Ease(p); }, [](FProcess& P) { P.S.bArm = true; P.S.Arm = 0; });
	}

	FSegment ArmOut()
	{
		return Wait(1.2, [](FProcess& P, double p) { P.S.Arm = 1 - Ease(p); });
	}

	std::vector<std::vector<FSegment>> BuildSteps()
	{
		std::vector<std::vector<FSegment>> Steps(StepCount);
		using T = FToolState;

		// 1 打开晶圆盒
		Steps[0].push_back(Door(&T::FoupOpen, 1, 1.6));

		// 2 取片、对准：大气机械手从盒里取片放到对准器上，转一圈找缺口
		Pick<EHolder::AtmRobot>(Steps[1]);
		Place<EHolder::Aligner>(Steps[1]);
		Steps[1].push_back(Wait(2, [](FProcess& P, double p) { P.S.AlignRot = Ease(p) * Pi * 2; }));
		Steps[1].push_back(Act([](FProcess& P) { P.S.AlignRot = 0; }));

		// 3 放进负载锁
		Steps[2].push_back(Door(&T::LlOuter, 1));
		Pick<EHolder::AtmRobot>(Steps[2]);
		Place<EHolder::LoadLock>(Steps[2]);
		Steps[2].push_back(Wait(0.8));
		Steps[2].push_back(Door(&T::LlOuter, 0));

		// 4 负载锁抽真空：760 → 约 0.1 Torr
		Steps[3].push_back(Wait(10, [](FProcess& P, double p) { P.S.LlP = 760 * std::exp(-9 * p); }));

		// 5 送进工艺腔：真空机械手取片，开闸阀伸进去，升降针接过晶圆，缩回、关闸阀，针降下
		Steps[4].push_back(Door(&T::LlInner, 1));
		Pick<EHolder::VtmRobot>(Steps[4]);
		Steps[4].push_back(Door(&T::LlInner, 0));
		Steps[4].push_back(Door(&T::Slit, 1));
		Steps[4].push_back(Wait(0.8));
		Steps[4].push_back(ArmIn());
		Steps[4].push_back(Wait(0.8, [](FProcess& P, double p)
		{
			P.S.PinH = p;
			if (p > 0.85 && P.S.Holder == EHolder::VtmRobot) P.S.Holder = EHolder::Chamber;
		}));
		Steps[4].push_back(ArmOut());
		Steps[4].push_back(Act([](FProcess& P) { P.S.bArm = false; }));
		Steps[4].push_back(Door(&T::Slit, 0));
		Steps[4].push_back(Wait(0.9, [](FProcess& P, double p) { P.S.PinH = 1 - p; }));

		// 6 吸附晶圆、通背氦
		Steps[5].push_back(Wait(1.2, [](FProcess& P, double p) { P.S.EscV = 1200 * Ease(p); }));
		Steps[5].push_back(Wait(1.8, [](FProcess& P, double p) { P.S.He = 10 * Ease(p); }));

		// 7 通气、稳压
		Steps[6].push_back(Wait(4, [](FProcess& P, double p)
		{
			const double e = Ease(std::min(1.0, p * 2));
			P.S.Cl2 = P.R.Cl2 * e;
			P.S.HBr = P.R.HBr * e;
			P.S.O2 = P.R.O2 * e;
			P.S.Valve = Lerp(100, P.ValveTarget(), Ease(p));
			P.S.ChP = Lerp(0.02, P.R.P, 1 - std::exp(-5 * p)) + P.R.P * 0.08 * std::sin(p * Pi) * (1 - p);
		}));

		// 8 起辉
		Steps[7].push_back(Wait(0.6, [](FProcess& P, double p)
		{
			P.S.SrcF = P.R.Ws * p;
			P.S.SrcR = P.S.SrcF * 0.35;
			P.S.Plasma = p * 0.7;
		}));
		Steps[7].push_back(Wait(2.2, [](FProcess& P, double p)
		{
			P.S.SrcR = P.R.Ws * (0.35 * std::exp(-5 * p) + 0.01);
			P.S.Plasma = 0.7 + 0.3 * p;
		}));

		// 9 主刻蚀：加偏压，刻到光谱判出终点（或者超时）
		Steps[8].push_back(Wait(0.5, [](FProcess& P, double p) { P.S.BiasF = P.R.Wb * p; }));
		Steps[8].push_back(Act([](FProcess& P) { P.S.bEtching = true; }));
		Steps[8].push_back(Until([](const FProcess& P) { return P.E.bHasEndpoint || P.E.t > MainTimeout; }));
		Steps[8].push_back(Act([](FProcess& P)
		{
			if (!P.E.bHasEndpoint) P.E.bTimeout = true;
			P.E.tMain = P.E.t;
		}));

		// 10 过刻蚀：再刻主刻时间的 oe%
		{
			FSegment Over;
			Over.DurFn = [](const FProcess& P) { return P.E.bTimeout ? 0.01 : P.E.tMain * P.R.Oe / 100; };
			Steps[9].push_back(Over);
		}

		// 11 关射频、停气
		Steps[10].push_back(Act([](FProcess& P)
		{
			P.S.bEtching = false;
			P.Snap[0] = P.S.SrcF;
			P.Snap[1] = P.S.BiasF;
			P.Snap[2] = P.S.SrcR;
		}));
		Steps[10].push_back(Wait(0.8, [](FProcess& P, double p)
		{
			P.S.BiasF = P.Snap[1] * (1 - p);
			P.S.SrcF = P.Snap[0] * (1 - p);
			P.S.SrcR = P.Snap[2] * (1 - p);
			P.S.Plasma = 1 - p;
		}));
		Steps[10].push_back(Wait(2.5, [](FProcess& P, double p)
		{
			const double e = Ease(std::min(1.0, p * 2));
			P.S.Cl2 = P.R.Cl2 * (1 - e);
			P.S.HBr = P.R.HBr * (1 - e);
			P.S.O2 = P.R.O2 * (1 - e);
			P.S.Valve = Lerp(P.Snap[1], 100, e);
			P.S.ChP = Lerp(P.Snap[0], 0.02, 1 - std::exp(-6 * p));
		}, [](FProcess& P)
		{
			P.Snap[0] = P.S.ChP;
			P.Snap[1] = P.S.Valve;
		}));

		// 12 去吸附
		Steps[11].push_back(Wait(1, [](FProcess& P, double p) { P.S.He = 10 * (1 - p); }));
		Steps[11].push_back(Wait(1.4, [](FProcess& P, double p) { P.S.EscV = 1200 * (1 - Ease(p)); }));

		// 13 传出到负载锁
		Steps[12].push_back(Wait(0.8, [](FProcess& P, double p) { P.S.PinH = p; }));
		Steps[12].push_back(Door(&T::Slit, 1));
		Steps[12].push_back(Wait(0.8));
		Steps[12].push_back(ArmIn());
		Steps[12].push_back(Wait(0.8, [](FProcess& P, double p)
		{
			P.S.PinH = 1 - p;
			if (p > 0.15 && P.S.Holder == EHolder::Chamber) P.S.Holder = EHolder::VtmRobot;
		}));
		Steps[12].push_back(ArmOut());
		Steps[12].push_back(Act([](FProcess& P) { P.S.bArm = false; }));
		Steps[12].push_back(Door(&T::Slit, 0));
		Steps[12].push_back(Door(&T::LlInner, 1));
		Place<EHolder::LoadLock>(Steps[12]);
		Steps[12].push_back(Door(&T::LlInner, 0));
		Steps[12].push_back(Wait(0.8));

		// 14 负载锁回大气：0.1 → 760 Torr
		Steps[13].push_back(Wait(6, [](FProcess& P, double p) { P.S.LlP = 0.1 * std::pow(7600.0, p); }));

		// 15 放回晶圆盒
		Steps[14].push_back(Door(&T::LlOuter, 1));
		Pick<EHolder::AtmRobot>(Steps[14]);
		Steps[14].push_back(Door(&T::LlOuter, 0, 0.6));
		Place<EHolder::Foup>(Steps[14]);
		Steps[14].push_back(Wait(0.6));
		Steps[14].push_back(Door(&T::FoupOpen, 0, 1.4));
		return Steps;
	}

	const std::vector<FSegment>& StepProgram(int Index)
	{
		static const std::vector<std::vector<FSegment>> Steps = BuildSteps();
		return Steps[std::clamp(Index, 0, StepCount - 1)];
	}
}

namespace ToyEtch
{
	bool StepChains(int Index)
	{
		return Index == 8 || Index == 9;
	}

	FProcess::FProcess()
	{
		M = Model(R);
	}

	bool FProcess::SetRecipe(const FRecipe& NewR)
	{
		if (IsLocked())
		{
			return false;
		}
		R = NewR;
		M = Model(R);
		return true;
	}

	double FProcess::ValveTarget() const
	{
		return std::clamp(100 * M.total / (M.total + 18 * R.P), 5.0, 95.0);
	}

	EStartResult FProcess::StartOrNext()
	{
		if (bBusy)
		{
			return EStartResult::Busy;
		}
		if (bDone)
		{
			Reset();
			return EStartResult::Reset;
		}
		const bool bFirst = StepIdx < 0;
		if (bFirst)
		{
			M = Model(R);
			if (M.Problem != EProblem::None)
			{
				return EStartResult::BadRecipe;
			}
			ProcT0 = SimT;
			Samples.clear();
			LastSample = -1;
			bHasResult = false;
		}
		bGap = false;
		bGapResolved = false;
		RunStep(StepIdx + 1);
		return bFirst ? EStartResult::Started : EStartResult::Advanced;
	}

	void FProcess::SetAuto(bool bOn)
	{
		bAuto = bOn;
		if (bOn && !bBusy && StepIdx >= 0 && !bDone)
		{
			bGap = false;
			bGapResolved = false;
			RunStep(StepIdx + 1);
		}
	}

	void FProcess::Reset()
	{
		S = FToolState();
		E = FEtch();
		M = Model(R);
		StepIdx = -1;
		bBusy = false;
		bDone = false;
		ProcT0 = -1;
		EpT = -1;
		Samples.clear();
		LastSample = -1;
		Seg = -1;
		bSegActive = false;
		bResolved = false;
		bGap = false;
		bGapResolved = false;
	}

	void FProcess::Advance(double Dt)
	{
		if (Dt <= 0)
		{
			return;
		}
		const int Count = std::max(1, static_cast<int>(std::ceil(Dt / MaxSubstep - 1e-9)));
		const double H = Dt / Count;
		for (int i = 0; i < Count; i++)
		{
			Substep(H);
		}
	}

	void FProcess::Substep(double H)
	{
		// 网页版一帧：simT += dt → runTimers() → physicsTick(dt) → recordSample()，然后 await 的后续代码才跑
		SimT += H;
		RunTimers();
		PhysicsTick(H);
		RecordSample();
		RunContinuations();
	}

	void FProcess::RunTimers()
	{
		if (bSegActive)
		{
			const FSegment& G = StepProgram(StepIdx)[Seg];
			if (G.Until)
			{
				bResolved = G.Until(*this);
			}
			else
			{
				const double P = std::clamp((SimT - SegT0) / SegDur, 0.0, 1.0);
				if (G.Door) S.*(G.Door) = Lerp(SegFrom, G.Target, Ease(P));
				if (G.Tick) G.Tick(*this, P);
				bResolved = P >= 1;
			}
		}
		if (bGap && SimT - GapT0 >= GapDur)
		{
			bGapResolved = true;
		}
	}

	void FProcess::RunContinuations()
	{
		if (bResolved)
		{
			bResolved = false;
			bSegActive = false;
			StartSegments();
		}
		if (bGapResolved)
		{
			bGapResolved = false;
			bGap = false;
			if (!bBusy && !bDone && StepIdx == GapStep && (StepChains(GapStep) || bAuto))
			{
				RunStep(GapStep + 1);
			}
		}
	}

	void FProcess::RunStep(int Index)
	{
		bBusy = true;
		StepIdx = Index;
		Seg = -1;
		StartSegments();
	}

	void FProcess::StartSegments()
	{
		// 从下一段开始往后走：即时动作直接做掉，遇到要等的段就开始计时
		const std::vector<FSegment>& Program = StepProgram(StepIdx);
		while (++Seg < static_cast<int>(Program.size()))
		{
			const FSegment& G = Program[Seg];
			if (G.Begin) G.Begin(*this);
			if (G.IsInstant()) continue;
			bSegActive = true;
			SegT0 = SimT;
			SegDur = std::max(G.DurFn ? G.DurFn(*this) : G.Dur, 1e-3);
			SegFrom = G.Door ? S.*(G.Door) : 0;
			return;
		}
		bSegActive = false;
		OnStepDone();
	}

	void FProcess::OnStepDone()
	{
		bBusy = false;
		if (StepIdx == StepCount - 1)
		{
			bDone = true;
			Result.R = R;
			Result.M = M;
			Result.E = E;
			Result.V = Evaluate(E);
			bHasResult = true;
			return;
		}
		if (StepChains(StepIdx) || bAuto)
		{
			bGap = true;
			GapStep = StepIdx;
			GapT0 = SimT;
			GapDur = StepChains(StepIdx) ? 0.05 : 0.6;
		}
	}

	void FProcess::PhysicsTick(double Dt)
	{
		if (S.bEtching) EtchTick(E, M, Dt);
		// 光谱：等离子体本身有一点底光；刻多晶硅时含硅产物的谱线和刻蚀速率成正比（判终点也在里面）
		const bool bHadEndpoint = E.bHasEndpoint;
		const double Noise = NoiseAmp > 0 ? (Random01() - 0.5) * NoiseAmp : 0;
		S.Oes = OesTick(E, M, Dt, S.Plasma, S.bEtching, Noise);
		if (!bHadEndpoint && E.bHasEndpoint) EpT = SimT - ProcT0;
		// 晶圆温度：有背氦时贴着卡盘温度，等离子体会再加热一点
		if (S.Holder == EHolder::Chamber && S.He > 5) S.WaferT += (R.T + S.Plasma * R.Ws / 300 - S.WaferT) * std::min(1.0, Dt / 1.5);
		else if (S.Holder == EHolder::Chamber && S.Plasma > 0) S.WaferT += Dt * 2;
		else S.WaferT += (22 - S.WaferT) * std::min(1.0, Dt / 20);
	}

	void FProcess::RecordSample()
	{
		if (ProcT0 < 0 || SimT - LastSample < 0.2)
		{
			return;
		}
		LastSample = SimT;
		Samples.push_back(FSample{ SimT - ProcT0, S.LlP, S.ChP, S.SrcF, S.SrcR, S.BiasF, E.OesF });
		if (Samples.size() > 3000)
		{
			Samples.erase(Samples.begin());
		}
	}

	double FProcess::Random01()
	{
		// 线性同余，够做光谱噪声；不用 UE 的随机数，云端 g++ 也能编
		Seed = Seed * 1664525u + 1013904223u;
		return (Seed >> 8) / 16777216.0;
	}

	bool RunWholeProcess(FProcess& P, const FRecipe& R, double MaxSeconds)
	{
		P.NoiseAmp = 0;
		P.Reset();
		P.SetRecipe(R);
		P.bAuto = true;
		if (P.StartOrNext() != EStartResult::Started)
		{
			return false;
		}
		const double T0 = P.SimT;
		while (!P.bDone && P.SimT - T0 < MaxSeconds)
		{
			P.Advance(MaxSubstep);
		}
		return P.bDone;
	}
}
