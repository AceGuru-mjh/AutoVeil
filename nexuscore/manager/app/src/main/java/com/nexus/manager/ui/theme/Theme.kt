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

package com.nexus.manager.ui.theme

import android.app.Activity
import android.os.Build
import androidx.compose.foundation.isSystemInDarkTheme
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.dynamicDarkColorScheme
import androidx.compose.material3.dynamicLightColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.toArgb
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalView
import androidx.core.view.WindowCompat

// ============ 主题模式 ============
enum class ThemeMode { SYSTEM, LIGHT, DARK }

// ============ 毛玻璃颜色集 ============
data class GlassColors(
    val surface: Color,
    val stroke: Color,
    val overlay: Color,
    val scrim: Color,
)

val LocalGlassColors = compositionLocalOf {
    GlassColors(
        surface = GlassSurfaceDark,
        stroke = GlassStrokeDark,
        overlay = GlassOverlayDark,
        scrim = Color.Black.copy(alpha = 0.4f),
    )
}

// ============ 浅色 / 深色 M3 调色板 ============
private val NexusLightColors = lightColorScheme(
    primary = NexusPrimaryLight,
    onPrimary = NexusOnPrimaryLight,
    primaryContainer = NexusPrimaryContainerLight,
    onPrimaryContainer = NexusOnPrimaryContainerLight,
    secondary = NexusSecondaryLight,
    onSecondary = NexusOnSecondaryLight,
    secondaryContainer = NexusSecondaryContainerLight,
    onSecondaryContainer = NexusOnSecondaryContainerLight,
    tertiary = NexusTertiaryLight,
    onTertiary = NexusOnTertiaryLight,
    tertiaryContainer = NexusTertiaryContainerLight,
    onTertiaryContainer = NexusOnTertiaryContainerLight,
    error = NexusErrorLight,
    onError = NexusOnErrorLight,
    errorContainer = NexusErrorContainerLight,
    onErrorContainer = NexusOnErrorContainerLight,
    background = NexusBackgroundLight,
    onBackground = NexusOnBackgroundLight,
    surface = NexusSurfaceLight,
    onSurface = NexusOnSurfaceLight,
    surfaceVariant = NexusSurfaceVariantLight,
    onSurfaceVariant = NexusOnSurfaceVariantLight,
    outline = NexusOutlineLight,
    outlineVariant = NexusOutlineVariantLight,
)

private val NexusDarkColors = darkColorScheme(
    primary = NexusPrimaryDark,
    onPrimary = NexusOnPrimaryDark,
    primaryContainer = NexusPrimaryContainerDark,
    onPrimaryContainer = NexusOnPrimaryContainerDark,
    secondary = NexusSecondaryDark,
    onSecondary = NexusOnSecondaryDark,
    secondaryContainer = NexusSecondaryContainerDark,
    onSecondaryContainer = NexusOnSecondaryContainerDark,
    tertiary = NexusTertiaryDark,
    onTertiary = NexusOnTertiaryDark,
    tertiaryContainer = NexusTertiaryContainerDark,
    onTertiaryContainer = NexusOnTertiaryContainerDark,
    error = NexusErrorDark,
    onError = NexusOnErrorDark,
    errorContainer = NexusErrorContainerDark,
    onErrorContainer = NexusOnErrorContainerDark,
    background = NexusBackgroundDark,
    onBackground = NexusOnBackgroundDark,
    surface = NexusSurfaceDark,
    onSurface = NexusOnSurfaceDark,
    surfaceVariant = NexusSurfaceVariantDark,
    onSurfaceVariant = NexusOnSurfaceVariantDark,
    outline = NexusOutlineDark,
    outlineVariant = NexusOutlineVariantDark,
)

private val LightGlassColors = GlassColors(
    surface = GlassSurfaceLight,
    stroke = GlassStrokeLight,
    overlay = GlassOverlayLight,
    scrim = Color.Black.copy(alpha = 0.25f),
)

private val DarkGlassColors = GlassColors(
    surface = GlassSurfaceDark,
    stroke = GlassStrokeDark,
    overlay = GlassOverlayDark,
    scrim = Color.Black.copy(alpha = 0.5f),
)

/**
 * NexusCore 主题入口
 *
 * @param mode 主题模式（系统/浅色/深色）
 * @param dynamicColor 是否启用 Material You 动态颜色（Android 12+）
 */
@Composable
fun NexusTheme(
    mode: ThemeMode = ThemeMode.SYSTEM,
    dynamicColor: Boolean = false,
    content: @Composable () -> Unit,
) {
    val systemDark = isSystemInDarkTheme()
    val isDark = when (mode) {
        ThemeMode.SYSTEM -> systemDark
        ThemeMode.LIGHT -> false
        ThemeMode.DARK -> true
    }

    val context = LocalContext.current
    val colorScheme = when {
        dynamicColor && Build.VERSION.SDK_INT >= Build.VERSION_CODES.S ->
            if (isDark) dynamicDarkColorScheme(context) else dynamicLightColorScheme(context)
        isDark -> NexusDarkColors
        else -> NexusLightColors
    }
    val glass = if (isDark) DarkGlassColors else LightGlassColors

    val view = LocalView.current
    if (!view.isInEditMode) {
        SideEffect {
            val window = (view.context as Activity).window
            // 透明状态栏 / 导航栏，让毛玻璃能透出底层内容
            WindowCompat.setDecorFitsSystemWindows(window, false)
            val controller = WindowCompat.getInsetsController(window, view)
            controller.isAppearanceLightStatusBars = !isDark
            controller.isAppearanceLightNavigationBars = !isDark
            window.statusBarColor = Color.Transparent.toArgb()
            window.navigationBarColor = Color.Transparent.toArgb()
        }
    }

    CompositionLocalProvider(LocalGlassColors provides glass) {
        MaterialTheme(
            colorScheme = colorScheme,
            typography = NexusTypography,
            shapes = NexusShapes,
            content = content,
        )
    }
}
