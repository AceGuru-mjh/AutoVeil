//! 声明式 Hook 表
//!
//! 创新点：模块用 TOML 描述 hook 意图，无需写 C++ 代码。
//! NexusHook daemon 在运行时解析 TOML，根据声明生成代理方法。
//!
//! 示例 hook.toml:
//! ```toml
//! [[hook]]
//! target_class = "android.app.ActivityManager"
//! target_method = "getRunningAppProcesses"
//! signature = "()Ljava/util/List;"
//! before = "log_call"          # 模块导出函数名
//! after = "modify_result"
//!
//! [[hook]]
//! target_class = "java.lang.Runtime"
//! target_method = "exec"
//! signature = "(Ljava/lang/String;)Ljava/lang/Process;"
//! replace = "block_shell"      # 完全替换原方法
//! ```

use std::collections::HashMap;

/// 单个 hook 条目
#[derive(Debug, Clone, PartialEq)]
pub struct HookEntry {
    /// 目标 Java 类全限定名
    pub target_class: String,
    /// 目标方法名
    pub target_method: String,
    /// JVM 方法签名（如 "()Ljava/util/List;"）
    pub signature: String,
    /// hook 类型
    pub hook_type: HookType,
    /// 模块导出的处理函数名（在模块 .so 中）
    pub handler: String,
    /// 额外参数（透传给 handler）
    pub params: HashMap<String, String>,
}

/// hook 类型
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HookType {
    /// 在原方法前调用 handler
    Before,
    /// 在原方法后调用 handler
    After,
    /// 完全替换原方法
    Replace,
    /// 在原方法前后都调用 handler
    Around,
}

/// hook 触发目标
#[derive(Debug, Clone, PartialEq)]
pub enum HookTarget {
    /// 指定类名 + 方法名
    JavaMethod {
        class: String,
        method: String,
        signature: String,
    },
    /// 指定 native 函数（用于 hook libart.so / libbinder.so 等）
    NativeSymbol {
        lib: String,
        symbol: String,
    },
    /// 系统属性读取（hook __system_property_get）
    SystemProperty {
        key_pattern: String,
    },
}

/// 已解析的 hook 表
#[derive(Debug, Clone, Default)]
pub struct HookTable {
    pub entries: Vec<HookEntry>,
}

impl HookTable {
    /// 从 TOML 字符串解析
    pub fn parse_toml(content: &str) -> Result<Self, HookTableError> {
        // MVP 极简手写 TOML parser（仅支持本模块用到的子集）
        // 生产可换 toml crate
        let mut entries = Vec::new();
        let mut current: Option<HookEntry> = None;
        let mut current_params: HashMap<String, String> = HashMap::new();

        for (lineno, line) in content.lines().enumerate() {
            let trimmed = line.trim();
            if trimmed.is_empty() || trimmed.starts_with('#') {
                continue;
            }
            if trimmed == "[[hook]]" {
                if let Some(mut e) = current.take() {
                    e.params = std::mem::take(&mut current_params);
                    entries.push(e);
                }
                current = Some(HookEntry {
                    target_class: String::new(),
                    target_method: String::new(),
                    signature: String::new(),
                    hook_type: HookType::Before,
                    handler: String::new(),
                    params: HashMap::new(),
                });
                continue;
            }
            if let Some(eq) = trimmed.find('=') {
                let key = trimmed[..eq].trim();
                let val = trimmed[eq + 1..].trim().trim_matches('"');
                let entry = current.as_mut().ok_or_else(|| {
                    HookTableError::Parse(format!("line {}: key=value outside [[hook]]", lineno + 1))
                })?;
                match key {
                    "target_class" => entry.target_class = val.to_string(),
                    "target_method" => entry.target_method = val.to_string(),
                    "signature" => entry.signature = val.to_string(),
                    "handler" => entry.handler = val.to_string(),
                    "before" => {
                        entry.hook_type = HookType::Before;
                        entry.handler = val.to_string();
                    }
                    "after" => {
                        entry.hook_type = HookType::After;
                        entry.handler = val.to_string();
                    }
                    "replace" => {
                        entry.hook_type = HookType::Replace;
                        entry.handler = val.to_string();
                    }
                    "around" => {
                        entry.hook_type = HookType::Around;
                        entry.handler = val.to_string();
                    }
                    _ => {
                        // 其他 key 作为 params
                        entry.params.insert(key.to_string(), val.to_string());
                    }
                }
                continue;
            }
            return Err(HookTableError::Parse(format!(
                "line {}: cannot parse: {}", lineno + 1, line
            )));
        }
        if let Some(mut e) = current {
            e.params = current_params;
            entries.push(e);
        }
        Ok(Self { entries })
    }

    /// 校验所有 entry（必填字段、handler 非空等）
    pub fn validate(&self) -> Vec<String> {
        let mut warnings = Vec::new();
        for (i, e) in self.entries.iter().enumerate() {
            if e.target_class.is_empty() {
                warnings.push(format!("hook[{}]: target_class is empty", i));
            }
            if e.target_method.is_empty() {
                warnings.push(format!("hook[{}]: target_method is empty", i));
            }
            if e.signature.is_empty() {
                warnings.push(format!("hook[{}]: signature is empty", i));
            }
            if e.handler.is_empty() {
                warnings.push(format!("hook[{}]: handler is empty", i));
            }
        }
        warnings
    }

    /// 查找匹配某个目标的 hook
    pub fn find_for_java_method(&self, class: &str, method: &str, sig: &str) -> Vec<&HookEntry> {
        self.entries.iter().filter(|e| {
            e.target_class == class && e.target_method == method && e.signature == sig
        }).collect()
    }
}

/// hook 表解析错误
#[derive(Debug)]
pub enum HookTableError {
    Parse(String),
    Io(std::io::Error),
}

impl std::fmt::Display for HookTableError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Parse(s) => write!(f, "parse error: {}", s),
            Self::Io(e) => write!(f, "io error: {}", e),
        }
    }
}

impl std::error::Error for HookTableError {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_simple_toml() {
        let toml = r#"
# 一个简单 hook
[[hook]]
target_class = "android.app.ActivityManager"
target_method = "getRunningAppProcesses"
signature = "()Ljava/util/List;"
before = "log_call"
"#;
        let table = HookTable::parse_toml(toml).unwrap();
        assert_eq!(table.entries.len(), 1);
        assert_eq!(table.entries[0].target_class, "android.app.ActivityManager");
        assert_eq!(table.entries[0].target_method, "getRunningAppProcesses");
        assert_eq!(table.entries[0].signature, "()Ljava/util/List;");
        assert_eq!(table.entries[0].hook_type, HookType::Before);
        assert_eq!(table.entries[0].handler, "log_call");
    }

    #[test]
    fn test_parse_multiple_hooks() {
        let toml = r#"
[[hook]]
target_class = "java.lang.Runtime"
target_method = "exec"
signature = "(Ljava/lang/String;)Ljava/lang/Process;"
replace = "block_shell"

[[hook]]
target_class = "android.app.Activity"
target_method = "onCreate"
signature = "(Landroid/os/Bundle;)V"
after = "log_activity_create"
"#;
        let table = HookTable::parse_toml(toml).unwrap();
        assert_eq!(table.entries.len(), 2);
        assert_eq!(table.entries[0].hook_type, HookType::Replace);
        assert_eq!(table.entries[1].hook_type, HookType::After);
    }

    #[test]
    fn test_parse_with_params() {
        let toml = r#"
[[hook]]
target_class = "android.location.Location"
target_method = "getLatitude"
signature = "()D"
replace = "fake_location"
fake_lat = "37.7749"
fake_lng = "-122.4194"
"#;
        let table = HookTable::parse_toml(toml).unwrap();
        assert_eq!(table.entries[0].params.get("fake_lat"), Some(&"37.7749".to_string()));
        assert_eq!(table.entries[0].params.get("fake_lng"), Some(&"-122.4194".to_string()));
    }

    #[test]
    fn test_validate_empty_fields() {
        let toml = r#"
[[hook]]
target_class = ""
target_method = ""
"#;
        let table = HookTable::parse_toml(toml).unwrap();
        let warnings = table.validate();
        assert!(warnings.iter().any(|w| w.contains("target_class is empty")));
        assert!(warnings.iter().any(|w| w.contains("target_method is empty")));
        assert!(warnings.iter().any(|w| w.contains("signature is empty")));
        assert!(warnings.iter().any(|w| w.contains("handler is empty")));
    }

    #[test]
    fn test_find_for_java_method() {
        let toml = r#"
[[hook]]
target_class = "Foo"
target_method = "bar"
signature = "()V"
before = "h1"

[[hook]]
target_class = "Foo"
target_method = "bar"
signature = "()V"
after = "h2"

[[hook]]
target_class = "Other"
target_method = "baz"
signature = "()V"
before = "h3"
"#;
        let table = HookTable::parse_toml(toml).unwrap();
        let matches = table.find_for_java_method("Foo", "bar", "()V");
        assert_eq!(matches.len(), 2);
    }

    #[test]
    fn test_comments_and_blank_lines() {
        let toml = r#"
# comment 1

# comment 2
[[hook]]
# inline comment
target_class = "Foo"

target_method = "bar"
signature = "()V"
before = "h1"
"#;
        let table = HookTable::parse_toml(toml).unwrap();
        assert_eq!(table.entries.len(), 1);
        assert_eq!(table.entries[0].target_class, "Foo");
    }
}
