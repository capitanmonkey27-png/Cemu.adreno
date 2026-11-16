#include "util/helpers/AndroidTweaks.h"
#include "util/helpers/AndroidTweaksLayout.h"
#include "config/ActiveSettings.h"
#include "Cemu/Logging/CemuLogging.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>

#if defined(__ANDROID__)
#include <sched.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <dirent.h>
#include <chrono>
#include <cstdio>
#include <thread>
#include <unordered_map>
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

	// One entry per switch. Drives the config file (read/write) and the in-app "Adreno tweaks" screen.
	struct FlagDesc
	{
		const char* key;
		bool Config::* member;
		const char* comment;
	};

	static const FlagDesc kFlags[] = {
		{"bigCoreAffinity", &Config::bigCoreAffinity, "Place Latte / PPC threads on dedicated big cores (topology aware)"},
		{"helperThreadDemotion", &Config::helperThreadDemotion, "Keep compile / cache helper threads off those cores and lower their priority"},
		{"limitCompileThreads", &Config::limitCompileThreads, "Cap shader compile threads at the number of little cores (less oversubscription, less peak RAM)"},
		{"byRegionBarriers", &Config::byRegionBarriers, "Experimental: by-region barriers between render passes (Adreno only, may be a no-op)"},
		{"fp16ShaderRewrite", &Config::fp16ShaderRewrite, "Experimental: FP16 rewrite of generated GLSL (Adreno only, falls back per shader on compile errors). Clear the shader caches after changing this."},
		{"threadCpuLog", &Config::threadCpuLog, "Diagnostic: every 5 s log the busiest threads and the core they ran on (first ~10 minutes of a game)"},
		{"tuForceSysmem", &Config::tuForceSysmem, "Turnip only: TU_DEBUG=sysmem, always render directly to memory instead of tiled GMEM rendering"},
		{"tuForceGmem", &Config::tuForceGmem, "Turnip only: TU_DEBUG=gmem, always use tiled GMEM rendering (ignored when tuForceSysmem=1)"},
		{"tuNoUbwc", &Config::tuNoUbwc, "Turnip only: TU_DEBUG=noubwc, turn off UBWC compression"},
		{"skipDeviceRobustBufferAccess", &Config::skipDeviceRobustBufferAccess, "Experimental: leave device-wide robustBufferAccess off when VK_EXT_pipeline_robustness is missing. Can be faster on Adreno/Turnip, but may cause garbage or GPU hangs in games that read out of bounds."},
	};

	static const FlagDesc* FindFlag(const char* key)
	{
		if (!key)
			return nullptr;
		for (const FlagDesc& f : kFlags)
			if (std::string(f.key) == key)
				return &f;
		return nullptr;
	}

	static std::filesystem::path ConfigFilePath()
	{
		return ActiveSettings::GetConfigPath("adreno_tweaks.txt");
	}

	static void WriteConfig(std::ostream& out, const Config& cfg)
	{
		out << "# Android/Adreno performance tweaks (1 = on, 0 = off)\n";
		out << "# Changes made in the app (Settings > Adreno tweaks) are written here; they apply after an app restart.\n";
		for (const FlagDesc& f : kFlags)
			out << "# " << f.comment << "\n" << f.key << "=" << (cfg.*(f.member) ? 1 : 0) << "\n";
	}

	// Returns false if the file could not be opened (cfg keeps its defaults in that case).
	static bool ReadConfigFile(const std::filesystem::path& path, Config& cfg)
	{
		std::ifstream in(path);
		if (!in.is_open())
			return false;
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
			if (const FlagDesc* f = FindFlag(key.c_str()))
				cfg.*(f->member) = on;
		}
		return true;
	}

	static Config LoadConfig()
	{
		Config cfg;
		try
		{
			const std::filesystem::path path = ConfigFilePath();
			if (!ReadConfigFile(path, cfg))
			{
				std::ofstream out(path);
				if (out.is_open())
					WriteConfig(out, cfg);
			}
		}
		catch (...)
		{
			// keep defaults
		}
		return cfg;
	}

	static std::atomic<unsigned> s_earlyConfigCalls{0};

	const Config& GetConfig()
	{
		static const Config s_defaults;
		static std::mutex s_mutex;
		static Config s_loaded;
		static std::atomic<bool> s_isLoaded{false};
		if (s_isLoaded.load(std::memory_order_acquire))
			return s_loaded;
		// The config file lives in the app's config folder, which is only known once ActiveSettings::SetPaths() has
		// run. A call from a thread that starts earlier gets the defaults and nothing is cached, so the real file is
		// still read afterwards (before, the first call froze the defaults for the whole session).
		if (ActiveSettings::GetConfigPath("").empty())
		{
			s_earlyConfigCalls.fetch_add(1, std::memory_order_relaxed);
			return s_defaults;
		}
		std::lock_guard lock(s_mutex);
		if (!s_isLoaded.load(std::memory_order_relaxed))
		{
			s_loaded = LoadConfig();
			s_isLoaded.store(true, std::memory_order_release);
		}
		return s_loaded;
	}

	// --- settings screen support ------------------------------------------------------------------------------
	// GetConfig() is read once per process, so the screen edits a separate "saved" copy that is written to the
	// config file on every change. IsActiveFlag() tells the screen what the running process is using.

	static std::string FlagsToString(const Config& cfg)
	{
		std::string s;
		for (const FlagDesc& f : kFlags)
			s += std::string(s.empty() ? "" : " ") + f.key + "=" + (cfg.*(f.member) ? "1" : "0");
		return s;
	}

	static std::string DescribeFile()
	{
		std::error_code ec;
		const std::filesystem::path path = ConfigFilePath();
		const bool exists = std::filesystem::exists(path, ec);
		const auto size = exists ? std::filesystem::file_size(path, ec) : 0;
		return path.string() + (exists ? " (exists, " + std::to_string((unsigned long long)size) + " bytes)" : " (MISSING)");
	}

	static std::mutex s_savedMutex;
	static Config s_saved;
	static bool s_savedLoaded = false;

	static Config& SavedConfigLocked()
	{
		if (!s_savedLoaded)
		{
			try
			{
				ReadConfigFile(ConfigFilePath(), s_saved);
			}
			catch (...)
			{
			}
			s_savedLoaded = true;
		}
		return s_saved;
	}

	bool GetSavedFlag(const char* key)
	{
		const FlagDesc* f = FindFlag(key);
		if (!f)
			return false;
		std::lock_guard lock(s_savedMutex);
		return SavedConfigLocked().*(f->member);
	}

	bool IsActiveFlag(const char* key)
	{
		const FlagDesc* f = FindFlag(key);
		if (!f)
			return false;
		return GetConfig().*(f->member);
	}

	bool SetSavedFlag(const char* key, bool value)
	{
		const FlagDesc* f = FindFlag(key);
		if (!f)
			return false;
		std::lock_guard lock(s_savedMutex);
		Config& cfg = SavedConfigLocked();
		cfg.*(f->member) = value;
		try
		{
			std::ofstream out(ConfigFilePath(), std::ios::trunc);
			if (!out.is_open())
				return false;
			WriteConfig(out, cfg);
			return true;
		}
		catch (...)
		{
			return false;
		}
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

	static std::string CpuSetToString(const cpu_set_t& set)
	{
		std::string s;
		for (int i = 0; i < CPU_SETSIZE; i++)
			if (CPU_ISSET(i, &set))
				s += (s.empty() ? "" : ",") + std::to_string(i);
		return s.empty() ? std::string("-") : s;
	}

	// DIAGNOSTIC: logs what was asked, whether the kernel accepted it, and the affinity the thread really has afterwards.
	// The cemuLog_thread itself is never logged from here (it would log from inside its own startup).
	static void ApplyCores(const std::string& tag, const std::vector<int>& cores)
	{
		const bool canLog = tag != "cemuLog_thread";
		const long tid = (long)syscall(SYS_gettid);
		if (cores.empty())
		{
			if (canLog)
				cemuLog_log(LogType::Force, "AndroidTweaks: [{}] tid={} no cores planned, affinity untouched", tag, tid);
			return;
		}
		cpu_set_t set;
		CPU_ZERO(&set);
		for (int c : cores)
			CPU_SET(c, &set);
		// 0 = calling thread.
		int rc = sched_setaffinity(0, sizeof(set), &set);
		int err = rc != 0 ? errno : 0;
		cpu_set_t now;
		CPU_ZERO(&now);
		sched_getaffinity(0, sizeof(now), &now);
		if (canLog)
			cemuLog_log(LogType::Force, "AndroidTweaks: [{}] tid={} wanted={} rc={} errno={} actual={}", tag, tid, CpuSetToString(set), rc, err, CpuSetToString(now));
	}

	static void ApplyNice(int nice)
	{
		// Best effort. Raising priority (negative nice) may be refused for apps, lowering never is.
		setpriority(PRIO_PROCESS, (id_t)syscall(SYS_gettid), nice);
	}
#endif

#if defined(__ANDROID__)
	// --- optional diagnostic: per-thread CPU census (threadCpuLog) -------------------------------------------
	struct ThreadSample
	{
		std::string name;
		unsigned long long ticks = 0;
		int cpu = -1;
	};

	// Parses /proc/self/task/<tid>/stat. Field numbers below are the ones from proc(5).
	static bool ReadThreadStat(const std::string& taskDir, ThreadSample& out)
	{
		std::ifstream f(taskDir + "/stat");
		std::string line;
		if (!f.is_open() || !std::getline(f, line))
			return false;
		const size_t open = line.find('(');
		const size_t close = line.rfind(')'); // the thread name may itself contain ')' or spaces
		if (open == std::string::npos || close == std::string::npos || close < open)
			return false;
		out.name = line.substr(open + 1, close - open - 1);
		std::istringstream rest(line.substr(close + 1));
		std::vector<std::string> fields; // fields[0] is stat field 3 (state)
		std::string tok;
		while (rest >> tok)
			fields.push_back(tok);
		if (fields.size() < 37)
			return false;
		out.ticks = std::strtoull(fields[11].c_str(), nullptr, 10) + std::strtoull(fields[12].c_str(), nullptr, 10); // utime + stime
		out.cpu = std::atoi(fields[36].c_str()); // processor
		return true;
	}

	static void CpuCensusMain()
	{
		const long clkTck = std::max(1L, sysconf(_SC_CLK_TCK));
		std::unordered_map<int, unsigned long long> prevTicks;
		auto prevTime = std::chrono::steady_clock::now();
		for (int sample = 0; sample < 120; sample++) // ~10 minutes, then stop
		{
			std::this_thread::sleep_for(std::chrono::seconds(5));
			const auto now = std::chrono::steady_clock::now();
			const double secs = std::chrono::duration<double>(now - prevTime).count();
			prevTime = now;

			struct Row
			{
				std::string name;
				int tid;
				double pct;
				int cpu;
			};
			std::vector<Row> rows;
			double total = 0.0;
			if (DIR* dir = opendir("/proc/self/task"))
			{
				while (dirent* e = readdir(dir))
				{
					const int tid = std::atoi(e->d_name);
					if (tid <= 0)
						continue;
					ThreadSample s;
					if (!ReadThreadStat("/proc/self/task/" + std::to_string(tid), s))
						continue;
					auto it = prevTicks.find(tid);
					const unsigned long long before = it != prevTicks.end() ? it->second : s.ticks; // first sight: no delta yet
					prevTicks[tid] = s.ticks;
					const double pct = secs > 0.0 ? 100.0 * double(s.ticks - before) / (double(clkTck) * secs) : 0.0;
					total += pct;
					if (pct >= 3.0)
						rows.push_back({s.name, tid, pct, s.cpu});
				}
				closedir(dir);
			}
			std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.pct > b.pct; });
			std::string msg;
			for (size_t i = 0; i < rows.size() && i < 10; i++)
			{
				char buf[128];
				snprintf(buf, sizeof(buf), "%s%s(%d)=%.0f%%@cpu%d", i ? " " : "", rows[i].name.c_str(), rows[i].tid, rows[i].pct, rows[i].cpu);
				msg += buf;
			}
			cemuLog_log(LogType::Force, "AndroidTweaks: census total={:.0f}% {}", total, msg.empty() ? std::string("(idle)") : msg);
		}
	}

	// Started from the Latte thread BEFORE it pins itself, so the census thread keeps the full affinity mask
	// and never competes with the Latte thread for its core. It is not named (no SetThreadName) on purpose.
	// One line in the log with the values this process really uses, written when the Latte thread starts.
	static void LogActiveConfigOnce()
	{
		static std::once_flag s_once;
		std::call_once(s_once, []() {
			cemuLog_log(LogType::Force, "AndroidTweaks: config file={} early_reads={} active: {}", DescribeFile(), s_earlyConfigCalls.load(), FlagsToString(GetConfig()));
		});
	}

	static void StartCpuCensus()
	{
		static std::once_flag s_once;
		std::call_once(s_once, []() { std::thread(CpuCensusMain).detach(); });
	}
#endif

	void ApplyThreadTuning(ThreadRole role, int emuCoreIndex, int emuThreadCount)
	{
#if defined(__ANDROID__)
		if (role == ThreadRole::Gpu)
			LogActiveConfigOnce();
		if (role == ThreadRole::Gpu && GetConfig().threadCpuLog)
			StartCpuCensus();
		if (!GetConfig().bigCoreAffinity)
			return;
		const auto& layout = GetCpuLayout();
		if (!layout.valid)
			return;
		const Layout::Role layoutRole = role == ThreadRole::Gpu ? Layout::Role::Gpu : Layout::Role::Emulation;
		ApplyCores(role == ThreadRole::Gpu ? std::string("Gpu/Latte") : "Emu/PPC core " + std::to_string(emuCoreIndex), Layout::PlanCores(layout, layoutRole, emuCoreIndex, emuThreadCount));
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
		{
			// DIAGNOSTIC: shows which thread names are NOT recognised (they keep whatever affinity they inherited)
			if (std::strcmp(threadName, "cemuLog_thread") != 0)
			{
				cpu_set_t cur;
				CPU_ZERO(&cur);
				sched_getaffinity(0, sizeof(cur), &cur);
				cemuLog_log(LogType::Force, "AndroidTweaks: [unmatched '{}'] tid={} inherited={}", threadName, (long)syscall(SYS_gettid), CpuSetToString(cur));
			}
			return;
		}
		if (role == Layout::Role::Helper && !cfg.helperThreadDemotion)
			return;
		const auto& layout = GetCpuLayout();
		if (!layout.valid)
			return;
		// always set explicitly: threads inherit the affinity of the thread that created them
		ApplyCores(threadName, Layout::PlanCores(layout, role));
		if (role == Layout::Role::Helper)
			ApplyNice(8);
#else
		(void)threadName;
#endif
	}

	unsigned LimitCompileThreads(unsigned requested)
	{
#if defined(__ANDROID__)
		const auto& cfg = GetConfig();
		// helpers are only confined to the little cores in exclusive mode (see ApplyThreadTuningByName)
		if (!cfg.limitCompileThreads || !cfg.bigCoreAffinity || !cfg.helperThreadDemotion)
			return requested;
		const auto& layout = GetCpuLayout();
		if (!layout.valid || !layout.exclusive || layout.little.empty())
			return requested;
		return std::max(1u, std::min(requested, (unsigned)layout.little.size()));
#else
		return requested;
#endif
	}

	std::string DescribeTweaksState()
	{
		std::lock_guard lock(s_savedMutex);
		return "file: " + DescribeFile() + "\nactive: " + FlagsToString(GetConfig()) + "\nsaved: " + FlagsToString(SavedConfigLocked()) + "\nearly config reads (before paths were set): " + std::to_string(s_earlyConfigCalls.load());
	}

	std::string DescribeCpuLayout()
	{
#if defined(__ANDROID__)
		return Layout::Describe(GetCpuLayout());
#else
		return "not an Android device";
#endif
	}

	void ApplyGpuDriverEnv()
	{
#if defined(__ANDROID__)
		// The driver reads TU_DEBUG when its instance is created, so this has to run before the library is loaded.
		// Build the value from the TU_DEBUG the process started with, so calling this again never stacks flags.
		static const std::string s_originalValue = []() {
			const char* v = std::getenv("TU_DEBUG");
			return std::string(v ? v : "");
		}();
		const Config& cfg = GetConfig();
		std::string flags;
		auto add = [&flags](const char* f) {
			if (!flags.empty())
				flags += ",";
			flags += f;
		};
		if (cfg.tuForceSysmem)
			add("sysmem");
		else if (cfg.tuForceGmem)
			add("gmem");
		if (cfg.tuNoUbwc)
			add("noubwc");
		if (flags.empty())
			return;
		const std::string value = s_originalValue.empty() ? flags : s_originalValue + "," + flags;
		setenv("TU_DEBUG", value.c_str(), 1);
		cemuLog_log(LogType::Force, "AndroidTweaks: TU_DEBUG={}", value);
#endif
	}
}
