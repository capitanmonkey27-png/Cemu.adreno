#include "JNIUtils.h"
#include "util/helpers/AndroidTweaks.h"

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeAdrenoTweaks_getSavedTweak(JNIEnv* env, [[maybe_unused]] jclass clazz, jstring key)
{
	return AndroidTweaks::GetSavedFlag(JNIUtils::FromJString(env, key).c_str());
}

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeAdrenoTweaks_isTweakActive(JNIEnv* env, [[maybe_unused]] jclass clazz, jstring key)
{
	return AndroidTweaks::IsActiveFlag(JNIUtils::FromJString(env, key).c_str());
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeAdrenoTweaks_setTweak(JNIEnv* env, [[maybe_unused]] jclass clazz, jstring key, jboolean value)
{
	AndroidTweaks::SetSavedFlag(JNIUtils::FromJString(env, key).c_str(), value);
}

extern "C" [[maybe_unused]] JNIEXPORT jstring JNICALL
Java_info_cemu_cemu_nativeinterface_NativeAdrenoTweaks_getCpuLayout(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return JNIUtils::ToJString(env, AndroidTweaks::DescribeCpuLayout());
}

extern "C" [[maybe_unused]] JNIEXPORT jstring JNICALL
Java_info_cemu_cemu_nativeinterface_NativeAdrenoTweaks_getTweaksState(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return JNIUtils::ToJString(env, AndroidTweaks::DescribeTweaksState());
}
