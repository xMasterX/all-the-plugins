package tlv

import (
	"fmt"
	"strings"

	"github.com/fatih/color"
	"github.com/spensercai/nfc_apdu_runner/tools/nfc_analysis_platform/pkg/tlv"
	"github.com/spf13/cobra"
)

var (
	hexData  string
	tagList  string
	dataType string
)

// TlvCmd represents the tlv command
var TlvCmd = &cobra.Command{
	Use:   "tlv",
	Short: "Parse TLV data and extract values for specified tags",
	Long: `Parse TLV data and extract values for specified tags.

Examples:
  # Parse all tags
  nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264

  # Parse specific tags
  nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264 --tag 84,50

  # Specify data type
  nfc_analysis_platform tlv --hex 6F198407A0000000031010A50E500A4D617374657243617264 --tag 50 --type ascii`,
	RunE: func(cmd *cobra.Command, args []string) error {
		if hexData == "" {
			return fmt.Errorf("hex data must be provided (--hex)")
		}

		// Parse TLV data
		tlvList, err := tlv.ParseHex(hexData)
		if err != nil {
			return fmt.Errorf("failed to parse TLV data: %w", err)
		}

		// If no tag is specified, display all tags
		if tagList == "" {
			return displayAllTags(tlvList)
		}

		// Parse tag list
		tags := strings.Split(tagList, ",")
		for _, tag := range tags {
			tag = strings.TrimSpace(tag)
			if tag == "" {
				continue
			}

			// Find tag
			tlvItem, err := tlvList.FindTagRecursive(tag)
			if err != nil {
				fmt.Printf("Tag %s not found: %v\n", tag, err)
				continue
			}

			// Display tag value
			displayTagValue(tag, tlvItem)
		}

		return nil
	},
}

// displayAllTags displays all tags
func displayAllTags(tlvList tlv.TLVList) error {
	titleColor := color.New(color.FgCyan, color.Bold)

	fmt.Println(strings.Repeat("=", 50))
	titleColor.Println("TLV Data Parsing Results")
	fmt.Println(strings.Repeat("=", 50))

	// Recursively display TLV structure
	displayTLVStructure(tlvList, 0)

	return nil
}

// displayTLVStructure recursively displays TLV structure
func displayTLVStructure(tlvList tlv.TLVList, level int) {
	indent := strings.Repeat("  ", level)
	tagColor := color.New(color.FgYellow, color.Bold)
	valueColor := color.New(color.FgGreen)
	constructedColor := color.New(color.FgMagenta, color.Bold)

	for _, item := range tlvList {
		// Display tag
		tagColor.Printf("%sTag: %s", indent, item.Tag)

		// Display length
		fmt.Printf(", Length: %d", item.Length)

		// If it's a constructed tag, recursively display its contents
		if item.IsConstructed() {
			constructedColor.Println(" [Constructed]")

			// Parse nested TLV
			nestedList, err := tlv.Parse(item.Value)
			if err != nil {
				fmt.Printf("%s  Failed to parse nested TLV: %v\n", indent, err)
				continue
			}

			// Recursively display nested TLV
			displayTLVStructure(nestedList, level+1)
		} else {
			// Display value
			fmt.Print(", Value: ")

			// Display value based on data type
			switch dataType {
			case "utf8", "utf-8":
				data, err := tlv.GetTagValueAsString(hexData, item.Tag.String(), "utf-8")
				if err != nil {
					valueColor.Printf("%s\n", item.Value)
				} else {
					valueColor.Printf("%s\n", data)
				}
			case "ascii":
				data, err := tlv.GetTagValueAsString(hexData, item.Tag.String(), "ascii")
				if err != nil {
					valueColor.Printf("%s\n", item.Value)
				} else {
					valueColor.Printf("%s\n", data)
				}
			case "numeric":
				data, err := tlv.GetTagValueAsString(hexData, item.Tag.String(), "numeric")
				if err != nil {
					valueColor.Printf("%s\n", item.Value)
				} else {
					valueColor.Printf("%s\n", data)
				}
			default:
				valueColor.Printf("%s\n", item.Value)
			}
		}
	}
}

// displayTagValue displays tag value
func displayTagValue(tag string, tlvItem *tlv.TLV) {
	tagColor := color.New(color.FgYellow, color.Bold)
	valueColor := color.New(color.FgGreen)

	// Display tag
	tagColor.Printf("Tag: %s", tag)

	// Display length
	fmt.Printf(", Length: %d", tlvItem.Length)

	// Display value
	fmt.Print(", Value: ")

	// Display value based on data type
	switch dataType {
	case "utf8", "utf-8":
		data, err := tlv.GetTagValueAsString(hexData, tag, "utf-8")
		if err != nil {
			valueColor.Printf("%s\n", tlvItem.Value)
		} else {
			valueColor.Printf("%s\n", data)
		}
	case "ascii":
		data, err := tlv.GetTagValueAsString(hexData, tag, "ascii")
		if err != nil {
			valueColor.Printf("%s\n", tlvItem.Value)
		} else {
			valueColor.Printf("%s\n", data)
		}
	case "numeric":
		data, err := tlv.GetTagValueAsString(hexData, tag, "numeric")
		if err != nil {
			valueColor.Printf("%s\n", tlvItem.Value)
		} else {
			valueColor.Printf("%s\n", data)
		}
	default:
		valueColor.Printf("%s\n", tlvItem.Value)
	}
}

func init() {
	// Add command line flags
	TlvCmd.Flags().StringVar(&hexData, "hex", "", "Hex TLV data to parse")
	TlvCmd.Flags().StringVar(&tagList, "tag", "", "List of tags to extract, comma separated")
	TlvCmd.Flags().StringVar(&dataType, "type", "", "Data type (utf8, ascii, numeric)")
}
