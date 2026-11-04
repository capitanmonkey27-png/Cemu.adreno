# Adreno tweaks for Cemu Android 0.5

Apply from the Cemu-0.5 source root:

    patch -p1 < adreno-tweaks-on-cemu-0.5.patch

Settings live in `adreno_tweaks.txt` (config dir, created with defaults on first run):

    bigCoreAffinity=1     # default ON  - pin PPC scheduler + Latte threads to the fast CPU cores
    byRegionBarriers=0    # default OFF - experimental, Adreno only, may be a no-op
    fp16ShaderRewrite=0   # default OFF - experimental, Adreno only; clear shader caches after changing

## What is in the patch
- Thor's Vulkan work ported onto 0.5 via 3-way merge (base: SSimco 0.3, ours: 0.5, theirs: Thor 0.3.2):
  VulkanCapabilities, ShaderOptimizer, VulkanBarrierHelpers, FP16 surface-copy shaders, SPIR-V
  optimize-for-speed on Android.
- Fixes to Thor's code:
  - FP16 regexes dropped the `;` / `=` after each declaration (referenced non-existent capture groups).
  - Shaders using floatBitsTo*/intBitsToFloat (nearly all Cemu shaders) are never rewritten.
  - Rewritten GLSL is trial-parsed with glslang; on failure the original GLSL is used.
  - FP16 device features/extensions are only enabled when the FP16 tweak is on.
  - Thor's ShaderCache removed: it deadlocked (SaveToDisk re-locked a non-recursive mutex) and was redundant,
    Cemu's SPIR-V cache already stores the compiled result.
  - Unused VK_QCOM_render_pass_transform enablement removed.
- New: big-core thread affinity (util/helpers/AndroidTweaks.*).

## Verification
- ShaderOptimizer + config loader compiled and unit-tested standalone (g++, stubbed Vulkan headers).
- NOT built with the Android NDK, NOT run on a device, no benchmarks.

## Building an APK that installs next to the original
The patch also makes the release build install as `info.cemu.cemu.adreno` (name "Cemu Adreno"), so it
coexists with the original `info.cemu.cemu`. It uses its own app data folder, so keys, game paths and
saves are separate. The content provider authority follows the app id, so there is no install conflict.

Build with GitHub Actions (nothing to install locally):
1. Put the patched source in your own GitHub repo (keep `.github/` and the submodules).
2. Optional but recommended, once: create a signing key so later builds can update in place:

       keytool -genkeypair -v -keystore adreno.jks -alias adreno -keyalg RSA -keysize 4096 -validity 10000
       base64 -w0 adreno.jks   # paste into the secret below

   Repo Settings > Secrets > Actions: `ANDROID_STORE_FILE_BASE64`, `ANDROID_KEY_STORE_PASSWORD`, `ANDROID_KEY_ALIAS`
   (the key password must equal the store password, that is how build.gradle.kts reads it).
3. Actions tab > "Build Android APK (Adreno)" > Run workflow. Download `cemu-adreno-apk` when it finishes.

Without the secrets the APK is signed with the runner's throwaway debug key, which changes every run.
