package decoder

import (
	"bufio"
	"encoding/hex"
	"fmt"
	"go/ast"
	"go/parser"
	"go/token"
	"io/ioutil"
	"math"
	"os"
	"regexp"
	"strconv"
	"strings"

	"github.com/fatih/color"
	"github.com/spensercai/nfc_apdu_runner/tools/nfc_analysis_platform/pkg/tlv"
)

// Response 表示解析后的APDU响应
type Response struct {
	Inputs  []string
	Outputs []string
}

// ErrorInfo 表示APDU错误信息
type ErrorInfo struct {
	Index       int
	StatusCode  string
	Description string
}

// DecodeContext 解码上下文，包含解码过程中的状态
type DecodeContext struct {
	Response *Response
	Errors   []ErrorInfo
	Debug    bool
}

// NewDecodeContext 创建新的解码上下文
func NewDecodeContext(response *Response, debug bool) *DecodeContext {
	return &DecodeContext{
		Response: response,
		Errors:   []ErrorInfo{},
		Debug:    debug,
	}
}

// AddError 添加错误信息
func (ctx *DecodeContext) AddError(index int, statusCode string) {
	description := GetErrorDescription(statusCode)
	ctx.Errors = append(ctx.Errors, ErrorInfo{
		Index:       index,
		StatusCode:  statusCode,
		Description: description,
	})
}

// ParseResponseData 解析.apdures文件内容
func ParseResponseData(data string) (*Response, error) {
	scanner := bufio.NewScanner(strings.NewReader(data))
	response := &Response{
		Inputs:  []string{},
		Outputs: []string{},
	}

	// 跳过文件头
	for scanner.Scan() {
		line := scanner.Text()
		if line == "Response:" {
			break
		}
	}

	// 解析输入和输出
	for scanner.Scan() {
		line := scanner.Text()
		if strings.HasPrefix(line, "In: ") {
			response.Inputs = append(response.Inputs, strings.TrimPrefix(line, "In: "))
		} else if strings.HasPrefix(line, "Out: ") {
			response.Outputs = append(response.Outputs, strings.TrimPrefix(line, "Out: "))
		}
	}

	if err := scanner.Err(); err != nil {
		return nil, fmt.Errorf("error scanning response data: %w", err)
	}

	return response, nil
}

// ListFormatFiles 列出格式目录下的所有.apdufmt文件
func ListFormatFiles(formatDir string) ([]string, error) {
	files, err := ioutil.ReadDir(formatDir)
	if err != nil {
		return nil, fmt.Errorf("error reading format directory: %w", err)
	}

	var formatFiles []string
	for _, file := range files {
		if !file.IsDir() && strings.HasSuffix(file.Name(), ".apdufmt") {
			formatFiles = append(formatFiles, file.Name())
		}
	}

	return formatFiles, nil
}

// SelectFormat 让用户选择格式文件
func SelectFormat(formats []string) (string, error) {
	if len(formats) == 0 {
		return "", fmt.Errorf("no format files found")
	}

	fmt.Println("Available format files:")
	for i, format := range formats {
		fmt.Printf("%d. %s\n", i+1, format)
	}

	fmt.Print("Select a format (1-", len(formats), "): ")
	var choice int
	_, err := fmt.Scanf("%d", &choice)
	if err != nil {
		return "", fmt.Errorf("error reading user input: %w", err)
	}

	if choice < 1 || choice > len(formats) {
		return "", fmt.Errorf("invalid choice: %d", choice)
	}

	return formats[choice-1], nil
}

// DecodeAndDisplay 解码并显示结果
func DecodeAndDisplay(response *Response, formatData string, debug bool) error {
	lines := strings.Split(formatData, "\n")
	if len(lines) == 0 {
		return fmt.Errorf("empty format data")
	}

	// 创建解码上下文
	ctx := NewDecodeContext(response, debug)

	// 显示标题
	title := lines[0]
	titleColor := color.New(color.FgCyan, color.Bold)
	fmt.Println(strings.Repeat("=", 50))
	titleColor.Printf("%s\n", centerText(title, 50))
	fmt.Println(strings.Repeat("=", 50))

	// 解析和显示其余行
	for i := 1; i < len(lines); i++ {
		line := lines[i]
		if line == "" {
			fmt.Println()
			continue
		}

		// 解析占位符
		result, err := parsePlaceholders(line, ctx)
		if err != nil {
			return fmt.Errorf("error parsing line %d: %w", i+1, err)
		}

		fmt.Println(result)
	}

	// 如果启用了调试模式并且有错误，显示错误信息
	if debug && len(ctx.Errors) > 0 {
		errorColor := color.New(color.FgRed, color.Bold)
		fmt.Println()
		fmt.Println(strings.Repeat("-", 50))
		errorColor.Println("Error Information:")
		fmt.Println(strings.Repeat("-", 50))

		for _, err := range ctx.Errors {
			fmt.Printf("Output[%d]: Error code %s - %s\n",
				err.Index, err.StatusCode, err.Description)
		}
	}

	return nil
}

// centerText 将文本居中
func centerText(text string, width int) string {
	if len(text) >= width {
		return text
	}

	padding := (width - len(text)) / 2
	return strings.Repeat(" ", padding) + text
}

// parsePlaceholders 解析并替换占位符
func parsePlaceholders(line string, ctx *DecodeContext) (string, error) {
	re := regexp.MustCompile(`\{([^}]+)\}`)
	result := re.ReplaceAllStringFunc(line, func(match string) string {
		// 去除大括号
		expr := match[1 : len(match)-1]

		// 解析表达式
		value, err := evaluateExpression(expr, ctx)
		if err != nil {
			fmt.Fprintf(os.Stderr, "Warning: Error evaluating expression '%s': %s\n", expr, err)
			return match
		}

		return value
	})

	return result, nil
}

// evaluateExpression 评估表达式
func evaluateExpression(expr string, ctx *DecodeContext) (string, error) {
	// 首先检查是否包含数学表达式
	if containsMathOperators(expr) {
		// 先处理所有的函数调用和数据引用
		processedExpr, err := preprocessMathExpression(expr, ctx)
		if err != nil {
			return "", err
		}

		// 计算数学表达式
		result, err := evaluateMathExpression(processedExpr)
		if err != nil {
			return "", err
		}

		return result, nil
	}

	// 检查是否是带切片的TAG表达式，例如 O[1]TAG(84)[2:6]
	tagWithSliceRe := regexp.MustCompile(`([OI])\[(\d+)\]TAG\(([0-9A-Fa-f]+)\)(?:,\s*"([^"]+)")?\[([^]]+)\]`)
	tagWithSliceMatches := tagWithSliceRe.FindStringSubmatch(expr)
	if tagWithSliceMatches != nil {
		// 先获取TAG值
		sourceType := tagWithSliceMatches[1]
		indexStr := tagWithSliceMatches[2]
		tagHex := tagWithSliceMatches[3]
		encoding := ""
		if len(tagWithSliceMatches) > 4 && tagWithSliceMatches[4] != "" {
			encoding = tagWithSliceMatches[4]
		}
		sliceExpr := tagWithSliceMatches[5]

		// 构造不带切片的TAG表达式
		tagExpr := sourceType + "[" + indexStr + "]TAG(" + tagHex + ")"
		if encoding != "" {
			tagExpr += ", \"" + encoding + "\""
		}

		// 获取TAG值
		tagValue, err := evaluateExpression(tagExpr, ctx)
		if err != nil {
			return "", err
		}

		// 如果TAG值为空，直接返回
		if tagValue == "" {
			return "", nil
		}

		// 应用切片
		return applySlice(tagValue, sliceExpr)
	}

	// 检查是否是TLV标签表达式
	tlvTagRe := regexp.MustCompile(`([OI])\[(\d+)\]TAG\(([0-9A-Fa-f]+)\)(?:,\s*"([^"]+)")?`)
	tlvMatches := tlvTagRe.FindStringSubmatch(expr)
	if tlvMatches != nil {
		// 获取数据源（输入或输出）
		var data []string
		if tlvMatches[1] == "O" {
			data = ctx.Response.Outputs
		} else {
			data = ctx.Response.Inputs
		}

		// 获取索引
		index, err := strconv.Atoi(tlvMatches[2])
		if err != nil {
			return "", fmt.Errorf("invalid index: %s", tlvMatches[2])
		}

		if index < 0 || index >= len(data) {
			return "", fmt.Errorf("index out of range: %d", index)
		}

		// 检查输出是否成功
		if tlvMatches[1] == "O" && index < len(ctx.Response.Outputs) {
			output := ctx.Response.Outputs[index]
			if len(output) >= 4 {
				statusCode := output[len(output)-4:]
				if !IsSuccessStatusCode(statusCode) {
					// 添加错误信息
					ctx.AddError(index, statusCode)
					// 返回空字符串
					return "", nil
				}
			}
		}

		// 获取标签
		tagHex := tlvMatches[3]

		// 获取编码（如果有）
		encoding := ""
		if len(tlvMatches) > 4 && tlvMatches[4] != "" {
			encoding = tlvMatches[4]
		}

		// 解析TLV并获取标签值
		hexData := data[index]
		if encoding == "" {
			// 返回原始十六进制
			return tlv.GetTagValue(hexData, tagHex)
		} else {
			// 返回解码后的字符串
			return tlv.GetTagValueAsString(hexData, tagHex, encoding)
		}
	}

	// 检查是否是带切片的h2d函数，例如 h2d(O[1][2:6])[2:4]
	h2dWithSliceRe := regexp.MustCompile(`h2d\(([^)]+)\)\[([^]]+)\]`)
	h2dWithSliceMatches := h2dWithSliceRe.FindStringSubmatch(expr)
	if h2dWithSliceMatches != nil {
		// 先获取h2d值
		h2dArg := h2dWithSliceMatches[1]
		sliceExpr := h2dWithSliceMatches[2]

		// 构造不带切片的h2d表达式
		h2dExpr := "h2d(" + h2dArg + ")"

		// 获取h2d值
		h2dValue, err := evaluateExpression(h2dExpr, ctx)
		if err != nil {
			return "", err
		}

		// 如果h2d值为空，直接返回
		if h2dValue == "" {
			return "", nil
		}

		// 应用切片
		return applySlice(h2dValue, sliceExpr)
	}

	// 检查是否是带切片的hex函数，例如 hex(O[1][2:6])[2:4]
	hexWithSliceRe := regexp.MustCompile(`hex\(([^)]+)\)\[([^]]+)\]`)
	hexWithSliceMatches := hexWithSliceRe.FindStringSubmatch(expr)
	if hexWithSliceMatches != nil {
		// 先获取hex值
		hexArg := hexWithSliceMatches[1]
		sliceExpr := hexWithSliceMatches[2]

		// 构造不带切片的hex表达式
		hexExpr := "hex(" + hexArg + ")"

		// 获取hex值
		hexValue, err := evaluateExpression(hexExpr, ctx)
		if err != nil {
			return "", err
		}

		// 如果hex值为空，直接返回
		if hexValue == "" {
			return "", nil
		}

		// 应用切片
		return applySlice(hexValue, sliceExpr)
	}

	// 检查是否是h2d函数
	if strings.HasPrefix(expr, "h2d(") && strings.HasSuffix(expr, ")") {
		// 提取h2d函数的参数
		args := expr[4 : len(expr)-1]

		// 获取数据
		data, err := evaluateDataExpression(args, ctx)
		if err != nil {
			// 如果不是数据表达式，尝试直接解析
			data = args
		}

		// 如果数据为空，直接返回空字符串
		if data == "" {
			return "", nil
		}

		// 将十六进制转换为十进制
		decimal, err := hexToDecimal(data)
		if err != nil {
			return "", err
		}

		return decimal, nil
	}

	// 检查是否是hex函数
	if strings.HasPrefix(expr, "hex(") && strings.HasSuffix(expr, ")") {
		// 提取hex函数的参数
		args := expr[4 : len(expr)-1]
		parts := strings.Split(args, ",")

		var dataExpr string
		var encoding string

		if len(parts) == 1 {
			dataExpr = strings.TrimSpace(parts[0])
			encoding = "gbk" // 默认编码
		} else if len(parts) == 2 {
			dataExpr = strings.TrimSpace(parts[0])
			encoding = strings.Trim(strings.TrimSpace(parts[1]), "\"'")
		} else {
			return "", fmt.Errorf("invalid number of arguments for hex function")
		}

		// 获取数据
		data, err := evaluateDataExpression(dataExpr, ctx)
		if err != nil {
			return "", err
		}

		// 如果数据为空，直接返回空字符串
		if data == "" {
			return "", nil
		}

		// 解码十六进制
		decoded, err := hexDecode(data, encoding)
		if err != nil {
			return "", err
		}

		return decoded, nil
	}

	// 直接评估数据表达式
	return evaluateDataExpression(expr, ctx)
}

// containsMathOperators 检查表达式是否包含数学运算符
func containsMathOperators(expr string) bool {
	// 检查是否包含 +, -, *, / 运算符，但排除函数调用中的这些符号
	// 这是一个简化的检查，可能需要更复杂的解析
	inFunction := 0
	for i := 0; i < len(expr); i++ {
		if expr[i] == '(' {
			inFunction++
		} else if expr[i] == ')' {
			inFunction--
		} else if inFunction == 0 && (expr[i] == '+' || expr[i] == '-' || expr[i] == '*' || expr[i] == '/') {
			return true
		}
	}
	return false
}

// preprocessMathExpression 预处理数学表达式，替换函数调用和数据引用
func preprocessMathExpression(expr string, ctx *DecodeContext) (string, error) {
	// 匹配函数调用和数据引用
	re := regexp.MustCompile(`(h2d|hex)\([^)]+\)|([OI])\[(\d+)\](?:\[([^]]+)\])?`)

	result := re.ReplaceAllStringFunc(expr, func(match string) string {
		// 评估子表达式
		value, err := evaluateExpression(match, ctx)
		if err != nil {
			// 在这里我们不能直接返回错误，所以记录错误并返回原始匹配
			fmt.Fprintf(os.Stderr, "Warning: Error evaluating sub-expression '%s': %s\n", match, err)
			return match
		}

		// 如果结果是数字，直接返回
		if _, err := strconv.ParseFloat(value, 64); err == nil {
			return value
		}

		// 否则，将结果括在引号中作为字符串
		return fmt.Sprintf("\"%s\"", value)
	})

	return result, nil
}

// evaluateMathExpression 计算数学表达式
func evaluateMathExpression(expr string) (string, error) {
	// 使用 Go 的表达式解析器
	e, err := parser.ParseExpr(expr)
	if err != nil {
		return "", fmt.Errorf("error parsing expression: %w", err)
	}

	// 计算表达式
	result, err := evaluateAst(e)
	if err != nil {
		return "", err
	}

	// 格式化结果
	switch v := result.(type) {
	case float64:
		// 检查是否为整数
		if v == math.Floor(v) {
			return fmt.Sprintf("%.0f", v), nil
		}
		return fmt.Sprintf("%.2f", v), nil
	case int64:
		return fmt.Sprintf("%d", v), nil
	case string:
		return v, nil
	default:
		return fmt.Sprintf("%v", v), nil
	}
}

// evaluateAst 计算抽象语法树
func evaluateAst(expr ast.Expr) (interface{}, error) {
	switch e := expr.(type) {
	case *ast.BasicLit:
		// 基本字面量（数字、字符串等）
		switch e.Kind {
		case token.INT:
			return strconv.ParseInt(e.Value, 10, 64)
		case token.FLOAT:
			return strconv.ParseFloat(e.Value, 64)
		case token.STRING:
			// 去除引号
			return e.Value[1 : len(e.Value)-1], nil
		default:
			return nil, fmt.Errorf("unsupported literal type: %v", e.Kind)
		}

	case *ast.BinaryExpr:
		// 二元表达式（如 a + b）
		x, err := evaluateAst(e.X)
		if err != nil {
			return nil, err
		}

		y, err := evaluateAst(e.Y)
		if err != nil {
			return nil, err
		}

		// 将操作数转换为 float64
		xFloat, xErr := toFloat64(x)
		yFloat, yErr := toFloat64(y)

		if xErr != nil || yErr != nil {
			return nil, fmt.Errorf("cannot perform operation on non-numeric values")
		}

		// 执行操作
		switch e.Op {
		case token.ADD:
			return xFloat + yFloat, nil
		case token.SUB:
			return xFloat - yFloat, nil
		case token.MUL:
			return xFloat * yFloat, nil
		case token.QUO:
			if yFloat == 0 {
				return nil, fmt.Errorf("division by zero")
			}
			return xFloat / yFloat, nil
		default:
			return nil, fmt.Errorf("unsupported operator: %v", e.Op)
		}

	case *ast.ParenExpr:
		// 括号表达式
		return evaluateAst(e.X)

	default:
		return nil, fmt.Errorf("unsupported expression type: %T", expr)
	}
}

// toFloat64 将值转换为 float64
func toFloat64(v interface{}) (float64, error) {
	switch val := v.(type) {
	case float64:
		return val, nil
	case int64:
		return float64(val), nil
	case int:
		return float64(val), nil
	case string:
		return strconv.ParseFloat(val, 64)
	default:
		return 0, fmt.Errorf("cannot convert %T to float64", v)
	}
}

// hexToDecimal 将十六进制字符串转换为十进制数字字符串
func hexToDecimal(hexStr string) (string, error) {
	// 移除可能的 0x 前缀
	hexStr = strings.TrimPrefix(hexStr, "0x")
	hexStr = strings.TrimPrefix(hexStr, "0X")

	// 移除空格
	hexStr = strings.ReplaceAll(hexStr, " ", "")

	// 解析十六进制
	num, err := strconv.ParseInt(hexStr, 16, 64)
	if err != nil {
		return "", fmt.Errorf("error parsing hex value: %w", err)
	}

	return fmt.Sprintf("%d", num), nil
}

// hexDecode 解码十六进制字符串
func hexDecode(hexStr string, encoding string) (string, error) {
	// 解码十六进制
	data, err := hex.DecodeString(hexStr)
	if err != nil {
		return "", fmt.Errorf("error decoding hex: %w", err)
	}

	// 根据编码转换为字符串
	var result string

	switch encoding {
	case "utf-8", "utf8":
		result = string(data)
	case "gbk":
		// 简单处理：保留可见字符
		var sb strings.Builder
		for _, b := range data {
			if b >= 32 && b <= 126 {
				sb.WriteByte(b)
			}
		}
		result = sb.String()
	default:
		// 默认处理：保留可见字符
		var sb strings.Builder
		for _, b := range data {
			if b >= 32 && b <= 126 {
				sb.WriteByte(b)
			}
		}
		result = sb.String()
	}

	// 清理结果，只保留有效内容
	result = cleanString(result)

	return result, nil
}

// cleanString 清理字符串，只保留有效内容
func cleanString(s string) string {
	// 保留中文、英文、数字、空格等
	re := regexp.MustCompile(`[^\p{Han}\p{Latin}\p{N}\s.,;:!?()[\]{}'"\/\+\-\*=<>@#$%^&_|\\]+`)
	return re.ReplaceAllString(s, "")
}

// evaluateDataExpression 评估数据表达式 (如 O[1][10:18])
func evaluateDataExpression(expr string, ctx *DecodeContext) (string, error) {
	// 匹配 O[index][slice] 或 I[index][slice] 格式
	re := regexp.MustCompile(`([OI])\[(\d+)\](?:\[([^]]+)\])?`)
	matches := re.FindStringSubmatch(expr)

	if matches == nil {
		return "", fmt.Errorf("invalid data expression format: %s", expr)
	}

	// 确定是输入还是输出
	var data []string
	if matches[1] == "O" {
		data = ctx.Response.Outputs
	} else {
		data = ctx.Response.Inputs
	}

	// 获取索引
	index, err := strconv.Atoi(matches[2])
	if err != nil {
		return "", fmt.Errorf("invalid index: %s", matches[2])
	}

	if index < 0 || index >= len(data) {
		return "", fmt.Errorf("index out of range: %d", index)
	}

	// 检查输出是否成功
	if matches[1] == "O" && index < len(ctx.Response.Outputs) {
		output := ctx.Response.Outputs[index]
		if len(output) >= 4 {
			statusCode := output[len(output)-4:]
			if !IsSuccessStatusCode(statusCode) {
				// 添加错误信息
				ctx.AddError(index, statusCode)
				// 返回空字符串
				return "", nil
			}
		}
	}

	// 获取数据
	value := data[index]

	// 如果有切片表达式，应用它
	if len(matches) > 3 && matches[3] != "" {
		value, err = applySlice(value, matches[3])
		if err != nil {
			return "", err
		}
	}

	return value, nil
}

// applySlice 应用切片表达式 (如 10:18)
func applySlice(value string, sliceExpr string) (string, error) {
	parts := strings.Split(sliceExpr, ":")

	if len(parts) != 2 {
		return "", fmt.Errorf("invalid slice expression: %s", sliceExpr)
	}

	var start, end int
	var err error

	// 解析起始位置
	if parts[0] == "" {
		start = 0
	} else {
		start, err = strconv.Atoi(parts[0])
		if err != nil {
			return "", fmt.Errorf("invalid start index: %s", parts[0])
		}
	}

	// 解析结束位置
	if parts[1] == "" {
		end = len(value)
	} else {
		end, err = strconv.Atoi(parts[1])
		if err != nil {
			return "", fmt.Errorf("invalid end index: %s", parts[1])
		}
	}

	// 应用切片
	if start < 0 {
		start = len(value) + start
	}
	if end < 0 {
		end = len(value) + end
	}

	if start < 0 || start > len(value) || end < 0 || end > len(value) || start > end {
		return "", fmt.Errorf("slice indices out of range: %s", sliceExpr)
	}

	return value[start:end], nil
}
