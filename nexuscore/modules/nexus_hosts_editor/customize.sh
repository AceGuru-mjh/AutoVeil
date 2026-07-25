#!/system/bin/sh
# NexusHosts Editor 安装脚本
#
# 演示如何通过 NexusCore 模块覆盖 /system/etc/hosts。
# 这个目标比 build.prop 更稳定（Android 14+ 上路径与挂载行为都可预测），
# 用户可实际验证（ping 一个域名看是否被劫持）。
#
# 用户可在下方配置区编辑要添加的 hosts 条目。

SKIPUNZIP=0

# ============ 本地 shim（手动测试用，daemon 调用时被覆盖） ============
ui_print() { echo "$1"; }
set_perm() { chown "$2:$3" "$1" 2>/dev/null; chmod "$4" "$1" 2>/dev/null; }
set_perm_recursive() {
    dir="$1"; own="$2"; grp="$3"; dirperm="$4"; fileperm="$5"
    chown -R "$own:$grp" "$dir" 2>/dev/null
    find "$dir" -type d -exec chmod "$dirperm" {} \; 2>/dev/null
    find "$dir" -type f -exec chmod "$fileperm" {} \; 2>/dev/null
}
abort() { echo "! abort: $1" >&2; exit 1; }

# ============ 安装逻辑 ============
ui_print "- NexusHosts Editor 安装开始"
ui_print "- 模块路径: $NEXUS_MODULE_PATH"

HOSTS_FILE="$MODPATH/system/etc/hosts"
mkdir -p "$MODPATH/system/etc"

# ====== 用户配置（编辑此处添加 hosts 条目） ======
# 格式：<IP> <域名>
# 示例：屏蔽广告
ADGUARD_IP=0.0.0.0
AD_DOMAINS="
ad.example.com
ads.example.com
tracking.example.com
analytics.example.com
"
# 示例：自定义解析
# CUSTOM_ENTRIES="
# 127.0.0.1 mylocal.test
# 192.168.1.100 myserver.lan
# "
# ====================================

# 读取原始 /system/etc/hosts 作为基础
if [ -r "/system/etc/hosts" ]; then
    cp /system/etc/hosts "$HOSTS_FILE"
    ui_print "- 基于现有 /system/etc/hosts 扩展"
else
    # 创建一个最小化的默认 hosts
    cat > "$HOSTS_FILE" <<EOF
127.0.0.1 localhost
::1 ip6-localhost ip6-loopback
EOF
    ui_print "- 创建新的 /system/etc/hosts"
fi

# 追加用户配置的条目
{
    echo ""
    echo "# ===== NexusHosts Editor 注入 ====="
    echo "# generated: $(date)"
    echo ""
    echo "# 屏蔽广告"
    for domain in $AD_DOMAINS; do
        # 跳过空行和注释
        case "$domain" in
            ""|\#*) continue ;;
        esac
        echo "$ADGUARD_IP $domain"
    done
    echo ""
    echo "# ===== NexusHosts Editor END ====="
} >> "$HOSTS_FILE"

chmod 644 "$HOSTS_FILE"

ui_print "- hosts 文件大小: $(wc -c < "$HOSTS_FILE") bytes"
ui_print "- 重启后生效，可用 'ping ad.example.com' 验证"

# 记录安装日志
{
    echo "$(date) installed by NexusCore"
    echo "  target: /system/etc/hosts"
    echo "  blocked domains: $(echo "$AD_DOMAINS" | wc -w)"
} > "$MODPATH/install.log"

set_perm_recursive "$MODPATH/system" 0 0 0755 0644
set_perm "$HOSTS_FILE" 0 0 0644

exit 0
