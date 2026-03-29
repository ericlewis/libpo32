package po32

/*
#include "po32.h"
#include <stdlib.h>
*/
import "C"
import "unsafe"

// PatchParamsZero returns a zero-initialized PatchParams.
func PatchParamsZero() PatchParams {
	var c C.po32_patch_params_t
	C.po32_patch_params_zero(&c)
	return patchParamsFromC(&c)
}

// MorphPairsDefault returns the default morph pair array.
func MorphPairsDefault() [StateMorphPairCount]MorphPair {
	var c [StateMorphPairCount]C.po32_morph_pair_t
	C.po32_morph_pairs_default(&c[0], C.size_t(StateMorphPairCount))
	var out [StateMorphPairCount]MorphPair
	for i := range out {
		out[i] = morphPairFromC(&c[i])
	}
	return out
}

// PatchParseMtdrumText parses Microtonic .mtdrum patch text into parameters.
func PatchParseMtdrumText(text string) (PatchParams, error) {
	cText := C.CString(text)
	defer C.free(unsafe.Pointer(cText))
	var c C.po32_patch_params_t
	s := C.po32_patch_parse_mtdrum_text(cText, C.size_t(len(text)), &c)
	if err := statusToError(int(s)); err != nil {
		return PatchParams{}, err
	}
	return patchParamsFromC(&c), nil
}
