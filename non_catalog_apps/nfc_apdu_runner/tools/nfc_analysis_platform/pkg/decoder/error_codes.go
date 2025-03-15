package decoder

// APDUErrorCodes 包含常见的APDU错误码及其描述
var APDUErrorCodes = map[string]string{
	"6700": "Wrong length",
	"6800": "Functions in CLA not supported",
	"6881": "Logical channel not supported",
	"6882": "Secure messaging not supported",
	"6883": "Last command of the chain expected",
	"6884": "Command chaining not supported",
	"6900": "Command not allowed",
	"6981": "Command incompatible with file structure",
	"6982": "Security status not satisfied",
	"6983": "Authentication method blocked",
	"6984": "Referenced data invalidated",
	"6985": "Conditions of use not satisfied",
	"6986": "Command not allowed (no current EF)",
	"6987": "Expected SM data objects missing",
	"6988": "SM data objects incorrect",
	"6A00": "Wrong parameters P1-P2",
	"6A80": "Incorrect parameters in the data field",
	"6A81": "Function not supported",
	"6A82": "File not found",
	"6A83": "Record not found",
	"6A84": "Not enough memory space in the file",
	"6A85": "Lc inconsistent with TLV structure",
	"6A86": "Incorrect parameters P1-P2",
	"6A87": "Lc inconsistent with P1-P2",
	"6A88": "Referenced data not found",
	"6A89": "File already exists",
	"6A8A": "DF name already exists",
	"6B00": "Wrong parameters P1-P2",
	"6C00": "Incorrect Le field",
	"6D00": "Instruction code not supported or invalid",
	"6E00": "Class not supported",
	"6F00": "Unknown error",
	"9000": "Success",
	"9100": "Success, but with proactive command to follow",
	"9200": "Success, but with data still available",
	"9300": "SIM Application Toolkit busy",
	"9400": "No EF selected",
	"9500": "Invalid offset",
	"9600": "No elementary file (EF) selected",
	"9700": "Incorrect PIN or PIN blocked",
	"9800": "Incorrect MAC",
	"9900": "Select failed",
	"9A00": "Application not found",
	"9B00": "Command aborted",
	"9C00": "Card terminated",
	"9D00": "Card reset",
	"9E00": "Card removed",
	"9F00": "Card blocked",
}

// IsSuccessStatusCode 检查状态码是否表示成功
func IsSuccessStatusCode(statusCode string) bool {
	return statusCode == "9000" || statusCode == "9100" || statusCode == "9200"
}

// GetErrorDescription 获取错误码的描述
func GetErrorDescription(statusCode string) string {
	if desc, ok := APDUErrorCodes[statusCode]; ok {
		return desc
	}

	// 尝试匹配前两个字节
	if len(statusCode) >= 2 {
		prefix := statusCode[:2] + "00"
		if desc, ok := APDUErrorCodes[prefix]; ok {
			return desc
		}
	}

	return "Unknown error code"
}
