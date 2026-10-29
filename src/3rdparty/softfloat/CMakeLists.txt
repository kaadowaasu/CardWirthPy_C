# Pinned SoftFloat 3e dependency for the editor's shared D real arithmetic.
set(softfloat_source "${CMAKE_CURRENT_SOURCE_DIR}/source")

# Exact transitive closure of the Real80 wrapper API: extF80 arithmetic,
# comparisons, f32/f64 and 64-bit integer conversions, remainder, and
# single-round normalization. The 8086-SSE specialization is selected
# uniformly so NaN handling is part of the shared behavior on each OS.
set(softfloat_sources
    "${softfloat_source}/softfloat_state.c"
    "${softfloat_source}/extF80_lt.c"
    "${softfloat_source}/8086-SSE/softfloat_raiseFlags.c"
    "${softfloat_source}/s_lt128.c"
    "${softfloat_source}/extF80_le.c"
    "${softfloat_source}/extF80_eq.c"
    "${softfloat_source}/extF80_rem.c"
    "${softfloat_source}/8086-SSE/s_propagateNaNExtF80UI.c"
    "${softfloat_source}/s_normRoundPackToExtF80.c"
    "${softfloat_source}/s_roundPackToExtF80.c"
    "${softfloat_source}/s_shiftRightJam64Extra.c"
    "${softfloat_source}/s_shiftRightJam64.c"
    "${softfloat_source}/s_shortShiftLeft128.c"
    "${softfloat_source}/s_countLeadingZeros64.c"
    "${softfloat_source}/s_countLeadingZeros8.c"
    "${softfloat_source}/s_normSubnormalExtF80Sig.c"
    "${softfloat_source}/s_mul64ByShifted32To128.c"
    "${softfloat_source}/s_sub128.c"
    "${softfloat_source}/s_add128.c"
    "${softfloat_source}/s_approxRecip32_1.c"
    "${softfloat_source}/s_approxRecip_1Ks.c"
    "${softfloat_source}/extF80_div.c"
    "${softfloat_source}/s_le128.c"
    "${softfloat_source}/extF80_mul.c"
    "${softfloat_source}/s_mul64To128.c"
    "${softfloat_source}/extF80_sub.c"
    "${softfloat_source}/s_subMagsExtF80.c"
    "${softfloat_source}/s_shiftRightJam128.c"
    "${softfloat_source}/s_addMagsExtF80.c"
    "${softfloat_source}/s_shortShiftRightJam64Extra.c"
    "${softfloat_source}/extF80_add.c"
    "${softfloat_source}/extF80_to_f64.c"
    "${softfloat_source}/8086-SSE/s_extF80UIToCommonNaN.c"
    "${softfloat_source}/8086-SSE/s_commonNaNToF64UI.c"
    "${softfloat_source}/s_roundPackToF64.c"
    "${softfloat_source}/s_shortShiftRightJam64.c"
    "${softfloat_source}/extF80_to_f32.c"
    "${softfloat_source}/8086-SSE/s_commonNaNToF32UI.c"
    "${softfloat_source}/s_roundPackToF32.c"
    "${softfloat_source}/s_shiftRightJam32.c"
    "${softfloat_source}/f64_to_extF80.c"
    "${softfloat_source}/8086-SSE/s_commonNaNToExtF80UI.c"
    "${softfloat_source}/8086-SSE/s_f64UIToCommonNaN.c"
    "${softfloat_source}/s_normSubnormalF64Sig.c"
    "${softfloat_source}/f32_to_extF80.c"
    "${softfloat_source}/8086-SSE/s_f32UIToCommonNaN.c"
    "${softfloat_source}/s_normSubnormalF32Sig.c"
    "${softfloat_source}/s_countLeadingZeros32.c"
    "${softfloat_source}/i64_to_extF80.c"
    "${softfloat_source}/ui64_to_extF80.c"
)

add_library(cardwirth_softfloat3e STATIC ${softfloat_sources})
target_include_directories(cardwirth_softfloat3e
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/build/cardwirth" "${softfloat_source}/include"
    PRIVATE "${softfloat_source}/8086-SSE"
)
# SoftFloat gates extF80_t-by-value API declarations on this definition.
target_compile_definitions(cardwirth_softfloat3e PUBLIC SOFTFLOAT_FAST_INT64)
set_target_properties(cardwirth_softfloat3e PROPERTIES POSITION_INDEPENDENT_CODE ON)


set_target_properties(cardwirth_softfloat3e PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
