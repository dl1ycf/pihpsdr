
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "rnnoise_data.h"

//
// DL1YCF:
// all the NN data is now contained in files that can be
// compiled separately, namely rnnoise_data_x.c with
// x = 1,2,3,4,5,6.
// These files include rnnoise_data_x.h which is a split-up
// of data originally contained HERE as static data
//
// So we define the types here and mark them as available.
// This data is then referenced in rnnoise_arrays[] that is
// constructed at the bottom of this file.
//

#define WEIGHTS_conv1_weights_float_DEFINED
#define WEIGHTS_conv1_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_conv1_bias_DEFINED
#define WEIGHTS_conv1_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_conv2_weights_int8_DEFINED
#define WEIGHTS_conv2_weights_int8_TYPE WEIGHT_TYPE_int8
#define WEIGHTS_conv2_weights_float_DEFINED
#define WEIGHTS_conv2_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_conv2_subias_DEFINED
#define WEIGHTS_conv2_subias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_conv2_scale_DEFINED
#define WEIGHTS_conv2_scale_TYPE WEIGHT_TYPE_float
#define WEIGHTS_conv2_bias_DEFINED
#define WEIGHTS_conv2_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_input_weights_int8_DEFINED
#define WEIGHTS_gru1_input_weights_int8_TYPE WEIGHT_TYPE_int8
#define WEIGHTS_gru1_input_weights_float_DEFINED
#define WEIGHTS_gru1_input_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_input_weights_idx_DEFINED
#define WEIGHTS_gru1_input_weights_idx_TYPE WEIGHT_TYPE_int
#define WEIGHTS_gru1_input_subias_DEFINED
#define WEIGHTS_gru1_input_subias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_input_scale_DEFINED
#define WEIGHTS_gru1_input_scale_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_input_bias_DEFINED
#define WEIGHTS_gru1_input_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_recurrent_weights_diag_DEFINED
#define WEIGHTS_gru1_recurrent_weights_diag_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_recurrent_weights_int8_DEFINED
#define WEIGHTS_gru1_recurrent_weights_int8_TYPE WEIGHT_TYPE_int8
#define WEIGHTS_gru1_recurrent_weights_float_DEFINED
#define WEIGHTS_gru1_recurrent_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_recurrent_weights_idx_DEFINED
#define WEIGHTS_gru1_recurrent_weights_idx_TYPE WEIGHT_TYPE_int
#define WEIGHTS_gru1_recurrent_subias_DEFINED
#define WEIGHTS_gru1_recurrent_subias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_recurrent_scale_DEFINED
#define WEIGHTS_gru1_recurrent_scale_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru1_recurrent_bias_DEFINED
#define WEIGHTS_gru1_recurrent_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_input_weights_int8_DEFINED
#define WEIGHTS_gru2_input_weights_int8_TYPE WEIGHT_TYPE_int8
#define WEIGHTS_gru2_input_weights_float_DEFINED
#define WEIGHTS_gru2_input_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_input_weights_idx_DEFINED
#define WEIGHTS_gru2_input_weights_idx_TYPE WEIGHT_TYPE_int
#define WEIGHTS_gru2_input_subias_DEFINED
#define WEIGHTS_gru2_input_subias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_input_scale_DEFINED
#define WEIGHTS_gru2_input_scale_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_input_bias_DEFINED
#define WEIGHTS_gru2_input_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_recurrent_weights_diag_DEFINED
#define WEIGHTS_gru2_recurrent_weights_diag_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_recurrent_weights_int8_DEFINED
#define WEIGHTS_gru2_recurrent_weights_int8_TYPE WEIGHT_TYPE_int8
#define WEIGHTS_gru2_recurrent_weights_float_DEFINED
#define WEIGHTS_gru2_recurrent_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_recurrent_weights_idx_DEFINED
#define WEIGHTS_gru2_recurrent_weights_idx_TYPE WEIGHT_TYPE_int
#define WEIGHTS_gru2_recurrent_subias_DEFINED
#define WEIGHTS_gru2_recurrent_subias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_recurrent_scale_DEFINED
#define WEIGHTS_gru2_recurrent_scale_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru2_recurrent_bias_DEFINED
#define WEIGHTS_gru2_recurrent_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_input_weights_int8_DEFINED
#define WEIGHTS_gru3_input_weights_int8_TYPE WEIGHT_TYPE_int8
#define WEIGHTS_gru3_input_weights_float_DEFINED
#define WEIGHTS_gru3_input_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_input_weights_idx_DEFINED
#define WEIGHTS_gru3_input_weights_idx_TYPE WEIGHT_TYPE_int
#define WEIGHTS_gru3_input_subias_DEFINED
#define WEIGHTS_gru3_input_subias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_input_scale_DEFINED
#define WEIGHTS_gru3_input_scale_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_input_bias_DEFINED
#define WEIGHTS_gru3_input_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_recurrent_weights_diag_DEFINED
#define WEIGHTS_gru3_recurrent_weights_diag_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_recurrent_weights_int8_DEFINED
#define WEIGHTS_gru3_recurrent_weights_int8_TYPE WEIGHT_TYPE_int8
#define WEIGHTS_gru3_recurrent_weights_float_DEFINED
#define WEIGHTS_gru3_recurrent_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_recurrent_weights_idx_DEFINED
#define WEIGHTS_gru3_recurrent_weights_idx_TYPE WEIGHT_TYPE_int
#define WEIGHTS_gru3_recurrent_subias_DEFINED
#define WEIGHTS_gru3_recurrent_subias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_recurrent_scale_DEFINED
#define WEIGHTS_gru3_recurrent_scale_TYPE WEIGHT_TYPE_float
#define WEIGHTS_gru3_recurrent_bias_DEFINED
#define WEIGHTS_gru3_recurrent_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_dense_out_weights_float_DEFINED
#define WEIGHTS_dense_out_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_dense_out_bias_DEFINED
#define WEIGHTS_dense_out_bias_TYPE WEIGHT_TYPE_float
#define WEIGHTS_vad_dense_weights_float_DEFINED
#define WEIGHTS_vad_dense_weights_float_TYPE WEIGHT_TYPE_float
#define WEIGHTS_vad_dense_bias_DEFINED
#define WEIGHTS_vad_dense_bias_TYPE WEIGHT_TYPE_float

extern float conv1_weights_float[24960];
extern float conv1_bias[128];
extern opus_int8 conv2_weights_int8[147456];
extern float conv2_weights_float[147456];
extern float conv2_subias[384];
extern float conv2_scale[384];
extern float conv2_bias[384];
extern opus_int8 gru1_input_weights_int8[442368];
extern float gru1_input_weights_float[442368];
extern int gru1_input_weights_idx[13968];
extern float gru1_input_subias[1152];
extern float gru1_input_scale[1152];
extern float gru1_input_bias[1152];
extern float gru1_recurrent_weights_diag[1152];
extern opus_int8 gru1_recurrent_weights_int8[442368];
extern float gru1_recurrent_weights_float[442368];
extern int gru1_recurrent_weights_idx[13968];
extern float gru1_recurrent_subias[1152];
extern float gru1_recurrent_scale[1152];
extern float gru1_recurrent_bias[1152];
extern opus_int8 gru2_input_weights_int8[442368];
extern float gru2_input_weights_float[442368];
extern int gru2_input_weights_idx[13968];
extern float gru2_input_subias[1152];
extern float gru2_input_scale[1152];
extern float gru2_input_bias[1152];
extern float gru2_recurrent_weights_diag[1152];
extern opus_int8 gru2_recurrent_weights_int8[442368];
extern float gru2_recurrent_weights_float[442368];
extern int gru2_recurrent_weights_idx[13968];
extern float gru2_recurrent_subias[1152];
extern float gru2_recurrent_scale[1152];
extern float gru2_recurrent_bias[1152];
extern opus_int8 gru3_input_weights_int8[442368];
extern float gru3_input_weights_float[442368];
extern int gru3_input_weights_idx[13968];
extern float gru3_input_subias[1152];
extern float gru3_input_scale[1152];
extern float gru3_input_bias[1152];
extern float gru3_recurrent_weights_diag[1152];
extern opus_int8 gru3_recurrent_weights_int8[442368];
extern float gru3_recurrent_weights_float[442368];
extern int gru3_recurrent_weights_idx[13968];
extern float gru3_recurrent_subias[1152];
extern float gru3_recurrent_scale[1152];
extern float gru3_recurrent_bias[1152];
extern float dense_out_weights_float[49152];
extern float dense_out_bias[32];
extern float vad_dense_weights_float[1536];
extern float vad_dense_bias[1];

const WeightArray rnnoise_arrays[] = {
    {"conv1_weights_float",  WEIGHTS_conv1_weights_float_TYPE, sizeof(conv1_weights_float), conv1_weights_float},
    {"conv1_bias",  WEIGHTS_conv1_bias_TYPE, sizeof(conv1_bias), conv1_bias},
    {"conv2_weights_int8",  WEIGHTS_conv2_weights_int8_TYPE, sizeof(conv2_weights_int8), conv2_weights_int8},
    {"conv2_weights_float",  WEIGHTS_conv2_weights_float_TYPE, sizeof(conv2_weights_float), conv2_weights_float},
    {"conv2_subias",  WEIGHTS_conv2_subias_TYPE, sizeof(conv2_subias), conv2_subias},
    {"conv2_scale",  WEIGHTS_conv2_scale_TYPE, sizeof(conv2_scale), conv2_scale},
    {"conv2_bias",  WEIGHTS_conv2_bias_TYPE, sizeof(conv2_bias), conv2_bias},
    {"gru1_input_weights_int8",  WEIGHTS_gru1_input_weights_int8_TYPE, sizeof(gru1_input_weights_int8), gru1_input_weights_int8},
    {"gru1_input_weights_float",  WEIGHTS_gru1_input_weights_float_TYPE, sizeof(gru1_input_weights_float), gru1_input_weights_float},
    {"gru1_input_weights_idx",  WEIGHTS_gru1_input_weights_idx_TYPE, sizeof(gru1_input_weights_idx), gru1_input_weights_idx},
    {"gru1_input_subias",  WEIGHTS_gru1_input_subias_TYPE, sizeof(gru1_input_subias), gru1_input_subias},
    {"gru1_input_scale",  WEIGHTS_gru1_input_scale_TYPE, sizeof(gru1_input_scale), gru1_input_scale},
    {"gru1_input_bias",  WEIGHTS_gru1_input_bias_TYPE, sizeof(gru1_input_bias), gru1_input_bias},
    {"gru1_recurrent_weights_diag",  WEIGHTS_gru1_recurrent_weights_diag_TYPE, sizeof(gru1_recurrent_weights_diag), gru1_recurrent_weights_diag},
    {"gru1_recurrent_weights_int8",  WEIGHTS_gru1_recurrent_weights_int8_TYPE, sizeof(gru1_recurrent_weights_int8), gru1_recurrent_weights_int8},
    {"gru1_recurrent_weights_float",  WEIGHTS_gru1_recurrent_weights_float_TYPE, sizeof(gru1_recurrent_weights_float), gru1_recurrent_weights_float},
    {"gru1_recurrent_weights_idx",  WEIGHTS_gru1_recurrent_weights_idx_TYPE, sizeof(gru1_recurrent_weights_idx), gru1_recurrent_weights_idx},
    {"gru1_recurrent_subias",  WEIGHTS_gru1_recurrent_subias_TYPE, sizeof(gru1_recurrent_subias), gru1_recurrent_subias},
    {"gru1_recurrent_scale",  WEIGHTS_gru1_recurrent_scale_TYPE, sizeof(gru1_recurrent_scale), gru1_recurrent_scale},
    {"gru1_recurrent_bias",  WEIGHTS_gru1_recurrent_bias_TYPE, sizeof(gru1_recurrent_bias), gru1_recurrent_bias},
    {"gru2_input_weights_int8",  WEIGHTS_gru2_input_weights_int8_TYPE, sizeof(gru2_input_weights_int8), gru2_input_weights_int8},
    {"gru2_input_weights_float",  WEIGHTS_gru2_input_weights_float_TYPE, sizeof(gru2_input_weights_float), gru2_input_weights_float},
    {"gru2_input_weights_idx",  WEIGHTS_gru2_input_weights_idx_TYPE, sizeof(gru2_input_weights_idx), gru2_input_weights_idx},
    {"gru2_input_subias",  WEIGHTS_gru2_input_subias_TYPE, sizeof(gru2_input_subias), gru2_input_subias},
    {"gru2_input_scale",  WEIGHTS_gru2_input_scale_TYPE, sizeof(gru2_input_scale), gru2_input_scale},
    {"gru2_input_bias",  WEIGHTS_gru2_input_bias_TYPE, sizeof(gru2_input_bias), gru2_input_bias},
    {"gru2_recurrent_weights_diag",  WEIGHTS_gru2_recurrent_weights_diag_TYPE, sizeof(gru2_recurrent_weights_diag), gru2_recurrent_weights_diag},
    {"gru2_recurrent_weights_int8",  WEIGHTS_gru2_recurrent_weights_int8_TYPE, sizeof(gru2_recurrent_weights_int8), gru2_recurrent_weights_int8},
    {"gru2_recurrent_weights_float",  WEIGHTS_gru2_recurrent_weights_float_TYPE, sizeof(gru2_recurrent_weights_float), gru2_recurrent_weights_float},
    {"gru2_recurrent_weights_idx",  WEIGHTS_gru2_recurrent_weights_idx_TYPE, sizeof(gru2_recurrent_weights_idx), gru2_recurrent_weights_idx},
    {"gru2_recurrent_subias",  WEIGHTS_gru2_recurrent_subias_TYPE, sizeof(gru2_recurrent_subias), gru2_recurrent_subias},
    {"gru2_recurrent_scale",  WEIGHTS_gru2_recurrent_scale_TYPE, sizeof(gru2_recurrent_scale), gru2_recurrent_scale},
    {"gru2_recurrent_bias",  WEIGHTS_gru2_recurrent_bias_TYPE, sizeof(gru2_recurrent_bias), gru2_recurrent_bias},
    {"gru3_input_weights_int8",  WEIGHTS_gru3_input_weights_int8_TYPE, sizeof(gru3_input_weights_int8), gru3_input_weights_int8},
    {"gru3_input_weights_float",  WEIGHTS_gru3_input_weights_float_TYPE, sizeof(gru3_input_weights_float), gru3_input_weights_float},
    {"gru3_input_weights_idx",  WEIGHTS_gru3_input_weights_idx_TYPE, sizeof(gru3_input_weights_idx), gru3_input_weights_idx},
    {"gru3_input_subias",  WEIGHTS_gru3_input_subias_TYPE, sizeof(gru3_input_subias), gru3_input_subias},
    {"gru3_input_scale",  WEIGHTS_gru3_input_scale_TYPE, sizeof(gru3_input_scale), gru3_input_scale},
    {"gru3_input_bias",  WEIGHTS_gru3_input_bias_TYPE, sizeof(gru3_input_bias), gru3_input_bias},
    {"gru3_recurrent_weights_diag",  WEIGHTS_gru3_recurrent_weights_diag_TYPE, sizeof(gru3_recurrent_weights_diag), gru3_recurrent_weights_diag},
    {"gru3_recurrent_weights_int8",  WEIGHTS_gru3_recurrent_weights_int8_TYPE, sizeof(gru3_recurrent_weights_int8), gru3_recurrent_weights_int8},
    {"gru3_recurrent_weights_float",  WEIGHTS_gru3_recurrent_weights_float_TYPE, sizeof(gru3_recurrent_weights_float), gru3_recurrent_weights_float},
    {"gru3_recurrent_weights_idx",  WEIGHTS_gru3_recurrent_weights_idx_TYPE, sizeof(gru3_recurrent_weights_idx), gru3_recurrent_weights_idx},
    {"gru3_recurrent_subias",  WEIGHTS_gru3_recurrent_subias_TYPE, sizeof(gru3_recurrent_subias), gru3_recurrent_subias},
    {"gru3_recurrent_scale",  WEIGHTS_gru3_recurrent_scale_TYPE, sizeof(gru3_recurrent_scale), gru3_recurrent_scale},
    {"gru3_recurrent_bias",  WEIGHTS_gru3_recurrent_bias_TYPE, sizeof(gru3_recurrent_bias), gru3_recurrent_bias},
    {"dense_out_weights_float",  WEIGHTS_dense_out_weights_float_TYPE, sizeof(dense_out_weights_float), dense_out_weights_float},
    {"dense_out_bias",  WEIGHTS_dense_out_bias_TYPE, sizeof(dense_out_bias), dense_out_bias},
    {"vad_dense_weights_float",  WEIGHTS_vad_dense_weights_float_TYPE, sizeof(vad_dense_weights_float), vad_dense_weights_float},
    {"vad_dense_bias",  WEIGHTS_vad_dense_bias_TYPE, sizeof(vad_dense_bias), vad_dense_bias},
    {NULL, 0, 0, NULL}
};

int init_rnnoise(RNNoise *model, const WeightArray *arrays) {
    if (linear_init(&model->conv1, arrays, "conv1_bias", NULL, NULL,"conv1_weights_float", NULL, NULL, NULL, 195, 128)) return 1;
    if (linear_init(&model->conv2, arrays, "conv2_bias", "conv2_subias", "conv2_weights_int8","conv2_weights_float", NULL, NULL, "conv2_scale", 384, 384)) return 1;
    if (linear_init(&model->gru1_input, arrays, "gru1_input_bias", "gru1_input_subias", "gru1_input_weights_int8","gru1_input_weights_float", "gru1_input_weights_idx", NULL, "gru1_input_scale", 384, 1152)) return 1;
    if (linear_init(&model->gru1_recurrent, arrays, "gru1_recurrent_bias", "gru1_recurrent_subias", "gru1_recurrent_weights_int8","gru1_recurrent_weights_float", "gru1_recurrent_weights_idx", "gru1_recurrent_weights_diag", "gru1_recurrent_scale", 384, 1152)) return 1;
    if (linear_init(&model->gru2_input, arrays, "gru2_input_bias", "gru2_input_subias", "gru2_input_weights_int8","gru2_input_weights_float", "gru2_input_weights_idx", NULL, "gru2_input_scale", 384, 1152)) return 1;
    if (linear_init(&model->gru2_recurrent, arrays, "gru2_recurrent_bias", "gru2_recurrent_subias", "gru2_recurrent_weights_int8","gru2_recurrent_weights_float", "gru2_recurrent_weights_idx", "gru2_recurrent_weights_diag", "gru2_recurrent_scale", 384, 1152)) return 1;
    if (linear_init(&model->gru3_input, arrays, "gru3_input_bias", "gru3_input_subias", "gru3_input_weights_int8","gru3_input_weights_float", "gru3_input_weights_idx", NULL, "gru3_input_scale", 384, 1152)) return 1;
    if (linear_init(&model->gru3_recurrent, arrays, "gru3_recurrent_bias", "gru3_recurrent_subias", "gru3_recurrent_weights_int8","gru3_recurrent_weights_float", "gru3_recurrent_weights_idx", "gru3_recurrent_weights_diag", "gru3_recurrent_scale", 384, 1152)) return 1;
    if (linear_init(&model->dense_out, arrays, "dense_out_bias", NULL, NULL,"dense_out_weights_float", NULL, NULL, NULL, 1536, 32)) return 1;
    if (linear_init(&model->vad_dense, arrays, "vad_dense_bias", NULL, NULL,"vad_dense_weights_float", NULL, NULL, NULL, 1536, 1)) return 1;
    return 0;
}
