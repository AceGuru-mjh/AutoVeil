/*
 * AutoVeil / NexusCore
 * Copyright (c) 2026 AutoVeil / NexusCore Contributors
 *
 * Licensed under the MIT License with Non-Commercial Clause (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License in the LICENSE file at the project root.
 *
 * Commercial use requires a separate written license from the copyright holders.
 */

package com.nexus.manager.ui.components

import androidx.biometric.BiometricManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext

/**
 * 生物认证门：当 [enabled] 为 true 时，调用系统 BiometricPrompt 要求指纹/面部/锁屏凭据。
 *
 * 用法：
 *   BiometricGate(enabled = state.biometricEnabled) {
 *       // 通过认证后执行的回调
 *   }
 *
 * 注意：完整的 BiometricPrompt 需要 FragmentActivity；MVP 的 MainActivity 是 ComponentActivity，
 * 此组件作为占位实现，仅检测可用性。
 * 未通过认证时直接调用 onUnavailable；后续切换到 FragmentActivity 后可启用完整 BiometricPrompt。
 */
@Composable
fun BiometricGate(
    enabled: Boolean,
    onAuthenticated: () -> Unit,
    onUnavailable: () -> Unit = {},
) {
    val context = LocalContext.current
    val canAuth = remember(context) {
        val bm = BiometricManager.from(context)
        bm.canAuthenticate(
            BiometricManager.Authenticators.BIOMETRIC_WEAK or
                BiometricManager.Authenticators.DEVICE_CREDENTIAL
        ) == BiometricManager.BIOMETRIC_SUCCESS
    }

    LaunchedEffect(enabled) {
        if (!enabled) {
            onAuthenticated()
        } else if (!canAuth) {
            onUnavailable()
        } else {
            // MVP 占位：MainActivity 是 ComponentActivity，无法挂 BiometricPrompt 的 Fragment。
            // 切换到 FragmentActivity 后此处应弹出 BiometricPrompt 并按结果回调。
            onAuthenticated()
        }
    }
}
