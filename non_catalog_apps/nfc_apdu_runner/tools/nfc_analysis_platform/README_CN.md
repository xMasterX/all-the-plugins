# NFC 分析平台

NFC 分析平台是一个用于分析 Flipper Zero NFC 应用程序数据的综合工具。该平台提供了多种工具，用于 NFC 数据分析，包括：

- NFC APDU Runner 响应解码器 (nard)
- TLV 解析器 (tlv)

## 安装

### 前提条件

- Go 1.21 或更高版本
- Make (可选，用于简化构建过程)

### 依赖项

```bash
go get github.com/spf13/cobra
go get github.com/fatih/color
go get go.bug.st/serial
```

### 使用 Make 构建和安装

```bash
# 初始化项目
make init

# 构建项目
make build

# 安装到 ~/bin 目录
make install
```

### 跨平台构建

```bash
# 构建 Linux 版本
make build-linux

# 构建 Windows 版本
make build-windows

# 构建 macOS 版本
make build-macos

# 构建所有平台版本
make build-all
```

### 手动构建和安装

```bash
# 初始化 Go 模块
go mod tidy

# 构建项目
go build -o nfc_analysis_platform .

# 安装到 ~/bin 目录
mkdir -p ~/bin
cp nfc_analysis_platform ~/bin/
mkdir -p ~/bin/format
cp -r format/* ~/bin/format/ 2>/dev/null || :
```

确保 `~/bin` 目录在您的 PATH 环境变量中。

## 使用方法

### NFC APDU Runner 响应解码器 (nard)

NFC APDU Runner 响应解码器用于解析和显示来自 Flipper Zero NFC APDU Runner 应用程序的 `.apdures` 文件。

```bash
# 从本地文件加载 .apdures 文件
nfc_analysis_platform nard --file path/to/file.apdures

# 从 Flipper Zero 设备加载 .apdures 文件
nfc_analysis_platform nard --device

# 使用串口通信从 Flipper Zero 设备加载 .apdures 文件
nfc_analysis_platform nard --device --serial

# 指定串口
nfc_analysis_platform nard --device --serial --port /dev/ttyACM0

# 指定解码格式模板
nfc_analysis_platform nard --decode-format path/to/format.apdufmt

# 指定格式目录
nfc_analysis_platform nard --format-dir /path/to/format/directory

# 启用调试模式
nfc_analysis_platform nard --debug
```

#### 格式模板语法

NFC APDU Runner 响应解码器使用 Go 模板进行格式化。以下函数可用：

- `TAG(tag)`：从响应数据中提取 TLV 标签的值
- `hex(data)`：将十六进制字符串转换为 ASCII
- `h2d(data)`：将十六进制字符串转换为十进制
- 数学运算：加法 (`+`)、减法 (`-`)、乘法 (`*`)、除法 (`/`)
- 切片：`expression[start:end]` 从表达式结果中提取子字符串

示例格式模板：
```
EMV Card Information
Application Label: {O[1]TAG(50), "ascii"}
Card Number: {O[3]TAG(9F6B)[0:16], "numeric"}
Expiry Date: Year: 20{O[3]TAG(9F6B)[17:19], "numeric"}, Month: {O[3]TAG(9F6B)[19:21], "numeric"}
Country Code: {O[1]TAG(9F6E)[0:4]}
Application Priority: {O[1]TAG(87), "hex"}
Application Interchange Profile: {O[2]TAG(82), "hex"}
PIN Try Counter: {O[4]TAG(9F17), "numeric"}
```

### TLV 解析器 (tlv)

TLV 解析器用于解析 TLV（Tag-Length-Value）数据并提取指定标签的值。

```bash
# 解析所有标签
nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264

# 解析特定标签
nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264 --tag 84,50

# 指定数据类型
nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264 --tag 50 --type ascii
```

#### 支持的数据类型

- `utf8`, `utf-8`: UTF-8 编码
- `ascii`: ASCII 编码
- `numeric`: 数字编码（如 BCD 码）

## 功能特点

### NFC APDU Runner 响应解码器 (nard)

- 支持自定义解码格式模板
- 支持从本地文件或 Flipper Zero 设备加载 .apdures 文件
- 支持串口通信
- 支持调试模式，显示详细错误信息
- 支持十六进制到十进制的转换
- 支持数学运算
- 支持函数结果切片

### TLV 解析器 (tlv)

- 支持解析 TLV 数据
- 支持递归解析嵌套 TLV 结构
- 支持提取特定标签的值
- 支持多种数据类型显示（十六进制、ASCII、UTF-8、数字）
- 支持彩色输出，提高可读性

## 格式模板示例

平台在 `format` 目录中包含了几个示例格式模板：

- `emv_card.apdufmt`：用于解码 EMV 卡片信息

您可以创建自己的格式模板并将它们放在格式目录中。

## 错误处理

该工具自动检测错误响应（非 9000 状态码）并适当处理：

- 当输出具有非成功状态码（不是 9000）时，任何引用该输出的模板表达式将返回空字符串
- 解析过程中会收集错误信息，但默认不显示
- 要查看错误信息，请在运行命令时使用 `--debug` 参数
- 在调试模式下，错误信息会在输出结束后显示

## 贡献

欢迎贡献代码、报告问题或提出改进建议。请通过 GitHub Issues 或 Pull Requests 提交您的贡献。

## 许可证

本项目采用 MIT 许可证。详情请参阅 LICENSE 文件。 