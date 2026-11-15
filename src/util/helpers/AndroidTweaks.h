#pragma once

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
}
