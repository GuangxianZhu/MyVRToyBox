#include "ToyEtchCore.h"

#include <algorithm>
#include <cmath>

// 每个式子的写法（括号、先后顺序）都和 etch_core.js 保持一致，浮点结果才能对得上

namespace ToyEtch
{
	const char* ProblemKey(EProblem Problem)
	{
		switch (Problem)
		{
		case EProblem::LowPower: return "lowpower";
		case EProblem::LowFlow:  return "lowflow";
		case EProblem::NoGas:    return "nogas";
		default:                 return "";
		}
	}

	FModel Model(const FRecipe& R)
	{
		// 等离子体密度 ∝ 源功率；离子能量 ≈ 偏压功率 ÷ 离子电流（离子电流 ∝ 密度）
		// 往下刻 = 离子和自由基"串联"：哪个少就被哪个卡住（离子-自由基协同）
		// 往侧面刻 = 自由基自己的化学反应，被侧壁钝化层挡掉一部分
		FModel M;
		M.n = (R.Ws / 800) / (1 + R.P / 150);
		M.Ei = std::min(800.0, 15 + 0.9 * R.Wb / std::max(M.n, 0.15));
		const double Yield = std::max(0.0, std::sqrt(M.Ei) - std::sqrt(12.0)) / (std::sqrt(115.0) - std::sqrt(12.0));
		M.Gn = (R.Cl2 + 0.6 * R.HBr) / 100 * std::sqrt(M.n) * std::sqrt(R.P / 20);
		const double Ion = 1 / (1 / (M.Gn + 1e-6) + 1 / (1.2 * Yield * M.n + 1e-6));
		const double Chem = 0.03 * M.Gn * std::exp((R.T - 40) / 40);
		const double HBrFrac = R.HBr / (R.Cl2 + R.HBr + 1);
		M.pass = std::min(0.95, 0.7 * HBrFrac + 0.8 * R.O2 / 20);
		M.ERv = 300 * (Ion + Chem);
		M.ERl = 300 * Chem * (1 - M.pass) + 300 * Ion * 0.02 * (R.P / 20);
		const double YieldOx = std::max(0.0, std::sqrt(M.Ei) - std::sqrt(40.0)) / (std::sqrt(115.0) - std::sqrt(40.0));
		M.ERox = 300 * 0.03 * YieldOx * M.n * (1 - 0.6 * HBrFrac) * (1 - 0.3 * R.O2 / 20) + 0.2;
		M.ERmask = 300 * 0.12 * Yield * M.n + 300 * 0.25 * (R.O2 / 20) * std::sqrt(M.n) + 1;
		M.u = 0.025 + 0.05 * std::abs(R.P - 12) / 70 + (R.Ws > 1100 ? 0.02 : 0);
		M.total = R.Cl2 + R.HBr + R.O2;
		M.sel = M.ERv / M.ERox;
		if (R.Ws < 150) M.Problem = EProblem::LowPower;
		else if (M.total < 10) M.Problem = EProblem::LowFlow;
		else if (R.Cl2 + R.HBr < 5) M.Problem = EProblem::NoGas;
		return M;
	}

	void EtchTick(FEtch& E, const FModel& M, double Dt)
	{
		const double K = Dt / 60;
		E.t += Dt;
		const double Fc = 1 + M.u, Fe = 1 - M.u;
		if (E.Dc < 100) E.Dc = std::min(100.0, E.Dc + M.ERv * Fc * K);
		else if (E.Ox > 0) E.Ox = std::max(0.0, E.Ox - M.ERox * Fc * K);
		else E.Si += M.ERv * 0.8 * Fc * K;
		E.De = std::min(100.0, E.De + M.ERv * Fe * K);
		const int Reach = static_cast<int>(std::min(100.0, std::floor(E.Dc)));
		for (int i = 0; i <= Reach; i++) E.SideCut[i] = static_cast<float>(std::min(30.0, E.SideCut[i] + M.ERl * K));
		if (E.MaskH > 0) E.MaskH = std::max(0.0, E.MaskH - M.ERmask * K);
		else E.GateLoss = std::min(100.0, E.GateLoss + M.ERv * 0.9 * K);
		// 晶圆上已经刻穿的面积：刻蚀速率从中心 (1+u) 到边缘 (1-u)，按面积均匀分布
		const double S = (1 + M.u - 100 / std::max(1e-6, M.ERv / 60 * E.t)) / (2 * M.u);
		E.Uncleared = 1 - std::clamp(S, 0.0, 1.0);
	}

	double OesTick(FEtch& E, const FModel& M, double Dt, double Plasma, bool bEtching, double Noise)
	{
		// 等离子体本身有一点底光；刻多晶硅时含硅产物的谱线和刻蚀速率成正比
		const double Amp = 0.86 * std::clamp(M.ERv / 150, 0.15, 1.0);
		const double Target = Plasma > 0 ? Plasma * 0.12 + (bEtching ? Amp * E.Uncleared : 0) : 0;
		const double Oes = Target + (Plasma > 0 ? Noise : 0);
		E.OesF = E.OesF + (Oes - E.OesF) * std::min(1.0, Dt * 3);
		if (bEtching && !E.bHasEndpoint && E.t > 3 && E.OesF < 0.12 + Amp * 0.5)
		{
			E.bHasEndpoint = true;
			E.tEp = E.t;
		}
		return Oes;
	}

	FEvaluation Evaluate(const FEtch& E)
	{
		FEvaluation V;
		V.SideCutMax = *std::max_element(E.SideCut, E.SideCut + 101);
		V.bResidue = E.De < 99.9;
		V.Ok[0] = !E.bTimeout && !V.bResidue;
		V.Ok[1] = V.SideCutMax <= 6;
		V.Ok[2] = E.Ox >= 2 && E.Si == 0;
		V.Ok[3] = E.MaskH >= 10;
		V.Ok[4] = E.tMain <= 120;
		return V;
	}

	FSimulation SimulateEtch(const FRecipe& R, double Dt)
	{
		// 起辉以后等离子体已经稳定（plasma = 1），光谱滤波值从底光 0.12 开始
		FSimulation Sim;
		Sim.M = Model(R);
		if (Sim.M.Problem != EProblem::None) return Sim;
		FEtch& E = Sim.E;
		E.OesF = 0.12;
		while (!E.bHasEndpoint && E.t <= MainTimeout)
		{
			EtchTick(E, Sim.M, Dt);
			OesTick(E, Sim.M, Dt, 1, true, 0);
		}
		if (!E.bHasEndpoint) E.bTimeout = true;
		E.tMain = E.t;
		const double TOver = E.bTimeout ? 0 : E.tMain * R.Oe / 100;
		for (double Tt = 0; Tt + 1e-9 < TOver; Tt += Dt)
		{
			EtchTick(E, Sim.M, Dt);
			OesTick(E, Sim.M, Dt, 1, true, 0);
		}
		Sim.Result = Evaluate(E);
		return Sim;
	}
}
