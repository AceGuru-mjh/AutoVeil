package com.nexus.manager.ui

import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Dashboard
import androidx.compose.material.icons.filled.Extension
import androidx.compose.material.icons.filled.List
import androidx.compose.material.icons.filled.Security
import androidx.compose.material.icons.filled.Settings
import androidx.compose.material.icons.outlined.Dashboard
import androidx.compose.material.icons.outlined.Extension
import androidx.compose.material.icons.outlined.List
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.navigation.NavType
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import androidx.navigation.navArgument
import com.nexus.manager.ui.components.GlassNavBar
import com.nexus.manager.ui.components.GlassNavItem
import com.nexus.manager.ui.components.LocalSnackbar
import com.nexus.manager.ui.components.NexusBackground
import com.nexus.manager.ui.components.SnackbarController
import com.nexus.manager.ui.components.SuRequestDialog
import com.nexus.manager.ui.nav.Routes
import com.nexus.manager.ui.pages.DashboardPage
import com.nexus.manager.ui.pages.LogsPage
import com.nexus.manager.ui.pages.ModuleDetailPage
import com.nexus.manager.ui.pages.ModulesPage
import com.nexus.manager.ui.pages.SettingsPage
import com.nexus.manager.ui.pages.SuperUserPage
import com.nexus.manager.ui.pages.BootPatcherPage
import com.nexus.manager.NexusApp
import androidx.lifecycle.compose.collectAsStateWithLifecycle

/**
 * 应用根 Composable
 *
 * 结构：
 *   NexusBackground (渐变背景，让毛玻璃有内容可透出)
 *     └─ Scaffold (透明容器)
 *          ├─ content: NavHost (Dashboard/Modules/SuperUser/Logs/Settings + ModuleDetail)
 *          ├─ bottomBar: GlassNavBar (仅主页签显示)
 *          └─ snackbarHost: 全局 Snackbar
 */
@Composable
fun NexusRoot() {
    val app = NexusApp.get()
    val themeMode by app.settings.themeMode.collectAsStateWithLifecycle(initialValue = com.nexus.manager.ui.theme.ThemeMode.SYSTEM)
    val dynamicColor by app.settings.dynamicColor.collectAsStateWithLifecycle(initialValue = false)

    com.nexus.manager.ui.theme.NexusTheme(mode = themeMode, dynamicColor = dynamicColor) {
        NexusBackground {
            RootContent()
        }
    }
}

@Composable
private fun RootContent() {
    val navController = rememberNavController()
    val snackbarHostState = remember { SnackbarHostState() }
    val snackbarController = remember { SnackbarController(snackbarHostState) }

    val backStack by navController.currentBackStackEntryAsState()
    val currentRoute = backStack?.destination?.route

    val showBottomBar = currentRoute in topLevelRoutes

    CompositionLocalProvider(LocalSnackbar provides snackbarController) {
        Scaffold(
            modifier = Modifier.fillMaxSize(),
            containerColor = Color.Transparent,
            snackbarHost = { SnackbarHost(snackbarHostState) },
            bottomBar = {
                if (showBottomBar) {
                    GlassNavBar(
                        items = navItems,
                        currentRoute = currentRoute ?: Routes.DASHBOARD,
                        onItemSelected = { item ->
                            if (currentRoute != item.route) {
                                navController.navigate(item.route) {
                                    popUpTo(Routes.DASHBOARD) { saveState = true }
                                    launchSingleTop = true
                                    restoreState = true
                                }
                            }
                        },
                    )
                }
            },
        ) { padding ->
            NavHost(
                navController = navController,
                startDestination = Routes.DASHBOARD,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(getContentPadding(padding, showBottomBar)),
            ) {
                composable(Routes.DASHBOARD) {
                    DashboardPage(
                        onNavigateToModules = { navController.navigate(Routes.MODULES) },
                        onNavigateToLogs = { navController.navigate(Routes.LOGS) },
                    )
                }
                composable(Routes.MODULES) {
                    ModulesPage(onOpenDetail = { id ->
                        navController.navigate(Routes.moduleDetail(id))
                    })
                }
                composable(Routes.SUPERUSER) { SuperUserPage() }
                composable(Routes.LOGS) { LogsPage() }
                composable(Routes.SETTINGS) {
                    SettingsPage(onNavigateToBootPatcher = { navController.navigate(Routes.BOOT_PATCHER) })
                }
                composable(
                    route = Routes.MODULE_DETAIL,
                    arguments = listOf(navArgument("moduleId") { type = NavType.StringType }),
                ) {
                    ModuleDetailPage(onBack = { navController.popBackStack() })
                }
                // Phase 7: Boot Patcher
                composable(Routes.BOOT_PATCHER) {
                    BootPatcherPage(onBack = { navController.popBackStack() })
                }
            }
        }

        // 全局 Su 请求对话框：任何页面收到 Daemon 推送都会弹出
        SuRequestDialog()
    }
}

/**
 * Scaffold 的 innerPadding 包含底部导航栏高度；当不显示底栏时（详情页）丢弃底部 inset。
 *
 * 顶部状态栏 inset 由各页面的 [GlassTopBar] 自行处理（其容器透明，延伸到状态栏下方，
 * 标题内边距已对齐状态栏），因此这里始终丢弃 top inset，避免标题被下推两次（双 inset）。
 */
private fun getContentPadding(padding: PaddingValues, showBottomBar: Boolean): PaddingValues {
    val start = padding.calculateLeftPadding(androidx.compose.ui.unit.LayoutDirection.Ltr)
    val end = padding.calculateRightPadding(androidx.compose.ui.unit.LayoutDirection.Ltr)
    val bottom = if (showBottomBar) padding.calculateBottomPadding() else 0.dp
    return PaddingValues(start = start, end = end, top = 0.dp, bottom = bottom)
}

private val topLevelRoutes = setOf(
    Routes.DASHBOARD,
    Routes.MODULES,
    Routes.SUPERUSER,
    Routes.LOGS,
    Routes.SETTINGS,
)

private val navItems = listOf(
    GlassNavItem(
        route = Routes.DASHBOARD,
        label = "状态",
        icon = Icons.Outlined.Dashboard,
        selectedIcon = Icons.Filled.Dashboard,
    ),
    GlassNavItem(
        route = Routes.MODULES,
        label = "模块",
        icon = Icons.Outlined.Extension,
        selectedIcon = Icons.Filled.Extension,
    ),
    GlassNavItem(
        route = Routes.SUPERUSER,
        label = "超级用户",
        icon = Icons.Outlined.Security,
        selectedIcon = Icons.Filled.Security,
    ),
    GlassNavItem(
        route = Routes.LOGS,
        label = "日志",
        icon = Icons.Outlined.List,
        selectedIcon = Icons.Filled.List,
    ),
    GlassNavItem(
        route = Routes.SETTINGS,
        label = "设置",
        icon = Icons.Outlined.Settings,
        selectedIcon = Icons.Filled.Settings,
    ),
)
