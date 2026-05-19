# Gitea Runner 注册指南
# 在 fnos 主机上执行以下步骤配置 Runner

# ============================================================
# Step 1: 安装必要工具
# ============================================================
# fnos 终端执行:
#   apt-get update && apt-get install -y clang-format clang-tidy cmake ninja-build

# ============================================================
# Step 2: 获取 Runner Token
# ============================================================
# 在 Gitea Web UI:
#   仓库 → Settings → Actions → Runners → Create new Runner
#   复制显示的 token

# ============================================================
# Step 3: 下载并注册 Runner
# ============================================================
# fnos 终端执行（替换 <TOKEN> 和 <GITEA_URL>）:
#   wget https://xbw-nas.iepose.cn/Teamkiller131/act_runner/releases/download/v0.2.11/act_runner-0.2.11-linux-amd64
#   chmod +x act_runner-0.2.11-linux-amd64
#   ./act_runner-0.2.11-linux-amd64 register \
#     --instance https://xbw-nas.iepose.cn \
#     --token <TOKEN> \
#     --name fnos-shell \
#     --labels fnos-shell \
#     --no-interactive

# ============================================================
# Step 4: 启动 Runner
# ============================================================
# fnos 终端执行:
#   ./act_runner-0.2.11-linux-amd64 daemon
#
# 或配置为系统服务（推荐，保持后台运行）:
#   ./act_runner-0.2.11-linux-amd64 daemon &> /var/log/gitea-runner.log &

# ============================================================
# Windows Runner（Threadripper，备用，后期配置）
# ============================================================
# 同样步骤，下载 Windows 版 act_runner，注册为 windows-threadripper
