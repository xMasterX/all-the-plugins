package flipper

import (
	"bufio"
	"bytes"
	"errors"
	"fmt"
	"io"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
	"time"

	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

const (
	// Flipper Zero的串口参数
	baudRate     = 115200
	dataBits     = 8
	stopBits     = serial.OneStopBit
	parity       = serial.NoParity
	readTimeout  = 5 * time.Second
	writeTimeout = 5 * time.Second

	// Flipper Zero CLI命令
	cmdPrompt       = ">: "
	cmdListFiles    = "storage list %s"
	cmdReadFile     = "storage read %s"
	cmdExit         = "exit"
	apduResponseDir = "/ext/apps_data/nfc_apdu_runner"

	// Flipper Zero USB VID/PID
	// flipperVID = "0483"
	// flipperPID = "5740"

	// 默认挂载路径（macOS）
	defaultMountPath = "/Volumes/Flipper SD"
)

// GetResponseFromDevice 从Flipper Zero设备获取APDU响应数据
// 通过SD卡挂载方式
func GetResponseFromDevice() (string, error) {
	// 获取Flipper Zero挂载路径
	mountPath, err := getFlipperMountPath()
	if err != nil {
		return "", fmt.Errorf("failed to get Flipper Zero mount path: %w", err)
	}

	// 列出响应文件
	files, err := listResponseFilesSD(mountPath)
	if err != nil {
		return "", fmt.Errorf("failed to list response files: %w", err)
	}

	if len(files) == 0 {
		return "", errors.New("no response files found on flipper zero")
	}

	// 让用户选择文件
	selectedFile, err := selectFile(files)
	if err != nil {
		return "", fmt.Errorf("failed to select file: %w", err)
	}

	// 读取文件内容
	content, err := readFileFromDeviceSD(mountPath, selectedFile)
	if err != nil {
		return "", fmt.Errorf("failed to read file from device: %w", err)
	}

	return content, nil
}

// GetResponseFromDeviceSerial 从Flipper Zero设备获取APDU响应数据
// 通过串口通信方式
func GetResponseFromDeviceSerial(portName string) (string, error) {
	// 连接到Flipper Zero
	port, err := connectToFlipper(portName)
	if err != nil {
		return "", fmt.Errorf("failed to connect to Flipper Zero: %w", err)
	}
	defer port.Close()

	// 列出响应文件
	files, err := listResponseFilesSerial(port)
	if err != nil {
		return "", fmt.Errorf("failed to list response files: %w", err)
	}

	if len(files) == 0 {
		return "", errors.New("no response files found on flipper zero")
	}

	// 让用户选择文件
	selectedFile, err := selectFile(files)
	if err != nil {
		return "", fmt.Errorf("failed to select file: %w", err)
	}

	// 读取文件内容
	content, err := readFileFromDeviceSerial(port, selectedFile)
	if err != nil {
		return "", fmt.Errorf("failed to read file from device: %w", err)
	}

	return content, nil
}

// connectToFlipper 连接到Flipper Zero设备
func connectToFlipper(portName string) (serial.Port, error) {
	// 如果没有指定端口，自动检测Flipper Zero端口
	if portName == "" {
		var err error
		portName, err = findFlipperPort()
		if err != nil {
			return nil, err
		}
	}

	// 配置串口参数
	mode := &serial.Mode{
		BaudRate: baudRate,
		DataBits: dataBits,
		StopBits: stopBits,
		Parity:   parity,
	}

	// 打开串口
	port, err := serial.Open(portName, mode)
	if err != nil {
		return nil, fmt.Errorf("failed to open serial port %s: %w", portName, err)
	}

	// 设置超时
	if err := port.SetReadTimeout(readTimeout); err != nil {
		port.Close()
		return nil, fmt.Errorf("failed to set read timeout: %w", err)
	}

	return port, nil
}

// findFlipperPort 查找Flipper Zero的串口
func findFlipperPort() (string, error) {
	// 获取所有串口
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return "", fmt.Errorf("failed to get serial ports: %w", err)
	}

	// 查找Flipper Zero的串口
	for _, port := range ports {
		// 根据VID和PID识别Flipper Zero
		if (port.IsUSB && port.VID == "0483" && port.PID == "5740") || // Flipper Zero的VID和PID
			strings.Contains(strings.ToLower(port.Product), "flipper") ||
			strings.Contains(strings.ToLower(port.Name), "flipper") {
			return port.Name, nil
		}
	}

	// 如果没有找到，尝试根据操作系统的常见命名规则查找
	var potentialPorts []string
	switch runtime.GOOS {
	case "windows":
		// Windows上通常是COMx
		for _, port := range ports {
			if strings.HasPrefix(port.Name, "COM") {
				potentialPorts = append(potentialPorts, port.Name)
			}
		}
	case "darwin":
		// macOS上通常是/dev/tty.usbmodem*
		for _, port := range ports {
			if strings.Contains(port.Name, "usbmodem") {
				potentialPorts = append(potentialPorts, port.Name)
			}
		}
	case "linux":
		// Linux上通常是/dev/ttyACM*
		for _, port := range ports {
			if strings.Contains(port.Name, "ttyACM") {
				potentialPorts = append(potentialPorts, port.Name)
			}
		}
	}

	// 如果找到了潜在的端口，使用第一个
	if len(potentialPorts) > 0 {
		return potentialPorts[0], nil
	}

	return "", errors.New("flipper zero device not found")
}

// sendCommand 发送命令到Flipper Zero
func sendCommand(port serial.Port, command string) error {
	// 添加回车符
	command = command + "\r"
	_, err := port.Write([]byte(command))
	return err
}

// selectFile 让用户选择文件
func selectFile(files []string) (string, error) {
	fmt.Println("Available .apdures files:")
	for i, file := range files {
		// 显示文件名而不是完整路径
		fmt.Printf("%d. %s\n", i+1, filepath.Base(file))
	}

	fmt.Print("Select a file (1-", len(files), "): ")
	var choice int
	_, err := fmt.Scanf("%d", &choice)
	if err != nil {
		return "", fmt.Errorf("error reading user input: %w", err)
	}

	if choice < 1 || choice > len(files) {
		return "", fmt.Errorf("invalid choice: %d", choice)
	}

	return files[choice-1], nil
}

// CloseConnection 关闭与Flipper Zero的连接
func CloseConnection(port serial.Port) error {
	if port == nil {
		return nil
	}

	// 发送退出命令
	err := sendCommand(port, cmdExit)
	if err != nil {
		return fmt.Errorf("failed to send exit command: %w", err)
	}

	// 关闭端口
	return port.Close()
}

// listResponseFilesSerial 通过串口列出响应文件
func listResponseFilesSerial(port serial.Port) ([]string, error) {
	// 执行列出文件命令
	cmd := fmt.Sprintf("storage list %s", apduResponseDir)
	response, err := executeCommand(port, cmd)
	if err != nil {
		return nil, fmt.Errorf("failed to list files: %w", err)
	}

	// 解析响应，提取文件名
	var files []string
	scanner := bufio.NewScanner(strings.NewReader(response))
	for scanner.Scan() {
		line := scanner.Text()
		// 判断逻辑为去掉首位空格和换行符，用空格分割，第0个是[F],第1个是文件名且以.apdures结尾
		line = strings.TrimSpace(line)
		parts := strings.Split(line, " ")
		if len(parts) > 1 && parts[0] == "[F]" && strings.HasSuffix(parts[1], ".apdures") {
			files = append(files, parts[1])
		}
	}

	return files, nil
}

// readFileFromDeviceSerial 通过串口从设备读取文件内容
func readFileFromDeviceSerial(port serial.Port, fileName string) (string, error) {
	// 执行读取文件命令
	filePath := filepath.Join(apduResponseDir, fileName)
	cmd := fmt.Sprintf("storage read %s", filePath)
	response, err := executeCommand(port, cmd)
	if err != nil {
		return "", fmt.Errorf("failed to read file: %w", err)
	}

	// 移除可能的"storage read done"标记
	response = strings.Replace(response, "storage read done", "", 1)

	return strings.TrimSpace(response), nil
}

// getFlipperMountPath 获取Flipper Zero挂载路径
func getFlipperMountPath() (string, error) {
	switch runtime.GOOS {
	case "darwin":
		return getMacOSMountPath()
	case "windows":
		return getWindowsMountPath()
	case "linux":
		return getLinuxMountPath()
	default:
		return "", fmt.Errorf("unsupported operating system: %s", runtime.GOOS)
	}
}

// getMacOSMountPath 获取macOS下的挂载路径
func getMacOSMountPath() (string, error) {
	// 检查默认路径是否存在
	if _, err := os.Stat(defaultMountPath); err == nil {
		return defaultMountPath, nil
	}

	// 使用diskutil命令查找挂载的Flipper SD卡
	cmd := exec.Command("diskutil", "list", "-plist")
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to execute diskutil command: %w", err)
	}

	// 解析输出，查找Flipper SD卡
	// 这里简化处理，实际应该解析plist格式
	if strings.Contains(string(output), "Flipper SD") {
		// 查找挂载点
		cmd = exec.Command("mount")
		output, err = cmd.Output()
		if err != nil {
			return "", fmt.Errorf("failed to execute mount command: %w", err)
		}

		// 查找包含"Flipper SD"的行
		scanner := bufio.NewScanner(strings.NewReader(string(output)))
		for scanner.Scan() {
			line := scanner.Text()
			if strings.Contains(line, "Flipper SD") {
				// 提取挂载点
				parts := strings.Split(line, " on ")
				if len(parts) > 1 {
					mountPoint := strings.Split(parts[1], " (")[0]
					return mountPoint, nil
				}
			}
		}
	}

	return "", errors.New("flipper sd card not found. please connect your flipper zero and ensure the sd card is mounted")
}

// getWindowsMountPath 获取Windows下的挂载路径
func getWindowsMountPath() (string, error) {
	// 使用wmic命令查找挂载的Flipper SD卡
	cmd := exec.Command("wmic", "logicaldisk", "where", "VolumeName='Flipper SD'", "get", "DeviceID")
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to execute wmic command: %w", err)
	}

	// 解析输出，查找Flipper SD卡
	scanner := bufio.NewScanner(strings.NewReader(string(output)))
	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line != "" && line != "DeviceID" {
			return line, nil
		}
	}

	return "", errors.New("flipper sd card not found. please connect your flipper zero and ensure the sd card is mounted")
}

// getLinuxMountPath 获取Linux下的挂载路径
func getLinuxMountPath() (string, error) {
	// 使用lsblk命令查找挂载的Flipper SD卡
	cmd := exec.Command("lsblk", "-o", "NAME,LABEL,MOUNTPOINT", "-J")
	output, err := cmd.Output()
	if err != nil {
		return "", fmt.Errorf("failed to execute lsblk command: %w", err)
	}

	// 解析输出，查找Flipper SD卡
	// 这里简化处理，实际应该解析JSON格式
	if strings.Contains(string(output), "Flipper SD") {
		// 查找挂载点
		cmd = exec.Command("mount")
		output, err = cmd.Output()
		if err != nil {
			return "", fmt.Errorf("failed to execute mount command: %w", err)
		}

		// 查找包含"Flipper SD"的行
		scanner := bufio.NewScanner(strings.NewReader(string(output)))
		for scanner.Scan() {
			line := scanner.Text()
			if strings.Contains(line, "Flipper SD") {
				// 提取挂载点
				parts := strings.Split(line, " on ")
				if len(parts) > 1 {
					mountPoint := strings.Split(parts[1], " type ")[0]
					return mountPoint, nil
				}
			}
		}
	}

	return "", errors.New("flipper sd card not found. please connect your flipper zero and ensure the sd card is mounted")
}

// listResponseFilesSD 列出SD卡上的.apdures文件
func listResponseFilesSD(mountPath string) ([]string, error) {
	// 构建APDU响应文件目录路径
	dirPath := filepath.Join(mountPath, apduResponseDir)

	// 检查目录是否存在
	if _, err := os.Stat(dirPath); os.IsNotExist(err) {
		return nil, fmt.Errorf("APDU response directory not found: %s", dirPath)
	}

	// 读取目录内容
	entries, err := os.ReadDir(dirPath)
	if err != nil {
		return nil, fmt.Errorf("failed to read directory: %w", err)
	}

	// 过滤.apdures文件
	var files []string
	for _, entry := range entries {
		if !entry.IsDir() && strings.HasSuffix(entry.Name(), ".apdures") {
			files = append(files, filepath.Join(apduResponseDir, entry.Name()))
		}
	}

	return files, nil
}

// readFileFromDeviceSD 从SD卡读取文件内容
func readFileFromDeviceSD(mountPath, filePath string) (string, error) {
	// 构建文件完整路径
	fullPath := filepath.Join(mountPath, filePath)

	// 检查文件是否存在
	if _, err := os.Stat(fullPath); os.IsNotExist(err) {
		return "", fmt.Errorf("file not found: %s", fullPath)
	}

	// 读取文件内容
	data, err := os.ReadFile(fullPath)
	if err != nil {
		return "", fmt.Errorf("failed to read file: %w", err)
	}

	return string(data), nil
}

// executeCommand 执行Flipper命令并返回结果
func executeCommand(port serial.Port, command string) (string, error) {
	// 发送命令
	if _, err := port.Write([]byte(command + "\r\n")); err != nil {
		return "", fmt.Errorf("failed to send command: %w", err)
	}

	// 读取响应
	var buffer bytes.Buffer
	readBuf := make([]byte, 1024)

	// 设置超时
	deadline := time.Now().Add(readTimeout)

	// 标记是否已经看到第一个提示符（命令回显）
	seenFirstPrompt := false

	// 记录提示符的位置
	var firstPromptPos, secondPromptPos int = -1, -1

	for time.Now().Before(deadline) {
		// 设置较短的读取超时，以便及时检测数据是否已读取完毕
		port.SetReadTimeout(100 * time.Millisecond)

		n, err := port.Read(readBuf)
		if err != nil {
			if err == io.EOF {
				break
			}
			// 忽略超时错误，继续尝试读取
			if strings.Contains(err.Error(), "timeout") {
				// 检查是否已经有一段时间没有新数据了
				if buffer.Len() > 0 && time.Since(deadline.Add(-readTimeout)) > 500*time.Millisecond {
					break
				}
				continue
			}
			return "", fmt.Errorf("failed to read response: %w", err)
		}

		if n == 0 {
			// 没有更多数据，等待一小段时间再次尝试
			time.Sleep(100 * time.Millisecond)
			continue
		}

		// 重置超时
		deadline = time.Now().Add(readTimeout)

		// 将读取的数据添加到缓冲区
		buffer.Write(readBuf[:n])

		// 检查是否已经接收到完整的响应
		if bytes.Contains(readBuf[:n], []byte(">:")) {
			if !seenFirstPrompt {
				// 这是第一个提示符（命令回显的一部分）
				seenFirstPrompt = true
				firstPromptPos = buffer.Len() - n + bytes.Index(readBuf[:n], []byte(">:"))
			} else {
				// 这是第二个提示符（响应结束）
				secondPromptPos = buffer.Len() - n + bytes.Index(readBuf[:n], []byte(">:"))
				break
			}
		}
	}

	// 提取两个提示符之间的内容
	fullResponse := buffer.String()

	// 如果找到了两个提示符，直接提取它们之间的内容
	if firstPromptPos >= 0 && secondPromptPos >= 0 && secondPromptPos > firstPromptPos {
		// 找到第一个提示符后的第一个换行符
		startPos := strings.Index(fullResponse[firstPromptPos:], "\n")
		if startPos >= 0 {
			startPos += firstPromptPos + 1 // +1 跳过换行符
			return strings.TrimSpace(fullResponse[startPos:secondPromptPos]), nil
		}
	}

	// 如果无法直接提取，使用行扫描方式处理
	var result strings.Builder
	scanner := bufio.NewScanner(strings.NewReader(fullResponse))

	// 跳过命令回显部分
	inResponseSection := false

	for scanner.Scan() {
		line := scanner.Text()

		// 检查是否已经跳过了命令回显部分
		if !inResponseSection {
			// 当看到包含命令的行后，下一行开始是实际响应
			if strings.Contains(line, command) {
				inResponseSection = true
			}
			continue
		}

		// 如果遇到第二个提示符，说明响应结束
		if strings.Contains(line, ">:") {
			break
		}

		// 添加到结果中
		result.WriteString(line)
		result.WriteString("\n")
	}

	return strings.TrimSpace(result.String()), nil
}
