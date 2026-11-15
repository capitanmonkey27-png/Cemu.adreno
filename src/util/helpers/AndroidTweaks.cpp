#include "util/helpers/AndroidTweaks.h"
#include "util/helpers/AndroidTweaksLayout.h"
#include "config/ActiveSettings.h"
#include "Cemu/Logging/CemuLogging.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

#if defined(__ANDROID__)
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#endif

namespace AndroidTweaks
{
	static std::string Trim(const std::string& s)
	{
		size_t b = s.find_first_not_of(" \t\r\n");
		if (b == std::string::npos)
			return "";
		size_t e = s.find_last_not_of(" \t\r\n");
		return s.substr(b, e - b + 1);
	}

	static const char* kDefaultFile =
		"# Android/Adreno performance tweaks (1 = on, 0 = off)\n"
		"# Place Latte / PPC threads on dedicated big cores (topology aware)\n"
		"bigCoreAffinity=1\n"
		"# Keep compile / cache helper threads off those cores and lower their priority\n"
		"helperThreadDemotion=1\n"
		"# Experimental: by-region barriers between render passes (Adreno only, may be a no-op)\n"
		"byRegionBarriers=0\n"
		"# Experimental: FP16 rewrite of generated GLSL (Adreno only, falls back per shader on compile errors).\n"
		"# Clear the shader caches after changing this.\n"
		"fp16ShaderRewrite=0\n";

	static Config LoadConfig()
	{
		Config cfg;
		std::filesystem::path path;
		try
		{
			path = ActiveSettings::GetConfigPath("adreno_tweaks.txt");
			std::ifstream in(path);
			if (!in.is_open())
			{
				std::ofstream out(path);
				if (out.is_open())
					out << kDefaultFile;
				return cfg;
			}
			std::string line;
			while (std::getline(in, line))
			{
				size_t hash = line.find('#');
				if (hash != std::string::npos)
					line.erase(hash);
				size_t eq = line.find('=');
				if (eq == std::string::npos)
					continue;
				std::string key = Trim(line.substr(0, eq));
				std::string val = Trim(line.substr(eq + 1));
				bool on = (val == "1" || val == "true" || val == "on");
				if (key == "bigCoreAffinity")
					cfg.bigCoreAffinity = on;
				else if (key == "helperThreadDemotion")
					cfg.helperThreadDemotion = on;
				else if (key == "byRegionBarriers")
					cfg.byRegionBarriers = on;
				else if (key == "fp16ShaderRewrite")
					cfg.fp16ShaderRewrite = on;
			}
		}
		catch (...)
		{
			// keep defaults
		}
		return cfg;
	}

	const Config& GetConfig()
	{
		static const Config s_config = LoadConfig(); // thread-safe init
		return s_config;
	}

#if defined(__ANDROID__)
	// --- CPU topology detection -------------------------------------------------------------------------------

	static long ReadLongFile(const std::string& path)
	{
		std::ifstream f(path);
		long v = 0;
		if (f.is_open() && (f >> v))
			return v;
		return 0;
	}

	// Rough relative speed of an ARM core by its "CPU part" id, used only if sysfs gives us nothing.
	static long CpuPartScore(unsigned part)
	{
		switch (part)
		{
		case 0xd03: case 0xd04: case 0xd05: case 0xd46: case 0xd80: // A53 A35 A55 A510 A520
			return 100;
		case 0xd0a: case 0xd0b: case 0xd0d: case 0xd41: case 0xd47: case 0xd4d: case 0xd81: case 0xd87: // A75 A76 A77 A78 A710 A715 A720 A725
			return 250;
		case 0xd44: case 0xd4c: case 0xd48: case 0xd4e: case 0xd82: case 0xd85: // X1 X1C X2 X3 X4 X925
			return 300;
		default:
			return 0;
		}
	}

	static std::vector<long> ReadCpuPartScores(long cpuCount)
	{
		std::vector<long> scores(cpuCount, 0);
		std::ifstream f("/proc/cpuinfo");
		std::string line;
		long current = -1;
		while (std::getline(f, line))
		{
			if (line.rfind("processor", 0) == 0)
			{
				size_t c = line.find(':');
				current = c == std::string::npos ? -1 : std::strtol(line.c_str() + c + 1, nullptr, 10);
			}
			else if (line.rfind("CPU part", 0) == 0 && current >= 0 && current < cpuCount)
			{
				size_t c = line.find(':');
				if (c != std::string::npos)
					scores[current] = CpuPartScore((unsigned)std::strtoul(line.c_str() + c + 1, nullptr, 16));
			}
		}
		return scores;
	}

	static const Layout::CpuLayout& GetCpuLayout()
	{
		static const Layout::CpuLayout s_layout = []() {
			long cpuCount = sysconf(_SC_NPROCESSORS_CONF);
			Layout::CpuLayout result;
			if (cpuCount <= 1 || cpuCount > CPU_SETSIZE)
				return result;
			std::vector<Layout::CoreInfo> cores;
			std::vector<long> partScores;
			for (long i = 0; i < cpuCount; i++)
			{
				const std::string base = "/sys/devices/system/cpu/cpu" + std::to_string(i);
				long metric = ReadLongFile(base + "/cpu_capacity");
				if (metric <= 0)
					metric = ReadLongFile(base + "/cpufreq/cpuinfo_max_freq");
				if (metric <= 0)
				{
					if (partScores.empty())
						partScores = ReadCpuPartScores(cpuCount);
					metric = partScores[i];
				}
				cores.push_back({(int)i, metric});
			}
			result = Layout::BuildLayout(cores);
			cemuLog_log(LogType::Force, "AndroidTweaks: {}", Layout::Describe(result));
			return result;
		}();
		return s_layout;
	}

	// --- applying placement -----------------------------------------------------------------------------------

	static void ApplyCores(const std::vector<int>& cores)
	{
		if (cores.empty())
			return;
		cpu_set_t set;
		CPU_ZERO(&set);
		for (int c : cores)
			CPU_SET(c, &set);
		// 0 = calling thread. Failure (vendor restrictions, cgroups) is harmless.
		sched_setaffinity(0, sizeof(set), &set);
	}

	static void ApplyNice(int nice)
	{
		// Best effort. Raising priority (negative nice) may be refused for apps, lowering never is.
		setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), nice);
	}
#endif

	void ApplyThreadTuning(ThreadRole role, int emuCoreIndex, int emuThreadCount)
	{
#if defined(__ANDROID__)
		if (!GetConfig().bigCoreAffinity)
			return;
		const auto& layout = GetCpuLayout();
		if (!layout.valid)
			return;
		const Layout::Role layoutRole = role == ThreadRole::Gpu ? Layout::Role::Gpu : Layout::Role::Emulation;
		ApplyCores(Layout::PlanCores(layout, layoutRole, emuCoreIndex, emuThreadCount));
		ApplyNice(-4);
#else
		(void)role;
		(void)emuCoreIndex;
		(void)emuThreadCount;
#endif
	}

	void ApplyThreadTuningByName(const char* threadName)
	{
#if defined(__ANDROID__)
		const auto& cfg = GetConfig();
		if (!cfg.bigCoreAffinity || !threadName)
			return;
		Layout::Role role;
		if (!Layout::RoleFromThreadName(threadName, role))
			return;
		if (role == Layout::Role::Helper && !cfg.helperThreadDemotion)
			return;
		const auto& layout = GetCpuLayout();
		if (!layout.valid)
			return;
		// always set explicitly: threads inherit the affinity of the thread that created them
		ApplyCores(Layout::PlanCores(layout, role));
		if (role == Layout::Role::Helper)
			ApplyNice(8);
#else
		(void)threadName;
#endif
	}
}
