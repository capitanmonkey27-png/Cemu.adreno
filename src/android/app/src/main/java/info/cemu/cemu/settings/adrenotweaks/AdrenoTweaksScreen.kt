package info.cemu.cemu.settings.adrenotweaks

import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.MutableState
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import info.cemu.cemu.common.ui.components.Header
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeAdrenoTweaks
import info.cemu.cemu.nativeinterface.NativeSettings

@Composable
private fun rememberTweakState(key: String): MutableState<Boolean> =
    rememberSaveable { mutableStateOf(NativeAdrenoTweaks.getSavedTweak(key)) }

@Composable
private fun TweakToggle(
    key: String,
    label: String,
    description: String,
    state: MutableState<Boolean>,
    enabled: Boolean = true,
) {
    // the tweaks are read once when the app process starts, so a changed switch needs a restart
    val activeValue = remember(key) { NativeAdrenoTweaks.isTweakActive(key) }
    val restartRequired = state.value != activeValue
    Toggle(
        label = label,
        checked = state.value,
        onCheckedChanged = {
            state.value = it
            NativeAdrenoTweaks.setTweak(key, it)
        },
        description = if (restartRequired) "$description\n\n${tr("Restart the app to apply this change.")}" else description,
        enabled = enabled,
    )
}

@Composable
fun AdrenoTweaksScreen(navigateBack: () -> Unit) {
    val cpuLayout = remember { NativeAdrenoTweaks.getCpuLayout() }
    val bigCoreAffinity = rememberTweakState(NativeAdrenoTweaks.BIG_CORE_AFFINITY)
    val helperThreadDemotion = rememberTweakState(NativeAdrenoTweaks.HELPER_THREAD_DEMOTION)
    val limitCompileThreads = rememberTweakState(NativeAdrenoTweaks.LIMIT_COMPILE_THREADS)
    val skipRobustBufferAccess = rememberTweakState(NativeAdrenoTweaks.SKIP_DEVICE_ROBUST_BUFFER_ACCESS)
    val byRegionBarriers = rememberTweakState(NativeAdrenoTweaks.BY_REGION_BARRIERS)
    val fp16ShaderRewrite = rememberTweakState(NativeAdrenoTweaks.FP16_SHADER_REWRITE)
    val threadCpuLog = rememberTweakState(NativeAdrenoTweaks.THREAD_CPU_LOG)

    ScreenContent(
        appBarText = tr("Adreno tweaks"),
        navigateBack = navigateBack,
    ) {
        Text(
            text = tr("Detected CPU layout") + ": " + cpuLayout,
            fontSize = 14.sp,
            modifier = Modifier.padding(8.dp),
        )

        Header(tr("CPU"))
        TweakToggle(
            key = NativeAdrenoTweaks.BIG_CORE_AFFINITY,
            label = tr("Big core thread placement"),
            description = tr("Runs the GPU thread on the fastest core and each emulated PPC core on its own big core. Does nothing on CPUs without big and little cores."),
            state = bigCoreAffinity,
        )
        TweakToggle(
            key = NativeAdrenoTweaks.HELPER_THREAD_DEMOTION,
            label = tr("Keep helper threads off big cores"),
            description = tr("Shader compile, cache and file helper threads run on the little cores at lower priority so they cannot steal time from emulation. Needs big core thread placement."),
            state = helperThreadDemotion,
            enabled = bigCoreAffinity.value,
        )
        TweakToggle(
            key = NativeAdrenoTweaks.LIMIT_COMPILE_THREADS,
            label = tr("Limit shader compile threads"),
            description = tr("Uses at most one compile thread per little core instead of up to 8, for both the in-game compile pool and the shader cache loader at game start. Less thrashing and lower peak RAM. Needs the two options above."),
            state = limitCompileThreads,
            enabled = bigCoreAffinity.value && helperThreadDemotion.value,
        )

        Header(tr("GPU"))
        Toggle(
            label = tr("Full sync at GX2DrawDone"),
            description = tr("When a game asks for synchronization, the emulated CPU waits for the GPU to finish all work. More accurate, but it leaves the CPU cores idle while the GPU works. Turning it off can raise performance but may cause flickering or glitches. Applies the next time a game starts."),
            initialCheckedState = NativeSettings::getGX2DrawDoneSync,
            onCheckedChanged = NativeSettings::setGX2DrawDoneSync,
        )
        TweakToggle(
            key = NativeAdrenoTweaks.SKIP_DEVICE_ROBUST_BUFFER_ACCESS,
            label = tr("Skip device-wide robust buffer access (experimental)"),
            description = tr("Without VK_EXT_pipeline_robustness (Turnip) Cemu checks buffer bounds in every pipeline. Turning this on removes those checks, which may be faster, but games that read out of bounds can show garbage or crash the GPU driver."),
            state = skipRobustBufferAccess,
        )
        TweakToggle(
            key = NativeAdrenoTweaks.BY_REGION_BARRIERS,
            label = tr("By-region barriers (experimental)"),
            description = tr("Adds the by-region flag to barriers between render passes. May be a no-op on some drivers."),
            state = byRegionBarriers,
        )
        TweakToggle(
            key = NativeAdrenoTweaks.FP16_SHADER_REWRITE,
            label = tr("FP16 shader rewrite (experimental)"),
            description = tr("Rewrites generated shaders to use 16-bit floats on Adreno GPUs that support it. Shaders that fail to compile fall back to the original. Clear the shader caches after changing this."),
            state = fp16ShaderRewrite,
        )

        Header(tr("Diagnostics"))
        TweakToggle(
            key = NativeAdrenoTweaks.THREAD_CPU_LOG,
            label = tr("Log per-thread CPU usage"),
            description = tr("Every 5 seconds, writes the busiest threads and the core they ran on to the log, for the first 10 minutes of a game. Use it to see what loads each core."),
            state = threadCpuLog,
        )
    }
}
