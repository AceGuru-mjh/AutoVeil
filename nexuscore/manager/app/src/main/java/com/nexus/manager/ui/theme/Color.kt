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

package com.nexus.manager.ui.theme

import androidx.compose.ui.graphics.Color

// ============ 品牌主色：NexusCore 蓝 ============
// Light scheme
val NexusPrimaryLight = Color(0xFF2E6FF6)
val NexusOnPrimaryLight = Color(0xFFFFFFFF)
val NexusPrimaryContainerLight = Color(0xFFD6E2FF)
val NexusOnPrimaryContainerLight = Color(0xFF001A40)

val NexusSecondaryLight = Color(0xFF565E71)
val NexusOnSecondaryLight = Color(0xFFFFFFFF)
val NexusSecondaryContainerLight = Color(0xFFDAE2F9)
val NexusOnSecondaryContainerLight = Color(0xFF131C2B)

val NexusTertiaryLight = Color(0xFF705574)
val NexusOnTertiaryLight = Color(0xFFFFFFFF)
val NexusTertiaryContainerLight = Color(0xFFFAD8FB)
val NexusOnTertiaryContainerLight = Color(0xFF29132E)

val NexusErrorLight = Color(0xFFBA1A1A)
val NexusOnErrorLight = Color(0xFFFFFFFF)
val NexusErrorContainerLight = Color(0xFFFFDAD6)
val NexusOnErrorContainerLight = Color(0xFF410002)

val NexusBackgroundLight = Color(0xFFFDFBFF)
val NexusOnBackgroundLight = Color(0xFF1A1B1F)
val NexusSurfaceLight = Color(0xFFFDFBFF)
val NexusOnSurfaceLight = Color(0xFF1A1B1F)
val NexusSurfaceVariantLight = Color(0xFFE0E2EC)
val NexusOnSurfaceVariantLight = Color(0xFF43474E)
val NexusOutlineLight = Color(0xFF74777F)
val NexusOutlineVariantLight = Color(0xFFC4C6CF)

// ============ 深色主色 ============
val NexusPrimaryDark = Color(0xFFA9C7FF)
val NexusOnPrimaryDark = Color(0xFF002F66)
val NexusPrimaryContainerDark = Color(0xFF00468F)
val NexusOnPrimaryContainerDark = Color(0xFFD6E2FF)

val NexusSecondaryDark = Color(0xFFBEC6DC)
val NexusOnSecondaryDark = Color(0xFF283041)
val NexusSecondaryContainerDark = Color(0xFF3E4759)
val NexusOnSecondaryContainerDark = Color(0xFFDAE2F9)

val NexusTertiaryDark = Color(0xFFDDBCE0)
val NexusOnTertiaryDark = Color(0xFF3F2844)
val NexusTertiaryContainerDark = Color(0xFF573E5B)
val NexusOnTertiaryContainerDark = Color(0xFFFAD8FB)

val NexusErrorDark = Color(0xFFFFB4AB)
val NexusOnErrorDark = Color(0xFF690005)
val NexusErrorContainerDark = Color(0xFF93000A)
val NexusOnErrorContainerDark = Color(0xFFFFDAD6)

val NexusBackgroundDark = Color(0xFF101218)
val NexusOnBackgroundDark = Color(0xFFE2E2E9)
val NexusSurfaceDark = Color(0xFF121419)
val NexusOnSurfaceDark = Color(0xFFE2E2E9)
val NexusSurfaceVariantDark = Color(0xFF43474E)
val NexusOnSurfaceVariantDark = Color(0xFFC4C6CF)
val NexusOutlineDark = Color(0xFF8E9199)
val NexusOutlineVariantDark = Color(0xFF43474E)

// ============ 状态色（语义化） ============
val StatusOkLight = Color(0xFF1F6B3A)
val StatusOkDark = Color(0xFF7CD9A1)
val StatusWarnLight = Color(0xFFB45309)
val StatusWarnDark = Color(0xFFF5BD69)
val StatusErrorLight = Color(0xFFBA1A1A)
val StatusErrorDark = Color(0xFFFFB4AB)
val StatusInfoLight = Color(0xFF2E6FF6)
val StatusInfoDark = Color(0xFFA9C7FF)

// ============ 毛玻璃层颜色（半透明 + 模糊） ============
// 玻璃面板：背景叠在内容之上，再经 Modifier.blur 实现高斯模糊
val GlassSurfaceLight = Color(0xCCFDFBFF)   // 80% 不透明
val GlassSurfaceDark = Color(0xCC121419)    // 80% 不透明
val GlassStrokeLight = Color(0x33FFFFFF)    // 顶部高光描边
val GlassStrokeDark = Color(0x33FFFFFF)
val GlassOverlayLight = Color(0x66FFFFFF)   // 强光下提亮
val GlassOverlayDark = Color(0x1AFFFFFF)
