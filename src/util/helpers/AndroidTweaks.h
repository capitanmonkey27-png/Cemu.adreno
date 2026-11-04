#pragma once

// Optional Android/Adreno performance tweaks.
// Settings are read once from <config dir>/adreno_tweaks.txt (created with defaults on first use).
// File format: one "key=0|1" per line, '#' starts a comment.
namespace AndroidTweaks
{
	struct Config
	{
		// Pin emulation / GPU host threads to the faster CPU cores (big.LITTLE devices). Harmless if the kernel refuses.
		bool bigCoreAffinity = true;
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

	// Call from the thread that should be tuned
	void ApplyThreadTuning(ThreadRole role);
}
