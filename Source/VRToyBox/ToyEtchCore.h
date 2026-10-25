#pragma once

/**
 * 干法刻蚀机的工艺模型（C++ 版）。逐行照 ITBasics 干法刻蚀机/web/etch_core.js 翻译，网页和 VR 算出来的数要一样。
 *
 * 只算数，不碰 UE 的类型，所以云端也能用 g++ 单独编译、对答案。
 * 对答案：ITBasics 的 tools/export_core.mjs 用网页版算出 etcher_golden.json（拷在 Content/Etcher/），
 * 控制台跑 toy.EtchSelfTest，逐组比较（见 ToyEtchSelfTest.cpp）。
 * 改公式的顺序：先改 etch_core.js → 重新跑 export_core.mjs → 再照着改这里 → toy.EtchSelfTest 全过。
 *
 * 模型是简化的：趋势对、数量级对，系数是估计值（见 ITBasics 干法刻蚀机/README.md）。
 */
namespace ToyEtch
{
	/** 配方。单位：腔压 mTorr，功率 W，流量 sccm，温度 °C，过刻 %（按主刻时间） */
	struct FRecipe
	{
		double P = 10, Ws = 600, Wb = 80, Cl2 = 60, HBr = 120, O2 = 4, T = 50, Oe = 30;
	};

	/** 配方检查不通过的原因，文字在 etcher_content.json 的 problems 里（键名见 ProblemKey） */
	enum class EProblem : unsigned char { None, LowPower, LowFlow, NoGas };
	const char* ProblemKey(EProblem Problem);

	/** 按配方算出来的稳态量。速率单位 nm/min，离子能量 eV，n 是等离子体密度（相对值） */
	struct FModel
	{
		double n = 0, Ei = 0, Gn = 0, ERv = 0, ERl = 0, ERox = 0, ERmask = 0, u = 0, pass = 0, sel = 0, total = 0;
		EProblem Problem = EProblem::None;
	};
	FModel Model(const FRecipe& R);

	/** 一片晶圆的刻蚀状态。深度 nm；SideCut[i] 是深度 i nm 处侧壁被往里刻掉多少（nm，网页里是 Float32Array，这里也用 float） */
	struct FEtch
	{
		double Dc = 0, De = 0;
		float SideCut[101] = {};
		double MaskH = 60, Ox = 4, Si = 0, GateLoss = 0, t = 0;
		bool bHasEndpoint = false;
		double tEp = 0, tMain = 0;
		bool bTimeout = false;
		double Uncleared = 1, OesF = 0;
	};

	/** 主刻最多等多久（秒），到时还没终点就算"没刻完" */
	constexpr double MainTimeout = 180.0;

	/** 主刻 + 过刻期间每帧调用（dt 秒） */
	void EtchTick(FEtch& E, const FModel& M, double Dt);

	/** 光谱终点：返回这一帧的原始信号；刚判到终点时 E.bHasEndpoint 变成 true，E.tEp = 当时的 E.t。
	 *  Plasma 0~1 是辉光强度，Noise 加在原始信号上（标准答案用 0） */
	double OesTick(FEtch& E, const FModel& M, double Dt, double Plasma, bool bEtching, double Noise = 0);

	/** 跑完一片后的 5 项判定，顺序和 etcher_content.json 的 results 一样：刻干净、侧壁直、栅氧保住、光刻胶余量、够快 */
	struct FEvaluation
	{
		double SideCutMax = 0;
		bool bResidue = false;
		bool Ok[5] = {};
	};
	FEvaluation Evaluate(const FEtch& E);

	/** 只算刻蚀那一段（主刻到终点 + 过刻），固定步长、没有噪声。和 etch_core.js 的 simulateEtch() 一一对应 */
	struct FSimulation
	{
		FModel M;
		FEtch E;
		FEvaluation Result;
	};
	FSimulation SimulateEtch(const FRecipe& R, double Dt = 0.05);
}
