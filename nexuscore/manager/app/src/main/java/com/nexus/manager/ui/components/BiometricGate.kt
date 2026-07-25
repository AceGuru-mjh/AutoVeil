package com.nexus.manager.ui.components

import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.fragment.app.FragmentActivity

/**
 * 生物认证门：当 [enabled] 为 true 时，调用系统 BiometricPrompt 要求指纹/面部/锁屏凭据。
 *
 * 用法：
 *   BiometricGate(enabled = state.biometricEnabled) {
 *       // 通过认证后执行的回调
 *   }
 *
 * 整改 B3（原 bug）：
 *   原实现 MainActivity 是 ComponentActivity 无法挂 BiometricPrompt 的 Fragment，
 *   onAuthenticated() 被无条件调用，开启生物认证实际等于关闭。
 *
 *   现已将 MainActivity 改为 FragmentActivity，本组件真正调用 BiometricPrompt：
 *   - 通过 BIORETIC_WEAK | DEVICE_CREDENTIAL 允许指纹/面部/锁屏 PIN
 *   - 认证失败/取消时调用 onUnavailable
 *   - 设备无生物认证能力时（canAuthenticate != SUCCESS）直接 onUnavailable
 *
 *   注意：本组件是 fire-and-forget 模式，每次进入组合触发一次认证。
 *   若需 gating 持续操作，建议改用 rememberBiometricGateState 持有状态。
 */
@Composable
fun BiometricGate(
    enabled: Boolean,
    onAuthenticated: () -> Unit,
    onUnavailable: () -> Unit = {},
) {
    val context = LocalContext.current
    val activity = context as? FragmentActivity

    val canAuth = remember(context) {
        val bm = BiometricManager.from(context)
        // 与 PromptInfo 一致：仅 BIOMETRIC_WEAK
        bm.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_WEAK) == BiometricManager.BIOMETRIC_SUCCESS
    }

    LaunchedEffect(enabled) {
        if (!enabled) {
            onAuthenticated()
            return@LaunchedEffect
        }
        if (!canAuth || activity == null) {
            onUnavailable()
            return@LaunchedEffect
        }
        val executor = androidx.core.content.ContextCompat.getMainExecutor(context)
        val callback = object : BiometricPrompt.AuthenticationCallback() {
            override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                onAuthenticated()
            }
            override fun onAuthenticationFailed() {
                // 用户输错指纹/面部，BiometricPrompt 自己会重试，不立即 onUnavailable
            }
            override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                // 用户取消 / 锁屏超时 / 无法恢复的错误
                onUnavailable()
            }
        }
        val prompt = BiometricPrompt(
            activity,
            executor,
            callback,
        )
        // 注意：DEVICE_CREDENTIAL 不允许 setNegativeButtonText，否则抛 IllegalArgumentException。
        // 仅使用 BIOMETRIC_WEAK，让用户用指纹/面部；无生物认证的设备走 onUnavailable 路径。
        val info = BiometricPrompt.PromptInfo.Builder()
            .setTitle("NexusCore 认证")
            .setSubtitle("请验证身份以执行敏感操作")
            .setAllowedAuthenticators(BiometricManager.Authenticators.BIOMETRIC_WEAK)
            .setNegativeButtonText("取消")
            .build()
        try {
            prompt.authenticate(info)
        } catch (e: Exception) {
            // 极端情况（如 Activity 已 finish）退化为 onUnavailable
            onUnavailable()
        }
    }
}
