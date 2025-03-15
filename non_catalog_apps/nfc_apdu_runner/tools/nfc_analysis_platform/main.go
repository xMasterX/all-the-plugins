/*
 * @Author: SpenserCai
 * @Date: 2025-03-14 15:59:39
 * @version:
 * @LastEditors: SpenserCai
 * @LastEditTime: 2025-03-14 16:04:41
 * @Description: file content
 */
package main

import (
	"fmt"
	"os"

	"github.com/spensercai/nfc_apdu_runner/tools/nfc_analysis_platform/cmd"
)

func main() {
	if err := cmd.Execute(); err != nil {
		fmt.Fprintf(os.Stderr, "Error: %s\n", err)
		os.Exit(1)
	}
}
