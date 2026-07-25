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

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.material3.MaterialTheme

/**
 * 应用根背景：动态渐变 + 噪点暗示
 *
 * 渐变让毛玻璃表面（GlassCard / GlassNavBar）有"被磨砂的内容"可透出，
 * 是毛玻璃视觉效果成立的前提。颜色跟随主题。
 */
@Composable
fun NexusBackground(
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val scheme = MaterialTheme.colorScheme
    val isLandscape = LocalConfiguration.current.screenWidthDp > LocalConfiguration.current.screenHeightDp

    val brush = Brush.linearGradient(
        colors = listOf(
            scheme.primaryContainer,
            scheme.background,
            scheme.surfaceVariant,
        ),
        start = androidx.compose.ui.geometry.Offset(0f, 0f),
        end = androidx.compose.ui.geometry.Offset(
            if (isLandscape) 2000f else 800f,
            if (isLandscape) 1200f else 2000f,
        ),
    )

    Box(
        modifier = modifier
            .fillMaxSize()
            .background(scheme.background)
            .background(brush),
    ) {
        content()
    }
}
