package com.nexus.manager.data.bridge

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import java.io.File
import java.io.OutputStream

/**
 * 文件桥接工具
 *
 * 处理 UI 与 Daemon 之间的文件传递：
 * - [copyUriToTemp]：将 SAF 选取的 ZIP 复制到 app cache 目录，返回绝对路径给 Daemon
 * - [writeTextToUri]：将文本写入 SAF 创建的文档（日志导出）
 *
 * 设计说明：
 * - Daemon 以 root 运行，可读取任意路径；Manager 只需把文件放到 cache 即可
 * - 导出走 SAF（createDocument），无需申请存储权限，兼容 Android 10+ scoped storage
 */
object FileBridge {

    private const val TEMP_DIR = "nexus_install"
    private const val MAX_INSTALL_ZIP = 256L * 1024 * 1024  // 256 MiB

    /**
     * 复制 SAF URI 到 app cache 临时文件。
     * @return 临时文件绝对路径，失败返回 null
     */
    fun copyUriToTemp(context: Context, uri: Uri): String? {
        val name = queryDisplayName(context, uri) ?: "nexus_module_${System.currentTimeMillis()}.zip"
        val dir = File(context.cacheDir, TEMP_DIR).apply { mkdirs() }
        // 清理超过 1 小时的旧临时文件
        dir.listFiles()?.forEach { f ->
            if (f.lastModified() < System.currentTimeMillis() - 3_600_000) f.delete()
        }
        val target = File(dir, name)
        return try {
            context.contentResolver.openInputStream(uri)?.use { input ->
                target.outputStream().use { output ->
                    var copied = 0L
                    val buf = ByteArray(64 * 1024)
                    while (true) {
                        val n = input.read(buf)
                        if (n <= 0) break
                        copied += n
                        if (copied > MAX_INSTALL_ZIP) {
                            target.delete()
                            return null
                        }
                        output.write(buf, 0, n)
                    }
                }
            } ?: return null
            target.absolutePath
        } catch (e: Exception) {
            target.delete()
            null
        }
    }

    /**
     * 将文本写入 SAF 文档 Uri（日志导出用）。
     * @return 是否写入成功
     */
    fun writeTextToUri(context: Context, uri: Uri, text: String): Boolean {
        return try {
            context.contentResolver.openOutputStream(uri, "w")?.use { os ->
                os.bufferedWriter(Charsets.UTF_8).use { w -> w.write(text) }
                true
            } ?: false
        } catch (e: Exception) {
            false
        }
    }

    private fun queryDisplayName(context: Context, uri: Uri): String? {
        return runCatching {
            context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
                ?.use { c ->
                    if (c.moveToFirst() && !c.isNull(0)) c.getString(0) else null
                }
        }.getOrNull()
    }
}
