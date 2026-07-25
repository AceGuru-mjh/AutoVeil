// nexuscli 共享库（CLI 与 daemon 都用的 IPC 编解码）
// 当前实现直接复用 ipc/codec.cpp 的 Encoder/Decoder，无需额外代码。
// 此文件作为占位，未来如果 CLI 需要单独的请求构造逻辑，放在这里。

#include "nexus/ipc/codec.h"

namespace nexus::ipc {
// 当前无额外实现；codec.h 的 Encoder/Decoder 已覆盖。
}
