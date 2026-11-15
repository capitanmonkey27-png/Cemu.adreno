#pragma once

// Pure (no OS / no Cemu dependencies) CPU layout logic used by AndroidTweaks.
// Kept header-only so it can be unit tested on a desktop machine.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace AndroidTweaks::Layout
{
	struct CoreInfo
	{
		int id = 0;
		// Any monotonic "how fast is this core" number: cpu_capacity, cpuinfo_max_freq or a CPU-part score.
		// 0 / negative = unknown.
		long metric = 0;
	};

	enum class Role
	{
		Gpu,         // Latte command processor thread (the Vulkan driver runs on it too)
		Emulation,   // PPC core scheduler thread
		Recompiler,  // PPC JIT worker: latency sensitive, but not a permanent 100% thread
		Helper       // shader / pipeline compile, cache writers, loaders: throughput work that must not steal big cores
	};

	struct CpuLayout
	{
		std::vector<int> big;    // every non-little core, strongest first
		std::vector<int> little; // efficiency cores
		std::vector<int> all;
		// true when there are enough big cores to give Latte + 3 PPC threads one core each
		bool exclusive = false;
		bool valid = false;
	};

	// Groups cores into clusters (a core joins the current cluster while its metric is within 10% of the
	// cluster's fastest core), then treats the slowest cluster as "little" if there is more than one cluster.
	// Symmetric CPUs (one cluster) or unknown topology give valid == false -> callers must not touch affinity.
	inline CpuLayout BuildLayout(std::vector<CoreInfo> cores)
	{
		CpuLayout out;
		for (const auto& c : cores)
			out.all.push_back(c.id);
		if (cores.size() < 2)
			return out;
		for (const auto& c : cores)
			if (c.metric <= 0)
				return out; // topology unknown

		// strongest first, ties -> higher id first (big cores usually have the higher ids)
		std::sort(cores.begin(), cores.end(), [](const CoreInfo& a, const CoreInfo& b) {
			if (a.metric != b.metric)
				return a.metric > b.metric;
			return a.id > b.id;
		});

		std::vector<std::vector<CoreInfo>> clusters;
		for (const auto& c : cores)
		{
			if (!clusters.empty() && c.metric * 10 >= clusters.back().front().metric * 9)
				clusters.back().push_back(c);
			else
				clusters.push_back({c});
		}
		if (clusters.size() < 2)
			return out; // symmetric

		for (size_t i = 0; i < clusters.size(); i++)
		{
			for (const auto& c : clusters[i])
			{
				if (i + 1 == clusters.size())
					out.little.push_back(c.id);
				else
					out.big.push_back(c.id);
			}
		}
		std::sort(out.little.begin(), out.little.end());
		std::sort(out.all.begin(), out.all.end());
		out.exclusive = out.big.size() >= 4;
		out.valid = !out.big.empty() && !out.little.empty();
		return out;
	}

	// Which host cores a thread should be allowed to run on.
	// emuCoreIndex / emuThreadCount only matter for Role::Emulation.
	inline std::vector<int> PlanCores(const CpuLayout& l, Role role, int emuCoreIndex = 0, int emuThreadCount = 3)
	{
		if (!l.valid)
			return {};
		switch (role)
		{
		case Role::Gpu:
			return l.exclusive ? std::vector<int>{l.big[0]} : l.big;
		case Role::Emulation:
		{
			if (!l.exclusive)
				return l.big;
			// PPC core 1 usually runs the game's main thread -> best core after the GPU thread's one
			int slot = 0;
			if (emuThreadCount > 1)
				slot = (emuCoreIndex == 1) ? 0 : (emuCoreIndex == 0 ? 1 : 2);
			return {l.big[1 + slot]};
		}
		case Role::Recompiler:
			return l.big;
		case Role::Helper:
		default:
		{
			if (!l.exclusive)
				return l.all;
			// everything except the cores reserved for Latte + the PPC threads
			std::vector<int> r(l.big.begin() + 4, l.big.end());
			r.insert(r.end(), l.little.begin(), l.little.end());
			std::sort(r.begin(), r.end());
			return r;
		}
		}
	}

	// Maps a thread name (as given to SetThreadName) to a role. Returns false for threads we leave alone.
	inline bool RoleFromThreadName(const std::string& name, Role& role)
	{
		static const char* const helperNames[] = {
			"compilePl", "vkShaderComp", "plCacheCompiler", "plCacheWriter", "vkDriverPlCache",
			"bootsnd", "fileCache", "cemuLog_thread", "TitleListWorker", "SaveListWorker"};
		for (const char* n : helperNames)
		{
			if (name == n)
			{
				role = Role::Helper;
				return true;
			}
		}
		if (name == "PPCRecompiler")
		{
			role = Role::Recompiler;
			return true;
		}
		return false;
	}

	inline std::string Describe(const CpuLayout& l)
	{
		auto list = [](const std::vector<int>& v) {
			std::string s;
			for (int c : v)
				s += (s.empty() ? "" : ",") + std::to_string(c);
			return s.empty() ? std::string("-") : s;
		};
		if (!l.valid)
			return "symmetric or unknown CPU topology, affinity untouched";
		return "big(strongest first)=[" + list(l.big) + "] little=[" + list(l.little) + "] mode=" + (l.exclusive ? "exclusive" : "shared");
	}
}
