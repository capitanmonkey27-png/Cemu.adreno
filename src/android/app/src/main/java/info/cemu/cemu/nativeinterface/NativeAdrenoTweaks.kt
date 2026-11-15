package info.cemu.cemu.nativeinterface

object NativeAdrenoTweaks {
    // keys of adreno_tweaks.txt
    const val BIG_CORE_AFFINITY = "bigCoreAffinity"
    const val HELPER_THREAD_DEMOTION = "helperThreadDemotion"
    const val LIMIT_COMPILE_THREADS = "limitCompileThreads"
    const val BY_REGION_BARRIERS = "byRegionBarriers"
    const val FP16_SHADER_REWRITE = "fp16ShaderRewrite"
    const val SKIP_DEVICE_ROBUST_BUFFER_ACCESS = "skipDeviceRobustBufferAccess"
    const val THREAD_CPU_LOG = "threadCpuLog"

    /** Value stored in adreno_tweaks.txt (what the next app start will use). */
    @JvmStatic
    external fun getSavedTweak(key: String): Boolean

    /** Value the running process is using. Differs from the saved value until the app is restarted. */
    @JvmStatic
    external fun isTweakActive(key: String): Boolean

    /** Saves the value to adreno_tweaks.txt immediately. */
    @JvmStatic
    external fun setTweak(key: String, value: Boolean)

    @JvmStatic
    external fun getCpuLayout(): String
}
