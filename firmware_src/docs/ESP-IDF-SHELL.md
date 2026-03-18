# ESP-IDF Shell 使用指南

## 重要说明

⚠️ **所有 ESP-IDF 相关命令（如 `idf.py build`、`idf.py flash` 等）必须通过 ESP-IDF Shell 环境执行，否则会因环境变量未设置而失败。**

## ESP-IDF Shell 调用方式

### Windows PowerShell 环境调用

在 Windows 系统中，ESP-IDF 提供了一个预配置的 PowerShell 环境。正确的调用方式如下：

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd <项目路径>; <命令>}"
```

### 常用命令示例

#### 1. 编译项目

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py build}"
```

#### 2. 烧录固件

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py -p COM3 flash}"
```

#### 3. 监控串口输出

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py -p COM3 monitor}"
```

#### 4. 完整流程（编译 + 烧录 + 监控）

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py -p COM3 build flash monitor}"
```

#### 5. 清理构建

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py fullclean}"
```

#### 6. 配置菜单

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py menuconfig}"
```

#### 7. 设置目标芯片

```powershell
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py set-target esp32s3}"
```

## 环境变量说明

ESP-IDF Shell 环境会自动设置以下关键环境变量：

```
IDF_PATH: D:\Software\.espressif\v5.5.3\esp-idf
IDF_TOOLS_PATH: C:\Espressif\tools
IDF_PYTHON_ENV_PATH: C:\Espressif\tools\python\v5.5.3\venv
```

## 可用命令

在 ESP-IDF Shell 环境中，以下命令可用：

- `idf.py` - 主要构建工具
- `esptool.py` - ESP 工具
- `espefuse.py` - eFuse 操作工具
- `espsecure.py` - 安全工具
- `otatool.py` - OTA 工具
- `parttool.py` - 分区工具

## 替代方案：ESP-IDF CMD

如果更喜欢使用 CMD 命令提示符，可以使用以下快捷方式：

```
C:\WINDOWS\System32\WindowsPowerShell\v1.0\powershell.exe -NoExit -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'}"
```

这会打开一个已配置好 ESP-IDF 环境的 PowerShell 窗口，您可以在其中直接运行 `idf.py` 等命令。

## 常见问题

### Q: 为什么直接运行 `idf.py` 会失败？

A: 因为 `idf.py` 需要 ESP-IDF 的环境变量和 Python 虚拟环境。直接在系统 PowerShell 中运行会找不到命令或缺少依赖。

### Q: 可以在 VS Code 中使用吗？

A: 可以。VS Code 的 ESP-IDF 扩展会自动配置环境。但在终端中手动执行命令时，仍需使用上述方式。

### Q: 路径中的 `v5.5.3` 是什么？

A: 这是 ESP-IDF 的版本号。如果您使用其他版本，请相应修改路径。

## 最佳实践

1. **创建批处理脚本**：为常用命令创建 `.bat` 文件，避免每次输入长命令
2. **使用别名**：在 PowerShell 配置文件中创建函数简化命令
3. **自动化 CI/CD**：在持续集成脚本中使用此方式确保环境正确

## 示例批处理脚本

创建 `build.bat` 文件：

```batch
@echo off
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py build}"
```

创建 `flash.bat` 文件：

```batch
@echo off
set PORT=%1
if "%PORT%"=="" set PORT=COM3
powershell.exe -ExecutionPolicy Bypass -NoProfile -Command "& {. 'C:\Espressif\tools\Microsoft.v5.5.3.PowerShell_profile.ps1'; cd d:\WorkSpace\LinkMIDI\firmware_src; idf.py -p %PORT% flash}"
```

使用方式：
```batch
build.bat          # 编译项目
flash.bat          # 烧录到 COM3
flash.bat COM5     # 烧录到 COM5
```