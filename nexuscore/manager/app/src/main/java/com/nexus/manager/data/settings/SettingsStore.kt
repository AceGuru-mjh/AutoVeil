package com.nexus.manager.data.settings

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.preferencesDataStore
import com.nexus.manager.ui.theme.ThemeMode
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.map

private val Context.settingsDataStore: DataStore<Preferences> by preferencesDataStore(
    name = "nexus_settings"
)

/**
 * 用户设置持久化（DataStore Preferences）
 *
 * - 主题模式（系统/浅色/深色）
 * - 动态颜色开关
 * - 日志最低级别（Verbose/Debug/Info/Warn/Error）
 * - 生物认证（进入敏感操作时要求指纹/面部）
 * - 框架更新通道（稳定/Beta/Canary）
 */
class SettingsStore(private val context: Context) {

    private object Keys {
        val THEME_MODE = intPreferencesKey("theme_mode")
        val DYNAMIC_COLOR = booleanPreferencesKey("dynamic_color")
        val LOG_MIN_LEVEL = intPreferencesKey("log_min_level")
        val BIOMETRIC_ENABLED = booleanPreferencesKey("biometric_enabled")
        val UPDATE_CHANNEL = intPreferencesKey("update_channel")
    }

    val themeMode: Flow<ThemeMode> = context.settingsDataStore.data.map { p ->
        p[Keys.THEME_MODE]?.let { ThemeMode.entries.getOrNull(it) } ?: ThemeMode.SYSTEM
    }

    val dynamicColor: Flow<Boolean> = context.settingsDataStore.data.map { p ->
        p[Keys.DYNAMIC_COLOR] ?: false
    }

    val logMinLevel: Flow<Int> = context.settingsDataStore.data.map { p ->
        p[Keys.LOG_MIN_LEVEL] ?: 2  // Info 默认
    }

    val biometricEnabled: Flow<Boolean> = context.settingsDataStore.data.map { p ->
        p[Keys.BIOMETRIC_ENABLED] ?: false
    }

    val updateChannel: Flow<UpdateChannel> = context.settingsDataStore.data.map { p ->
        p[Keys.UPDATE_CHANNEL]?.let { UpdateChannel.entries.getOrNull(it) } ?: UpdateChannel.STABLE
    }

    suspend fun setThemeMode(mode: ThemeMode) {
        context.settingsDataStore.edit { it[Keys.THEME_MODE] = mode.ordinal }
    }

    suspend fun setDynamicColor(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.DYNAMIC_COLOR] = enabled }
    }

    suspend fun setLogMinLevel(level: Int) {
        context.settingsDataStore.edit { it[Keys.LOG_MIN_LEVEL] = level.coerceIn(0, 4) }
    }

    suspend fun setBiometricEnabled(enabled: Boolean) {
        context.settingsDataStore.edit { it[Keys.BIOMETRIC_ENABLED] = enabled }
    }

    suspend fun setUpdateChannel(channel: UpdateChannel) {
        context.settingsDataStore.edit { it[Keys.UPDATE_CHANNEL] = channel.ordinal }
    }
}

enum class UpdateChannel { STABLE, BETA, CANARY }
