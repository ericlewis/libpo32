package po35

/*
#include "po35_analysis.h"
*/
import "C"
import (
	libpo "github.com/ericlewis/libpo32/bindings/go"
	"unsafe"
)

// AnalysisSizeForPCM returns the encoded byte count for count mono samples.
// Rate must be 8000..192000 Hz, fps must be 20, 40, 60, or 80, and the rounded-up
// duration must fit at most 65535 speech records. On error it returns zero.
func AnalysisSizeForPCM(count int, rate, fps uint32) (int, error) {
	n, err := sizeArg(count)
	if err != nil {
		return 0, err
	}
	var out C.size_t
	if err = libpo.Status(C.po35_analysis_size_for_pcm(n, C.uint32_t(rate), C.uint32_t(fps), &out)).Err(); err != nil {
		return 0, err
	}
	return sizeResult(out)
}

// AnalysisEncodeF32 analyzes finite normalized mono PCM in [-1,1] using the C
// reconstruction model. It emits both coefficient banks in packed 44-byte
// records, zero-padding the final record. It is not the device's original
// speech encoder and performs no input or output normalization.
func AnalysisEncodeF32(pcm []float32, rate, fps uint32) ([]byte, error) {
	n, err := AnalysisSizeForPCM(len(pcm), rate, fps)
	if err != nil {
		return nil, err
	}
	records := make([]byte, n)
	var input *C.float
	if len(pcm) > 0 {
		input = (*C.float)(unsafe.Pointer(&pcm[0]))
	}
	var workspace C.po35_analysis_workspace_t
	var out C.size_t
	status := C.po35_analysis_encode_f32(&workspace, input, C.size_t(len(pcm)), C.uint32_t(rate), C.uint32_t(fps), bytePtr(records), C.size_t(n), &out)
	if err = libpo.Status(status).Err(); err != nil {
		return nil, err
	}
	return records[:int(out)], nil
}
