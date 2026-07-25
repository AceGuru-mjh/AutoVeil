/*
 * AutoVeil / NexusCore
 * Copyright (C) 2026 AutoVeil / NexusCore Contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

package com.nexus.manager.ui.components

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Shape
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.nexus.manager.ui.theme.GlassCardShape
import com.nexus.manager.ui.theme.LocalGlassColors

/**
 * 毛玻璃表面 —— NexusCore 核心 UI 容器
 *
 * 实现策略：
 * 1. 半透明底色（[LocalGlassColors.surface]），让底层内容隐约透出
 * 2. 顶部高光渐变（模拟玻璃反射），增加"磨砂"质感
 * 3. 细描边（[LocalGlassColors.stroke]），强化玻璃边缘
 * 4. 可选 [blurRadius]：Android 12+ 使用 Modifier.blur 对自身绘制做高斯模糊，
 *    叠加在半透明底色上产生真实的"磨砂"观感
 *
 * 注：Android 上"模糊背景内容"需要 RenderEffect 捕获父层，开销大且兼容性差。
 * 本组件采用业界主流的"半透明 + 渐变 + 描边 + 自身模糊"组合，视觉效果接近
 * 原生毛玻璃且性能可控。
 */
@Composable
fun GlassSurface(
    modifier: Modifier = Modifier,
    shape: Shape = GlassCardShape,
    blurRadius: Dp = 0.dp,
    contentPadding: Dp = 0.dp,
    content: @Composable BoxScope.() -> Unit,
) {
    val glass = LocalGlassColors.current
    val baseModifier = modifier
        .clip(shape)
        .background(glass.surface)
        .then(
            // 顶部高光渐变：模拟环境光从上方照射玻璃
            Modifier.drawWithContent {
                drawContent()
                val brush = Brush.linearGradient(
                    colors = listOf(
                        glass.overlay,
                        Color.Transparent,
                        Color.Transparent,
                    ),
                    start = Offset(0f, 0f),
                    end = Offset(0f, size.height * 0.6f),
                )
                drawRect(brush)
            }
        )
        .border(BorderStroke(1.dp, glass.stroke), shape)

    Box(
        modifier = if (blurRadius > 0.dp) {
            // 自身绘制模糊：让半透明底色产生柔和的磨砂粒度
            baseModifier.blur(blurRadius)
        } else baseModifier,
    ) {
        Box(Modifier.padding(contentPadding)) { content() }
    }
}

/**
 * 标准毛玻璃卡片：圆角 24dp + 16dp 内边距
 */
@Composable
fun GlassCard(
    modifier: Modifier = Modifier,
    shape: Shape = GlassCardShape,
    contentPadding: Dp = 16.dp,
    content: @Composable BoxScope.() -> Unit,
) {
    GlassSurface(
        modifier = modifier,
        shape = shape,
        contentPadding = contentPadding,
        content = content,
    )
}

/**
 * 全屏毛玻璃遮罩：用于对话框/底部弹窗的背景 scrim
 */
@Composable
fun GlassScrim(
    modifier: Modifier = Modifier,
    onClick: (() -> Unit)? = null,
    content: @Composable BoxScope.() -> Unit,
) {
    val glass = LocalGlassColors.current
    val interaction = remember { MutableInteractionSource() }
    Box(
        modifier = modifier
            .fillMaxSize()
            .background(glass.scrim)
            .then(
                if (onClick != null) {
                    Modifier.clickable(
                        interactionSource = interaction,
                        indication = null,
                        onClick = onClick,
                    )
                } else Modifier
            ),
        content = content,
    )
}
