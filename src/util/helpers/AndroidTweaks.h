#pragma once

#include <string>

// Optional Android/Adreno performance tweaks.
// Settings are read once from <config dir>/adreno_tweaks.txt (created with defaults on first use).
// File format: one "key=0|1" per line, '#' starts a comment.
namespace AndroidTweaks
{
	struct Config
	{
		// Topology-aware thread placement on big.LITTLE devices (harmless if the kernel refuses):
		// Latte thread -> prime core, the 3 PPC scheduler threads -> one big core each (when >= 4 big cores exist),
		// PPC recompiler -> big cores. Falls back to "any big core" on SoCs with fewer big cores.
		bool bigCoreAffinity = true;
		// Keep shader/pipeline compile + cache/IO helper threads off the cores reserved above and lower their
		// priority, so they can't steal time from the emulation threads. Only active together with bigCoreAffinity.
		bool helperThreadDemotion = true;
		// Add VK_DEPENDENCY_BY_REGION_BIT to the memory barriers issued between render passes (Adreno only).
		// Experimental: only has a defined effect for framebuffer-space stages, may be a no-op.
		bool byRegionBarriers = false;
		// Experimental text-based FP16 rewrite of generated GLSL (Adreno with FP16 support only).
		// Shaders that fail to compile after the rewrite fall back to the original GLSL automatically.
		bool fp16ShaderRewrite = false;
		// Cap the pipeline compile thread pool at the number of "little" cores (asymmetric CPUs only, and only while
		// helper threads are confined to those cores). Cemu starts up to 8 compile threads, which just oversubscribe
		// the little cores on a phone and raise peak RAM use while shaders compile.
		bool limitCompileThreads = true;
		// Experimental (GPU): without VK_EXT_pipeline_robustness (Turnip) Cemu turns on robustBufferAccess for the WHOLE
		// device, so every pipeline pays for bounds checking although only a few need it. 1 = leave it off.
		// May be faster, but games that read out of bounds can then show garbage or crash the GPU driver.
		bool skipDeviceRobustBufferAccess = false;
		// Diagnostic: every 5 s write the busiest threads (name, tid, % of one core, core last run on) to the log,
		// for the first ~10 minutes after a game starts. Shows what actually loads each core.
		bool threadCpuLog = false;
	};

	const Config& GetConfig();

	enum class ThreadRole
	{
		Emulation, // PPC core scheduler threads
		Gpu        // Latte command processor thread
	};

	// Call from the thread that should be tuned.
	// emuCoreIndex / emuThreadCount: PPC core index of this scheduler thread and how many scheduler threads exist
	// (1 in single-core mode, 3 in multi-core mode). Only used for ThreadRole::Emulation.
	void ApplyThreadTuning(ThreadRole role, int emuCoreIndex = 0, int emuThreadCount = 3);

	// Called from SetThreadName(): applies the right placement to well-known helper threads by name
	// (compile threads, cache writers, PPC recompiler, ...). Unknown names are left alone.
	void ApplyThreadTuningByName(const char* threadName);

	// Returns how many pipeline compile threads to start (never more than 'requested', never less than 1).
	unsigned LimitCompileThreads(unsigned requested);

	// --- in-app settings screen (Settings > Adreno tweaks) ---------------------------------------------------
	// 'key' is the config file key, e.g. "bigCoreAffinity". Unknown keys read as false and are never saved.
	// GetConfig() is read once per process: SetSavedFlag() writes adreno_tweaks.txt immediately, the new value is
	// used after the app is restarted. IsActiveFlag() is the value the running process actually uses.
	bool GetSavedFlag(const char* key);
	bool IsActiveFlag(const char* key);
	bool SetSavedFlag(const char* key, bool value);

	// One line describing the detected CPU layout, e.g. "big(strongest first)=[7,6,5,4] little=[0,1,2,3] mode=exclusive"
	std::string DescribeCpuLayout();
}
