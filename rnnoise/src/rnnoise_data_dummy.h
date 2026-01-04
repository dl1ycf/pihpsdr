static const float conv1_weights_float[24960] = { 0};
static const float conv1_bias[128] = { 0};
static const opus_int8 conv2_weights_int8[147456] = { 0};
static const float conv2_weights_float[147456] = { 0};
static const float conv2_subias[384] = { 0};
static const float conv2_scale[384] = { 0};
static const float conv2_bias[384] = { 0};
static const opus_int8 gru1_input_weights_int8[442368] = { 0};
static const float gru1_input_weights_float[442368] = { 0};
static const int gru1_input_weights_idx[13968] = { 0};
static const float gru1_input_subias[1152] = { 0};
static const float gru1_input_scale[1152] = { 0};
static const float gru1_input_bias[1152] = { 0};
static const float gru1_recurrent_weights_diag[1152] = { 0};
static const opus_int8 gru1_recurrent_weights_int8[442368] = { 0};
static const float gru1_recurrent_weights_float[442368] = { 0};
static const int gru1_recurrent_weights_idx[13968] = { 0};
static const float gru1_recurrent_subias[1152] = { 0};
static const float gru1_recurrent_scale[1152] = { 0};
static const float gru1_recurrent_bias[1152] = { 0};
static const opus_int8 gru2_input_weights_int8[442368] = { 0};
static const float gru2_input_weights_float[442368] = { 0};
static const int gru2_input_weights_idx[13968] = { 0};
static const float gru2_input_subias[1152] = { 0};
static const float gru2_input_scale[1152] = { 0};
static const float gru2_input_bias[1152] = { 0};
static const float gru2_recurrent_weights_diag[1152] = { 0};
static const opus_int8 gru2_recurrent_weights_int8[442368] = { 0};
static const float gru2_recurrent_weights_float[442368] = { 0};
static const int gru2_recurrent_weights_idx[13968] = { 0};
static const float gru2_recurrent_subias[1152] = { 0};
static const float gru2_recurrent_scale[1152] = { 0};
static const float gru2_recurrent_bias[1152] = { 0};
static const opus_int8 gru3_input_weights_int8[442368] = { 0};
static const float gru3_input_weights_float[442368] = { 0};
static const int gru3_input_weights_idx[13968] = { 0};
static const float gru3_input_subias[1152] = { 0};
static const float gru3_input_scale[1152] = { 0};
static const float gru3_input_bias[1152] = { 0};
static const float gru3_recurrent_weights_diag[1152] = { 0};
static const opus_int8 gru3_recurrent_weights_int8[442368] = { 0};
static const float gru3_recurrent_weights_float[442368] = { 0};
static const int gru3_recurrent_weights_idx[13968] = { 0};
static const float gru3_recurrent_subias[1152] = { 0};
static const float gru3_recurrent_scale[1152] = { 0};
static const float gru3_recurrent_bias[1152] = { 0};
static const float dense_out_weights_float[49152] = { 0};
static const float dense_out_bias[32] = { 0};
static const float vad_dense_weights_float[1536] = { 0};


#ifndef USE_WEIGHTS_FILE

#define WEIGHTS_vad_dense_bias_DEFINED
#define WEIGHTS_vad_dense_bias_TYPE WEIGHT_TYPE_float
static const float vad_dense_bias[1] = {
    0.27676787972450256
};


#endif /* USE_WEIGHTS_FILE */

#ifndef USE_WEIGHTS_FILE
const WeightArray rnnoise_arrays[] = {
#ifdef WEIGHTS_conv1_weights_float_DEFINED
    {"conv1_weights_float",  WEIGHTS_conv1_weights_float_TYPE, sizeof(conv1_weights_float), conv1_weights_float},
#endif
#ifdef WEIGHTS_conv1_bias_DEFINED
    {"conv1_bias",  WEIGHTS_conv1_bias_TYPE, sizeof(conv1_bias), conv1_bias},
#endif
#ifdef WEIGHTS_conv2_weights_int8_DEFINED
    {"conv2_weights_int8",  WEIGHTS_conv2_weights_int8_TYPE, sizeof(conv2_weights_int8), conv2_weights_int8},
#endif
#ifdef WEIGHTS_conv2_weights_float_DEFINED
    {"conv2_weights_float",  WEIGHTS_conv2_weights_float_TYPE, sizeof(conv2_weights_float), conv2_weights_float},
#endif
#ifdef WEIGHTS_conv2_subias_DEFINED
    {"conv2_subias",  WEIGHTS_conv2_subias_TYPE, sizeof(conv2_subias), conv2_subias},
#endif
#ifdef WEIGHTS_conv2_scale_DEFINED
    {"conv2_scale",  WEIGHTS_conv2_scale_TYPE, sizeof(conv2_scale), conv2_scale},
#endif
#ifdef WEIGHTS_conv2_bias_DEFINED
    {"conv2_bias",  WEIGHTS_conv2_bias_TYPE, sizeof(conv2_bias), conv2_bias},
#endif
#ifdef WEIGHTS_gru1_input_weights_int8_DEFINED
    {"gru1_input_weights_int8",  WEIGHTS_gru1_input_weights_int8_TYPE, sizeof(gru1_input_weights_int8), gru1_input_weights_int8},
#endif
#ifdef WEIGHTS_gru1_input_weights_float_DEFINED
    {"gru1_input_weights_float",  WEIGHTS_gru1_input_weights_float_TYPE, sizeof(gru1_input_weights_float), gru1_input_weights_float},
#endif
#ifdef WEIGHTS_gru1_input_weights_idx_DEFINED
    {"gru1_input_weights_idx",  WEIGHTS_gru1_input_weights_idx_TYPE, sizeof(gru1_input_weights_idx), gru1_input_weights_idx},
#endif
#ifdef WEIGHTS_gru1_input_subias_DEFINED
    {"gru1_input_subias",  WEIGHTS_gru1_input_subias_TYPE, sizeof(gru1_input_subias), gru1_input_subias},
#endif
#ifdef WEIGHTS_gru1_input_scale_DEFINED
    {"gru1_input_scale",  WEIGHTS_gru1_input_scale_TYPE, sizeof(gru1_input_scale), gru1_input_scale},
#endif
#ifdef WEIGHTS_gru1_input_bias_DEFINED
    {"gru1_input_bias",  WEIGHTS_gru1_input_bias_TYPE, sizeof(gru1_input_bias), gru1_input_bias},
#endif
#ifdef WEIGHTS_gru1_recurrent_weights_diag_DEFINED
    {"gru1_recurrent_weights_diag",  WEIGHTS_gru1_recurrent_weights_diag_TYPE, sizeof(gru1_recurrent_weights_diag), gru1_recurrent_weights_diag},
#endif
#ifdef WEIGHTS_gru1_recurrent_weights_int8_DEFINED
    {"gru1_recurrent_weights_int8",  WEIGHTS_gru1_recurrent_weights_int8_TYPE, sizeof(gru1_recurrent_weights_int8), gru1_recurrent_weights_int8},
#endif
#ifdef WEIGHTS_gru1_recurrent_weights_float_DEFINED
    {"gru1_recurrent_weights_float",  WEIGHTS_gru1_recurrent_weights_float_TYPE, sizeof(gru1_recurrent_weights_float), gru1_recurrent_weights_float},
#endif
#ifdef WEIGHTS_gru1_recurrent_weights_idx_DEFINED
    {"gru1_recurrent_weights_idx",  WEIGHTS_gru1_recurrent_weights_idx_TYPE, sizeof(gru1_recurrent_weights_idx), gru1_recurrent_weights_idx},
#endif
#ifdef WEIGHTS_gru1_recurrent_subias_DEFINED
    {"gru1_recurrent_subias",  WEIGHTS_gru1_recurrent_subias_TYPE, sizeof(gru1_recurrent_subias), gru1_recurrent_subias},
#endif
#ifdef WEIGHTS_gru1_recurrent_scale_DEFINED
    {"gru1_recurrent_scale",  WEIGHTS_gru1_recurrent_scale_TYPE, sizeof(gru1_recurrent_scale), gru1_recurrent_scale},
#endif
#ifdef WEIGHTS_gru1_recurrent_bias_DEFINED
    {"gru1_recurrent_bias",  WEIGHTS_gru1_recurrent_bias_TYPE, sizeof(gru1_recurrent_bias), gru1_recurrent_bias},
#endif
#ifdef WEIGHTS_gru2_input_weights_int8_DEFINED
    {"gru2_input_weights_int8",  WEIGHTS_gru2_input_weights_int8_TYPE, sizeof(gru2_input_weights_int8), gru2_input_weights_int8},
#endif
#ifdef WEIGHTS_gru2_input_weights_float_DEFINED
    {"gru2_input_weights_float",  WEIGHTS_gru2_input_weights_float_TYPE, sizeof(gru2_input_weights_float), gru2_input_weights_float},
#endif
#ifdef WEIGHTS_gru2_input_weights_idx_DEFINED
    {"gru2_input_weights_idx",  WEIGHTS_gru2_input_weights_idx_TYPE, sizeof(gru2_input_weights_idx), gru2_input_weights_idx},
#endif
#ifdef WEIGHTS_gru2_input_subias_DEFINED
    {"gru2_input_subias",  WEIGHTS_gru2_input_subias_TYPE, sizeof(gru2_input_subias), gru2_input_subias},
#endif
#ifdef WEIGHTS_gru2_input_scale_DEFINED
    {"gru2_input_scale",  WEIGHTS_gru2_input_scale_TYPE, sizeof(gru2_input_scale), gru2_input_scale},
#endif
#ifdef WEIGHTS_gru2_input_bias_DEFINED
    {"gru2_input_bias",  WEIGHTS_gru2_input_bias_TYPE, sizeof(gru2_input_bias), gru2_input_bias},
#endif
#ifdef WEIGHTS_gru2_recurrent_weights_diag_DEFINED
    {"gru2_recurrent_weights_diag",  WEIGHTS_gru2_recurrent_weights_diag_TYPE, sizeof(gru2_recurrent_weights_diag), gru2_recurrent_weights_diag},
#endif
#ifdef WEIGHTS_gru2_recurrent_weights_int8_DEFINED
    {"gru2_recurrent_weights_int8",  WEIGHTS_gru2_recurrent_weights_int8_TYPE, sizeof(gru2_recurrent_weights_int8), gru2_recurrent_weights_int8},
#endif
#ifdef WEIGHTS_gru2_recurrent_weights_float_DEFINED
    {"gru2_recurrent_weights_float",  WEIGHTS_gru2_recurrent_weights_float_TYPE, sizeof(gru2_recurrent_weights_float), gru2_recurrent_weights_float},
#endif
#ifdef WEIGHTS_gru2_recurrent_weights_idx_DEFINED
    {"gru2_recurrent_weights_idx",  WEIGHTS_gru2_recurrent_weights_idx_TYPE, sizeof(gru2_recurrent_weights_idx), gru2_recurrent_weights_idx},
#endif
#ifdef WEIGHTS_gru2_recurrent_subias_DEFINED
    {"gru2_recurrent_subias",  WEIGHTS_gru2_recurrent_subias_TYPE, sizeof(gru2_recurrent_subias), gru2_recurrent_subias},
#endif
#ifdef WEIGHTS_gru2_recurrent_scale_DEFINED
    {"gru2_recurrent_scale",  WEIGHTS_gru2_recurrent_scale_TYPE, sizeof(gru2_recurrent_scale), gru2_recurrent_scale},
#endif
#ifdef WEIGHTS_gru2_recurrent_bias_DEFINED
    {"gru2_recurrent_bias",  WEIGHTS_gru2_recurrent_bias_TYPE, sizeof(gru2_recurrent_bias), gru2_recurrent_bias},
#endif
#ifdef WEIGHTS_gru3_input_weights_int8_DEFINED
    {"gru3_input_weights_int8",  WEIGHTS_gru3_input_weights_int8_TYPE, sizeof(gru3_input_weights_int8), gru3_input_weights_int8},
#endif
#ifdef WEIGHTS_gru3_input_weights_float_DEFINED
    {"gru3_input_weights_float",  WEIGHTS_gru3_input_weights_float_TYPE, sizeof(gru3_input_weights_float), gru3_input_weights_float},
#endif
#ifdef WEIGHTS_gru3_input_weights_idx_DEFINED
    {"gru3_input_weights_idx",  WEIGHTS_gru3_input_weights_idx_TYPE, sizeof(gru3_input_weights_idx), gru3_input_weights_idx},
#endif
#ifdef WEIGHTS_gru3_input_subias_DEFINED
    {"gru3_input_subias",  WEIGHTS_gru3_input_subias_TYPE, sizeof(gru3_input_subias), gru3_input_subias},
#endif
#ifdef WEIGHTS_gru3_input_scale_DEFINED
    {"gru3_input_scale",  WEIGHTS_gru3_input_scale_TYPE, sizeof(gru3_input_scale), gru3_input_scale},
#endif
#ifdef WEIGHTS_gru3_input_bias_DEFINED
    {"gru3_input_bias",  WEIGHTS_gru3_input_bias_TYPE, sizeof(gru3_input_bias), gru3_input_bias},
#endif
#ifdef WEIGHTS_gru3_recurrent_weights_diag_DEFINED
    {"gru3_recurrent_weights_diag",  WEIGHTS_gru3_recurrent_weights_diag_TYPE, sizeof(gru3_recurrent_weights_diag), gru3_recurrent_weights_diag},
#endif
#ifdef WEIGHTS_gru3_recurrent_weights_int8_DEFINED
    {"gru3_recurrent_weights_int8",  WEIGHTS_gru3_recurrent_weights_int8_TYPE, sizeof(gru3_recurrent_weights_int8), gru3_recurrent_weights_int8},
#endif
#ifdef WEIGHTS_gru3_recurrent_weights_float_DEFINED
    {"gru3_recurrent_weights_float",  WEIGHTS_gru3_recurrent_weights_float_TYPE, sizeof(gru3_recurrent_weights_float), gru3_recurrent_weights_float},
#endif
#ifdef WEIGHTS_gru3_recurrent_weights_idx_DEFINED
    {"gru3_recurrent_weights_idx",  WEIGHTS_gru3_recurrent_weights_idx_TYPE, sizeof(gru3_recurrent_weights_idx), gru3_recurrent_weights_idx},
#endif
#ifdef WEIGHTS_gru3_recurrent_subias_DEFINED
    {"gru3_recurrent_subias",  WEIGHTS_gru3_recurrent_subias_TYPE, sizeof(gru3_recurrent_subias), gru3_recurrent_subias},
#endif
#ifdef WEIGHTS_gru3_recurrent_scale_DEFINED
    {"gru3_recurrent_scale",  WEIGHTS_gru3_recurrent_scale_TYPE, sizeof(gru3_recurrent_scale), gru3_recurrent_scale},
#endif
#ifdef WEIGHTS_gru3_recurrent_bias_DEFINED
    {"gru3_recurrent_bias",  WEIGHTS_gru3_recurrent_bias_TYPE, sizeof(gru3_recurrent_bias), gru3_recurrent_bias},
#endif
#ifdef WEIGHTS_dense_out_weights_float_DEFINED
    {"dense_out_weights_float",  WEIGHTS_dense_out_weights_float_TYPE, sizeof(dense_out_weights_float), dense_out_weights_float},
#endif
#ifdef WEIGHTS_dense_out_bias_DEFINED
    {"dense_out_bias",  WEIGHTS_dense_out_bias_TYPE, sizeof(dense_out_bias), dense_out_bias},
#endif
#ifdef WEIGHTS_vad_dense_weights_float_DEFINED
    {"vad_dense_weights_float",  WEIGHTS_vad_dense_weights_float_TYPE, sizeof(vad_dense_weights_float), vad_dense_weights_float},
#endif
#ifdef WEIGHTS_vad_dense_bias_DEFINED
    {"vad_dense_bias",  WEIGHTS_vad_dense_bias_TYPE, sizeof(vad_dense_bias), vad_dense_bias},
#endif
    {NULL, 0, 0, NULL}
};
#endif /* USE_WEIGHTS_FILE */

