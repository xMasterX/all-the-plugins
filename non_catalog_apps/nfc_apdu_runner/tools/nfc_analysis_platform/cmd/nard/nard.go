package nard

import (
	"fmt"
	"os"
	"path/filepath"

	"github.com/spensercai/nfc_apdu_runner/tools/nfc_analysis_platform/pkg/decoder"
	"github.com/spensercai/nfc_apdu_runner/tools/nfc_analysis_platform/pkg/flipper"
	"github.com/spf13/cobra"
)

var (
	filePath     string
	useDevice    bool
	useSerial    bool
	serialPort   string
	formatPath   string
	formatDir    string
	responseData string
	debugMode    bool
)

// NardCmd represents the nard command
var NardCmd = &cobra.Command{
	Use:   "nard",
	Short: "NFC APDU Runner Response Decoder",
	Long: `NFC APDU Runner Response Decoder (nard) is a tool for parsing and displaying .apdures files
from Flipper Zero NFC APDU Runner application.

You can load .apdures files either from a local file or directly from your Flipper Zero device
using either SD card mounting or serial communication.`,
	RunE: func(cmd *cobra.Command, args []string) error {
		// 获取响应数据
		var err error
		if useDevice {
			// 从设备获取响应数据
			if useSerial {
				// 使用串口通信
				responseData, err = flipper.GetResponseFromDeviceSerial(serialPort)
			} else {
				// 使用SD卡挂载
				responseData, err = flipper.GetResponseFromDevice()
			}
			if err != nil {
				return fmt.Errorf("failed to get response from device: %w", err)
			}
		} else if filePath != "" {
			// 从文件获取响应数据
			data, err := os.ReadFile(filePath)
			if err != nil {
				return fmt.Errorf("failed to read file: %w", err)
			}
			responseData = string(data)
		} else {
			return fmt.Errorf("either --file or --device flag must be specified")
		}

		// 解析响应数据
		response, err := decoder.ParseResponseData(responseData)
		if err != nil {
			return fmt.Errorf("failed to parse response data: %w", err)
		}

		// 如果没有指定格式文件，列出可用的格式文件
		if formatPath == "" {
			// 获取格式目录下的所有.apdufmt文件
			formats, err := decoder.ListFormatFiles(formatDir)
			if err != nil {
				return fmt.Errorf("failed to list format files: %w", err)
			}

			// 让用户选择格式文件
			selectedFormat, err := decoder.SelectFormat(formats)
			if err != nil {
				return fmt.Errorf("failed to select format: %w", err)
			}

			formatPath = filepath.Join(formatDir, selectedFormat)
		}

		// 读取格式文件
		formatData, err := os.ReadFile(formatPath)
		if err != nil {
			return fmt.Errorf("failed to read format file: %w", err)
		}

		// 解码并显示结果
		err = decoder.DecodeAndDisplay(response, string(formatData), debugMode)
		if err != nil {
			return fmt.Errorf("failed to decode and display: %w", err)
		}

		return nil
	},
}

func init() {
	// 获取当前可执行文件所在目录
	execPath, err := os.Executable()
	if err != nil {
		fmt.Fprintf(os.Stderr, "Error getting executable path: %s\n", err)
		os.Exit(1)
	}
	execDir := filepath.Dir(execPath)

	// 设置默认格式目录
	formatDir = filepath.Join(execDir, "nard_format")

	// 如果格式目录不存在，尝试使用相对路径
	if _, err := os.Stat(formatDir); os.IsNotExist(err) {
		formatDir = "nard_format"
	}

	// 添加命令行标志
	NardCmd.Flags().StringVarP(&filePath, "file", "f", "", "Path to .apdures file")
	NardCmd.Flags().BoolVarP(&useDevice, "device", "d", false, "Connect to Flipper Zero device")
	NardCmd.Flags().BoolVarP(&useSerial, "serial", "s", false, "Use serial communication with Flipper Zero (requires -d)")
	NardCmd.Flags().StringVarP(&serialPort, "port", "p", "", "Specify serial port for Flipper Zero (optional)")
	NardCmd.Flags().StringVar(&formatPath, "decode-format", "", "Path to format template file (.apdufmt)")
	NardCmd.Flags().StringVar(&formatDir, "format-dir", formatDir, "Directory containing format template files")
	NardCmd.Flags().BoolVar(&debugMode, "debug", false, "Enable debug mode to show error messages")
}
