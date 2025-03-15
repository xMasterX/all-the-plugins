package tlv

import (
	"encoding/hex"
	"fmt"
	"math"
	"strings"
)

// Tag 表示TLV中的标签
type Tag []byte

// Value 表示TLV中的值
type Value []byte

// TLV 表示一个TLV结构
type TLV struct {
	Tag    Tag
	Length int
	Value  Value
}

// TLVList 表示TLV列表
type TLVList []*TLV

// String 返回Tag的十六进制字符串表示
func (t Tag) String() string {
	return hex.EncodeToString(t)
}

// String 返回Value的十六进制字符串表示
func (v Value) String() string {
	return hex.EncodeToString(v)
}

// String 返回TLV的字符串表示
func (t *TLV) String() string {
	return fmt.Sprintf("Tag: %s, Length: %d, Value: %s", t.Tag, t.Length, t.Value)
}

// IsConstructed 判断标签是否为构造型
func (t *TLV) IsConstructed() bool {
	if len(t.Tag) == 0 {
		return false
	}
	return (t.Tag[0] & 0x20) == 0x20
}

// Parse 解析TLV数据
func Parse(data []byte) (TLVList, error) {
	var result TLVList
	var offset int

	for offset < len(data) {
		tlv, newOffset, err := ParseOne(data, offset)
		if err != nil {
			return nil, err
		}

		result = append(result, tlv)
		offset = newOffset
	}

	return result, nil
}

// ParseOne 解析单个TLV结构
func ParseOne(data []byte, offset int) (*TLV, int, error) {
	if offset >= len(data) {
		return nil, offset, fmt.Errorf("offset out of range")
	}

	// 解析标签
	tag, newOffset, err := parseTag(data, offset)
	if err != nil {
		return nil, offset, err
	}
	offset = newOffset

	// 解析长度
	if offset >= len(data) {
		return nil, offset, fmt.Errorf("unexpected end of data when parsing length")
	}
	length, newOffset, err := parseLength(data, offset)
	if err != nil {
		return nil, offset, err
	}
	offset = newOffset

	// 解析值
	if offset+length > len(data) {
		return nil, offset, fmt.Errorf("unexpected end of data when parsing value")
	}
	value := data[offset : offset+length]
	offset += length

	return &TLV{
		Tag:    tag,
		Length: length,
		Value:  value,
	}, offset, nil
}

// parseTag 解析TLV标签
func parseTag(data []byte, offset int) (Tag, int, error) {
	if offset >= len(data) {
		return nil, offset, fmt.Errorf("offset out of range")
	}

	firstByte := data[offset]
	offset++

	// 检查是否是多字节标签
	if (firstByte & 0x1F) == 0x1F {
		// 多字节标签
		tag := []byte{firstByte}

		for offset < len(data) {
			b := data[offset]
			tag = append(tag, b)
			offset++

			// 如果最高位不是1，则标签结束
			if (b & 0x80) == 0 {
				break
			}
		}

		return tag, offset, nil
	}

	// 单字节标签
	return []byte{firstByte}, offset, nil
}

// parseLength 解析TLV长度
func parseLength(data []byte, offset int) (int, int, error) {
	if offset >= len(data) {
		return 0, offset, fmt.Errorf("offset out of range")
	}

	firstByte := data[offset]
	offset++

	if firstByte <= 0x7F {
		// 短格式长度
		return int(firstByte), offset, nil
	}

	// 长格式长度
	numBytes := int(firstByte & 0x7F)
	if numBytes == 0 {
		// 不定长度，不支持
		return 0, offset, fmt.Errorf("indefinite length form not supported")
	}

	if offset+numBytes > len(data) {
		return 0, offset, fmt.Errorf("unexpected end of data when parsing length")
	}

	length := 0
	for i := 0; i < numBytes; i++ {
		length = (length << 8) | int(data[offset])
		offset++
	}

	return length, offset, nil
}

// FindTag 在TLV列表中查找指定标签
func (list TLVList) FindTag(tagHex string) (*TLV, error) {
	tagBytes, err := hex.DecodeString(tagHex)
	if err != nil {
		return nil, fmt.Errorf("invalid tag hex: %w", err)
	}

	for _, tlv := range list {
		if strings.EqualFold(hex.EncodeToString(tlv.Tag), hex.EncodeToString(tagBytes)) {
			return tlv, nil
		}
	}

	return nil, fmt.Errorf("tag %s not found", tagHex)
}

// FindTagRecursive 递归查找指定标签
func (list TLVList) FindTagRecursive(tagHex string) (*TLV, error) {
	// 先在当前级别查找
	tlv, err := list.FindTag(tagHex)
	if err == nil {
		return tlv, nil
	}

	// 递归查找构造型TLV
	for _, t := range list {
		if t.IsConstructed() {
			// 解析构造型TLV的值
			nestedList, err := Parse(t.Value)
			if err != nil {
				continue
			}

			// 在嵌套TLV中查找
			nestedTLV, err := nestedList.FindTagRecursive(tagHex)
			if err == nil {
				return nestedTLV, nil
			}
		}
	}

	return nil, fmt.Errorf("tag %s not found", tagHex)
}

// ParseHex 从十六进制字符串解析TLV
func ParseHex(hexStr string) (TLVList, error) {
	// 移除空格
	hexStr = strings.ReplaceAll(hexStr, " ", "")

	// 解码十六进制
	data, err := hex.DecodeString(hexStr)
	if err != nil {
		return nil, fmt.Errorf("error decoding hex: %w", err)
	}

	return Parse(data)
}

// GetTagValue 获取指定标签的值（十六进制字符串）
func GetTagValue(hexData string, tagHex string) (string, error) {
	tlvList, err := ParseHex(hexData)
	if err != nil {
		return "", err
	}

	tlv, err := tlvList.FindTagRecursive(tagHex)
	if err != nil {
		return "", err
	}

	return tlv.Value.String(), nil
}

// GetTagValueAsString 获取指定标签的值（解码为字符串）
func GetTagValueAsString(hexData string, tagHex string, encoding string) (string, error) {
	value, err := GetTagValue(hexData, tagHex)
	if err != nil {
		return "", err
	}

	// 解码十六进制
	data, err := hex.DecodeString(value)
	if err != nil {
		return "", fmt.Errorf("error decoding hex: %w", err)
	}

	// 根据编码转换为字符串
	switch encoding {
	case "utf-8", "utf8":
		return string(data), nil
	case "ascii":
		var sb strings.Builder
		for _, b := range data {
			if b >= 32 && b <= 126 {
				sb.WriteByte(b)
			}
		}
		return sb.String(), nil
	case "numeric":
		// 处理数字格式（如BCD编码）
		var sb strings.Builder
		for _, b := range data {
			sb.WriteString(fmt.Sprintf("%02X", b))
		}
		return sb.String(), nil
	default:
		// 默认返回十六进制字符串
		return value, nil
	}
}

// GetTagValueAsInt 获取指定标签的值（解码为整数）
func GetTagValueAsInt(hexData string, tagHex string) (int, error) {
	value, err := GetTagValue(hexData, tagHex)
	if err != nil {
		return 0, err
	}

	// 解码十六进制
	data, err := hex.DecodeString(value)
	if err != nil {
		return 0, fmt.Errorf("error decoding hex: %w", err)
	}

	// 转换为整数
	result := 0
	for _, b := range data {
		result = (result << 8) | int(b)
	}

	return result, nil
}

// GetTagValueAsFloat 获取指定标签的值（解码为浮点数）
func GetTagValueAsFloat(hexData string, tagHex string, decimalPlaces int) (float64, error) {
	intValue, err := GetTagValueAsInt(hexData, tagHex)
	if err != nil {
		return 0, err
	}

	// 转换为浮点数
	divisor := math.Pow10(decimalPlaces)
	return float64(intValue) / divisor, nil
}
