#include "util/helpers/AndroidTweaks.h"
#include "config/ActiveSettings.h"
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>

#if defined(__ANDROID__)
#include <sched.h>
#include <unistd.h>
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
		"# Pin emulation + GPU threads to the faster CPU cores\n"
		"bigCoreAffinity=1\n"
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
	// Cores whose max frequency is within 70% of the fastest core
	static const std::vector<int>& GetPerformanceCores()
	{
		static const std::vector<int> s_cores = []() {
			std::vector<int> result;
			long cpuCount = sysconf(_SC_NPROCESSORS_CONF);
			if (cpuCount <= 0 || cpuCount > CPU_SETSIZE)
				return result;
			std::vector<long> freq(cpuCount, 0);
			long maxFreq = 0;
			for (long i = 0; i < cpuCount; i++)
			{
				std::ifstream f("/sys/devices/system/cpu/cpu" + std::to_string(i) + "/cpufreq/cpuinfo_max_freq");
				long v = 0;
				if (f.is_open() && (f >> v))
					freq[i] = v;
				maxFreq = std::max(maxFreq, freq[i]);
			}
			if (maxFreq <= 0)
				return result; // topology unknown -> don't touch affinity
			for (long i = 0; i < cpuCount; i++)
			{
				if (freq[i] * 10 >= maxFreq * 7)
					result.push_back((int)i);
			}
			if ((long)result.size() == cpuCount)
				result.clear(); // symmetric CPU, nothing to gain
			return result;
		}();
		return s_cores;
	}
#endif

	void ApplyThreadTuning(ThreadRole role)
	{
		(void)role;
#if defined(__ANDROID__)
		if (!GetConfig().bigCoreAffinity)
			return;
		const auto& cores = GetPerformanceCores();
		if (cores.empty())
			return;
		cpu_set_t set;
		CPU_ZERO(&set);
		for (int c : cores)
			CPU_SET(c, &set);
		// failure (vendor restrictions, cgroups) is harmless
		sched_setaffinity(0, sizeof(set), &set);
#endif
	}
}
