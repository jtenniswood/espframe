idf_component_register(
    SRCS
        # Decoder core
        "src/dec/alpha_dec.c"
        "src/dec/buffer_dec.c"
        "src/dec/frame_dec.c"
        "src/dec/idec_dec.c"
        "src/dec/io_dec.c"
        "src/dec/quant_dec.c"
        "src/dec/tree_dec.c"
        "src/dec/vp8_dec.c"
        "src/dec/vp8l_dec.c"
        "src/dec/webp_dec.c"
        # DSP (generic C only -- no SIMD, no encoder)
        "src/dsp/alpha_processing.c"
        "src/dsp/cpu.c"
        "src/dsp/dec.c"
        "src/dsp/dec_clip_tables.c"
        "src/dsp/filters.c"
        "src/dsp/lossless.c"
        "src/dsp/rescaler.c"
        "src/dsp/upsampling.c"
        "src/dsp/yuv.c"
        # Utilities (decode-only)
        "src/utils/bit_reader_utils.c"
        "src/utils/color_cache_utils.c"
        "src/utils/filters_utils.c"
        "src/utils/huffman_utils.c"
        "src/utils/quant_levels_dec_utils.c"
        "src/utils/random_utils.c"
        "src/utils/rescaler_utils.c"
        "src/utils/thread_utils.c"
        "src/utils/utils.c"
    INCLUDE_DIRS "." "src"
    PRIV_INCLUDE_DIRS "."
)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
)
