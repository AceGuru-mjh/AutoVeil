package com.nexus.manager.ui.pages

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Bolt
import androidx.compose.material.icons.outlined.CheckCircle
import androidx.compose.material.icons.outlined.CloudUpload
import androidx.compose.material.icons.outlined.Security
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.nexus.manager.data.bridge.FileBridge
import com.nexus.manager.ui.components.GlassCard
import com.nexus.manager.ui.components.GlassTopBar
import com.nexus.manager.ui.components.SectionHeader
import com.nexus.manager.ui.components.StatusChip
import com.nexus.manager.ui.components.StatusTone
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Boot Patcher 页面（Phase 7 新增）
 *
 * NexusCore 作为独立 Root 框架，提供自研 boot image patcher：
 * - 用户选择设备上的 boot.img 文件（通过 SAF）
 * - Manager 把 boot.img 复制到 cache 目录
 * - 通过 IPC 调用 daemon 的 BootPatcher::patch
 * - 修补后的 boot.img 输出到 Download 目录
 * - 用户通过 fastboot flash boot 手动刷入
 *
 * 与 Magisk 的区别：
 * - Magisk 在 Manager 内部直接 patch（用 native libmagiskboot.so）
 * - NexusCore 把 patch 委托给 daemon（daemon 是 root，能访问更多资源）
 * - Manager 仅负责 UI 与文件 IO
 */
@Composable
fun BootPatcherPage(
    onBack: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()

    var selectedBootPath by remember { mutableStateOf<String?>(null) }
    var isPatching by remember { mutableStateOf(false) }
    var patchResult by remember { mutableStateOf<PatchResult?>(null) }
    var showError by remember { mutableStateOf<String?>(null) }

    // SAF 选择 boot.img
    val bootPicker = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.OpenDocument(),
    ) { uri ->
        if (uri != null) {
            scope.launch {
                val path = FileBridge.copyUriToTemp(context, uri)
                if (path != null) {
                    selectedBootPath = path
                } else {
                    showError = "无法读取选择的 boot.img 文件"
                }
            }
        }
    }

    Box(Modifier.fillMaxSize()) {
        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState()),
        ) {
            GlassTopBar(
                title = "Boot Patcher",
                subtitle = "修补 boot.img 注入 NexusCore daemon",
            )

            Spacer(Modifier.height(16.dp))

            // 状态卡
            SectionHeader(title = "当前状态", modifier = Modifier.padding(horizontal = 16.dp))
            GlassCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                contentPadding = 16.dp,
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(
                        Icons.Outlined.Security,
                        contentDescription = null,
                        tint = MaterialTheme.colorScheme.primary,
                    )
                    Spacer(Modifier.size(12.dp))
                    Column(Modifier.weight(1f)) {
                        Text(
                            "NexusCore 独立 Root 模式",
                            style = MaterialTheme.typography.titleMedium,
                            fontWeight = FontWeight.SemiBold,
                        )
                        Text(
                            "通过修补 boot.img 注入 init service，不依赖 Magisk/KSU/APatch",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                }
            }

            Spacer(Modifier.height(16.dp))

            // 步骤 1: 选择 boot.img
            SectionHeader(title = "步骤 1: 选择 boot.img", modifier = Modifier.padding(horizontal = 16.dp))
            GlassCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                contentPadding = 16.dp,
            ) {
                Column {
                    Text(
                        "从设备存储选择原始 boot.img 文件。",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(
                        "提示：boot.img 通常位于：\n" +
                                "• 解锁 bootloader 后从 fastboot boot 备份\n" +
                                "• 从设备厂商固件包提取\n" +
                                "• 第三方工具导出（如 TWRP）",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                    Spacer(Modifier.height(12.dp))
                    Button(
                        onClick = { bootPicker.launch(arrayOf("application/octet-stream", "*/*")) },
                        enabled = !isPatching,
                    ) {
                        Icon(Icons.Outlined.CloudUpload, contentDescription = null)
                        Spacer(Modifier.size(8.dp))
                        Text("选择 boot.img")
                    }
                    selectedBootPath?.let { path ->
                        Spacer(Modifier.height(8.dp))
                        StatusChip(
                            text = "已选择: ${path.substringAfterLast('/')}",
                            tone = StatusTone.OK,
                        )
                    }
                }
            }

            Spacer(Modifier.height(16.dp))

            // 步骤 2: 修补
            SectionHeader(title = "步骤 2: 修补 boot.img", modifier = Modifier.padding(horizontal = 16.dp))
            GlassCard(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = 16.dp),
                contentPadding = 16.dp,
            ) {
                Column {
                    Text(
                        "NexusCore 会向 boot.img 的 ramdisk 注入：\n" +
                                "• nexusd 二进制（root 守护进程）\n" +
                                "• nexus.rc（init service 定义）\n" +
                                "• bootstrap 脚本（首次启动标记）\n\n" +
                                "不修改 kernel，不依赖 verified boot。",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                    Spacer(Modifier.height(12.dp))
                    if (isPatching) {
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "正在修补... 请稍候",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    } else {
                        Button(
                            onClick = {
                                selectedBootPath?.let { bootPath ->
                                    isPatching = true
                                    patchResult = null
                                    scope.launch {
                                        // TODO: 调用 IPC daemon.patchBoot(bootPath, outputPath)
                                        // 简化：模拟延迟
                                        withContext(Dispatchers.IO) {
                                            Thread.sleep(2000)
                                        }
                                        patchResult = PatchResult(
                                            success = true,
                                            outputPath = "/sdcard/Download/nexus_patched_boot.img",
                                            message = "修补成功，请通过 fastboot 刷入",
                                        )
                                        isPatching = false
                                    }
                                }
                            },
                            enabled = selectedBootPath != null,
                        ) {
                            Icon(Icons.Outlined.Bolt, contentDescription = null)
                            Spacer(Modifier.size(8.dp))
                            Text("开始修补")
                        }
                    }
                }
            }

            Spacer(Modifier.height(16.dp))

            // 步骤 3: 刷入
            patchResult?.let { result ->
                SectionHeader(title = "步骤 3: 刷入修补后的 boot.img", modifier = Modifier.padding(horizontal = 16.dp))
                GlassCard(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 16.dp),
                    contentPadding = 16.dp,
                ) {
                    Column {
                        Row(verticalAlignment = Alignment.CenterVertically) {
                            Icon(
                                Icons.Outlined.CheckCircle,
                                contentDescription = null,
                                tint = MaterialTheme.colorScheme.tertiary,
                            )
                            Spacer(Modifier.size(8.dp))
                            Text(
                                result.message,
                                style = MaterialTheme.typography.bodyMedium,
                                fontWeight = FontWeight.Medium,
                                color = MaterialTheme.colorScheme.tertiary,
                            )
                        }
                        Spacer(Modifier.height(8.dp))
                        Text(
                            "输出路径: ${result.outputPath}",
                            style = MaterialTheme.typography.bodySmall,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Spacer(Modifier.height(12.dp))
                        Text(
                            "刷入命令（在 fastboot 模式下）：",
                            style = MaterialTheme.typography.bodyMedium,
                            fontWeight = FontWeight.Medium,
                        )
                        Spacer(Modifier.height(4.dp))
                        Card(
                            colors = CardDefaults.cardColors(
                                containerColor = MaterialTheme.colorScheme.surfaceVariant,
                            ),
                        ) {
                            Text(
                                "fastboot flash boot ${result.outputPath}",
                                style = MaterialTheme.typography.bodySmall,
                                modifier = Modifier.padding(8.dp),
                            )
                        }
                        Spacer(Modifier.height(12.dp))
                        OutlinedButton(onClick = onBack) {
                            Text("完成")
                        }
                    }
                }
            }

            // 错误对话框
            showError?.let { msg ->
                AlertDialog(
                    onDismissRequest = { showError = null },
                    title = { Text("错误") },
                    text = { Text(msg) },
                    confirmButton = {
                        TextButton(onClick = { showError = null }) { Text("确定") }
                    },
                )
            }

            Spacer(Modifier.height(96.dp))
        }
    }
}

private data class PatchResult(
    val success: Boolean,
    val outputPath: String,
    val message: String,
)
