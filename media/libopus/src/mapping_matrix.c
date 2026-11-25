/* Copyright (c) 2017 Google Inc.
   Written by Andrew Allen */
/*
   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions
   are met:

   - Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.

   - Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
   OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
   EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
   PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
   PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
   LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
   SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "arch.h"
#include "float_cast.h"
#include "opus_private.h"
#include "opus_defines.h"
#include "mapping_matrix.h"

#define MATRIX_INDEX(nb_rows, row, col) (nb_rows * col + row)

opus_int32 mapping_matrix_get_size(int rows, int cols)
{
  opus_int32 size;

  /* Mapping Matrix must only support up to 255 channels in or out.
   * Additionally, the total cell count must be <= 65004 octets in order
   * for the matrix to be stored in an OGG header.
   */
  if (rows > 255 || cols > 255)
      return 0;
  size = rows * (opus_int32)cols * sizeof(opus_int16);
  if (size > 65004)
    return 0;

  return align(sizeof(MappingMatrix)) + align(size);
}

opus_int16 *mapping_matrix_get_data(const MappingMatrix *matrix)
{
  /* void* cast avoids clang -Wcast-align warning */
  return (opus_int16*)(void*)((char*)matrix + align(sizeof(MappingMatrix)));
}

void mapping_matrix_init(MappingMatrix * const matrix,
  int rows, int cols, int gain, const opus_int16 *data, opus_int32 data_size)
{
  int i;
  opus_int16 *ptr;

#if !defined(ENABLE_ASSERTIONS)
  (void)data_size;
#endif
  celt_assert(align(data_size) == align(rows * cols * sizeof(opus_int16)));

  matrix->rows = rows;
  matrix->cols = cols;
  matrix->gain = gain;
  ptr = mapping_matrix_get_data(matrix);
  for (i = 0; i < rows * cols; i++)
  {
     ptr[i] = data[i];
  }
}

void mapping_matrix_init_transposed(MappingMatrix * const matrix,
  int rows, int cols, int gain, const opus_int16 *data, opus_int32 data_size)
{
  int row, col;
  opus_int16 *ptr;

#if !defined(ENABLE_ASSERTIONS)
  (void)data_size;
#endif
  celt_assert(align(data_size) == align(rows * cols * sizeof(opus_int16)));

  matrix->rows = rows;
  matrix->cols = cols;
  matrix->gain = gain;
  ptr = mapping_matrix_get_data(matrix);
  for (col = 0; col < cols; col++)
    for (row = 0; row < rows; row++)
      ptr[MATRIX_INDEX(rows, row, col)] = data[MATRIX_INDEX(cols, col, row)];
}

#ifndef DISABLE_FLOAT_API
void mapping_matrix_multiply_channel_in_float(
    const MappingMatrix *matrix,
    const float *input,
    int input_rows,
    opus_res *output,
    int output_row,
    int output_rows,
    int frame_size)
{
  /* Matrix data is ordered col-wise. */
  opus_int16* matrix_data;
  int i, col;

  celt_assert(input_rows <= matrix->cols && output_rows <= matrix->rows);

  matrix_data = mapping_matrix_get_data(matrix);

  for (i = 0; i < frame_size; i++)
  {
    float tmp = 0;
    for (col = 0; col < input_rows; col++)
    {
      tmp +=
        matrix_data[MATRIX_INDEX(matrix->rows, output_row, col)] *
        input[MATRIX_INDEX(input_rows, col, i)];
    }
    output[output_rows * i] = FLOAT2RES((1/32768.f)*tmp);
  }
}

void mapping_matrix_multiply_channel_out_float(
    const MappingMatrix *matrix,
    const opus_res *input,
    int input_row,
    int input_rows,
    float *output,
    int output_rows,
    int frame_size
)
{
  /* Matrix data is ordered col-wise. */
  opus_int16* matrix_data;
  int i, row;
  float input_sample;

  celt_assert(input_rows <= matrix->cols && output_rows <= matrix->rows);

  matrix_data = mapping_matrix_get_data(matrix);

  for (i = 0; i < frame_size; i++)
  {
    input_sample = RES2FLOAT(input[input_rows * i]);
    for (row = 0; row < output_rows; row++)
    {
      float tmp =
        (1/32768.f)*matrix_data[MATRIX_INDEX(matrix->rows, row, input_row)] *
        input_sample;
      output[MATRIX_INDEX(output_rows, row, i)] += tmp;
    }
  }
}
#endif /* DISABLE_FLOAT_API */

void mapping_matrix_multiply_channel_in_short(
    const MappingMatrix *matrix,
    const opus_int16 *input,
    int input_rows,
    opus_res *output,
    int output_row,
    int output_rows,
    int frame_size)
{
  /* Matrix data is ordered col-wise. */
  opus_int16* matrix_data;
  int i, col;

  celt_assert(input_rows <= matrix->cols && output_rows <= matrix->rows);

  matrix_data = mapping_matrix_get_data(matrix);

  for (i = 0; i < frame_size; i++)
  {
    opus_val32 tmp = 0;
    for (col = 0; col < input_rows; col++)
    {
#if defined(FIXED_POINT)
      tmp +=
        ((opus_int32)matrix_data[MATRIX_INDEX(matrix->rows, output_row, col)] *
        (opus_int32)input[MATRIX_INDEX(input_rows, col, i)]) >> 8;
#else
      tmp +=
        matrix_data[MATRIX_INDEX(matrix->rows, output_row, col)] *
        input[MATRIX_INDEX(input_rows, col, i)];
#endif
    }
#if defined(FIXED_POINT)
#ifdef ENABLE_RES24
    output[output_rows * i] = SHL32(tmp, RES_SHIFT-7);
#else
    output[output_rows * i] = SATURATE16((tmp + 64) >> 7);
#endif
#else
    output[output_rows * i] = (1/(32768.f*32768.f))*tmp;
#endif
  }
}

void mapping_matrix_multiply_channel_out_short(
    const MappingMatrix *matrix,
    const opus_res *input,
    int input_row,
    int input_rows,
    opus_int16 *output,
    int output_rows,
    int frame_size)
{
  /* Matrix data is ordered col-wise. */
  opus_int16* matrix_data;
  int i, row;
  opus_int32 input_sample;

  celt_assert(input_rows <= matrix->cols && output_rows <= matrix->rows);

  matrix_data = mapping_matrix_get_data(matrix);

  for (i = 0; i < frame_size; i++)
  {
    input_sample = RES2INT16(input[input_rows * i]);
    for (row = 0; row < output_rows; row++)
    {
      opus_int32 tmp =
        (opus_int32)matrix_data[MATRIX_INDEX(matrix->rows, row, input_row)] *
        input_sample;
      output[MATRIX_INDEX(output_rows, row, i)] += (tmp + 16384) >> 15;
    }
  }
}

void mapping_matrix_multiply_channel_in_int24(
    const MappingMatrix *matrix,
    const opus_int32 *input,
    int input_rows,
    opus_res *output,
    int output_row,
    int output_rows,
    int frame_size)
{
  /* Matrix data is ordered col-wise. */
  opus_int16* matrix_data;
  int i, col;

  celt_assert(input_rows <= matrix->cols && output_rows <= matrix->rows);

  matrix_data = mapping_matrix_get_data(matrix);

  for (i = 0; i < frame_size; i++)
  {
    opus_val64 tmp = 0;
    for (col = 0; col < input_rows; col++)
    {
      tmp +=
        matrix_data[MATRIX_INDEX(matrix->rows, output_row, col)] *
        (opus_val64)input[MATRIX_INDEX(input_rows, col, i)];
    }
#if defined(FIXED_POINT)
    output[output_rows * i] = INT24TORES((tmp + 16384) >> 15);
#else
    output[output_rows * i] = INT24TORES((1/(32768.f))*tmp);
#endif
  }
}

void mapping_matrix_multiply_channel_out_int24(
    const MappingMatrix *matrix,
    const opus_res *input,
    int input_row,
    int input_rows,
    opus_int32 *output,
    int output_rows,
    int frame_size)
{
  /* Matrix data is ordered col-wise. */
  opus_int16* matrix_data;
  int i, row;
  opus_int32 input_sample;

  celt_assert(input_rows <= matrix->cols && output_rows <= matrix->rows);

  matrix_data = mapping_matrix_get_data(matrix);

  for (i = 0; i < frame_size; i++)
  {
    input_sample = RES2INT24(input[input_rows * i]);
    for (row = 0; row < output_rows; row++)
    {
      opus_int64 tmp =
        (opus_int64)matrix_data[MATRIX_INDEX(matrix->rows, row, input_row)] *
        input_sample;
      output[MATRIX_INDEX(output_rows, row, i)] += (tmp + 16384) >> 15;
    }
  }
}


/* Projection mixing matrices for 1st- to 5th-order ambisonics; 6th to 12th order are
 * in mapping_matrix_hoa.c.
 *
 * Each is the N3D spherical-harmonic matrix sampled at (N+1)^2 maximum-determinant
 * (Fekete) nodes and replaced by its polar factor U.V^T: energy-preserving decoding
 * (Zotter, Pomberger & Noisternig, Acta Acustica 98(1), 2012) at critical sampling.
 * The result is orthogonal, so demixing does not amplify coding noise, no gain field
 * is needed, and the demixing matrix is the exact transpose, derived rather than
 * stored (see mapping_matrix_init_transposed()).
 *
 * The data is column-wise, like every MappingMatrix, so each row of the matrix (each
 * stream) is a beam toward one node. The last two rows and columns pass the
 * non-diegetic channels through unchanged, so without them the encoder can use just
 * the top-left block.
 */
const MappingMatrix mapping_matrix_foa_mixing = { 6, 6, 0 };
const opus_int16 mapping_matrix_foa_mixing_data[36] = {
     16384,  16384,  16384,  16384,      0,      0,      0,      0,
     23170, -23170,      0,      0,  28377,  -9459,  -9459,  -9459,
         0,      0,      0,  26754, -13377, -13377,      0,      0,
         0,      0,      0,      0,  32767,      0,      0,      0,
         0,      0,      0,  32767
};

#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 2
const MappingMatrix mapping_matrix_soa_mixing = { 11, 11, 0 };
const opus_int16 mapping_matrix_soa_mixing_data[121] = {
     11093,  10836,  10836,  11093,  10836,  10836,  10836,  10836,
     11093,      0,      0,      0,   -877,  -8667,  11369,   8667,
     18212, -18212,    877, -11369,      0,      0,  17235,   7235,
    -14469,  -8618, -14469,   7235,   7235,   7235,  -8618,      0,
         0,      0,  18308, -10189,  -9671,  10189,   2070,  -2070,
    -18308,   9671,      0,      0,  -3001,   1071,  12720, -18149,
     12720,   6325,   6325,   1071, -18149,      0,      0,      0,
     -1910,  12258, -13498, -12258,  14168, -14168,   1910,  13498,
         0,      0,  25383,  -9199,   6633,  -1198,   6633,  -9199,
     -9199,  -9199,  -1198,      0,      0,      0,  14044,  14410,
     11482, -14410,    366,   -366, -14044, -11482,      0,      0,
      -487,  16772,   2066,  -2948,   2066, -15571, -15571,  16772,
     -2948,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,  32767,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
     32767
};
#endif

#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 3
const MappingMatrix mapping_matrix_toa_mixing = { 18, 18, 0 };
const opus_int16 mapping_matrix_toa_mixing_data[324] = {
      7921,   7921,   7921,   7921,   7921,   8956,   7921,   8956,
      7921,   7921,   8956,   7921,   8956,   7921,   7921,   7921,
         0,      0,    250,    250,  13629,   7310, -10469,   7186,
     -6757, -14164, -10469,  -3409,   -208,  -6757,   7186,  -3409,
      9916,   9916,      0,      0,  13870,  -6179,  -1356,  -6179,
      -985,   7913,   8521,  -2740,   8521, -11528,   7913, -11528,
    -13085,   -985,  -1356,   9186,      0,      0,     79,  12417,
     -2204, -10040,   9047, -10663,  -8612,  -4453,   3197,  -6922,
     12857,   3726,   2259, -13410,   9606,   3117,      0,      0,
        55,    297,  -4607, -11670, -15265, -11406,   9297,   9388,
     -5289,   3735,   -398,  -4209,   2416,   7265,  15304,   5086,
         0,      0,    305,    -88,  -2835,  -7182,   1780,   8464,
     -9407,   5778, -14429,   6253,   -245,  12538, -13996,    517,
     -2027,  14576,      0,      0,  17870,  -3614,  -8740,  -3614,
     -8720,  -1724,   1384,  -8827,   1384,   9435,  -1724,   9435,
     12275,  -8720,  -8740,   2640,      0,      0,     96, -12425,
       521,  10139,  -1647, -12559, -11735,   1816,   4241,  12950,
     15143,  -7043,  -4400,   2370,  -2050,   4582,      0,      0,
       -79,  12325, -14612,   3975,  -2127,   4619,   2216, -13455,
     -7960,   3111,  12299,  -2432,  -3463,  13592,   -720,  -7290,
         0,      0,  -1066,  -1582, -16537,  13805,  -8722,   9446,
     -5763,   9057,   5534,  -5068,  -1020,   2821,  -1776, -10449,
     11875,  -2606,      0,      0,  -2145,  -1720,  -2451,  15531,
      3287, -13537,  16009,  -3357,  -8462,  -8520,    204,   8934,
     -4287,  -4085,  -2298,   9637,      0,      0,   -125,    541,
    -11245,    778,   9619,   1416,  -3560,   9159,  -8785,  -5460,
      -920, -14642,  15904,   1487,  -8101,  10594,      0,      0,
     21953,   6901,   3890,   6901,   3630,  -8327,  -8020,   5937,
     -8020,  -3480,  -8327,  -3480,  -6263,   3630,   3890,  -8593,
         0,      0,    -39,    584,   1960,   -170, -11187,  -3638,
    -10250,   2879,   6369, -17764,   3793,  11445,   5000,  14679,
     -8041,   3331,      0,      0,   3075, -15916,   3349,  -3879,
      3144,   4761,   3129,   4812, -13945,  -6385,  14348,   5793,
      6144,  -2000,   3456, -13813,      0,      0,  -1383,  13858,
      7935,   1989, -13094,   1426,   4208,  11742,  -4505,   1586,
      9498,  -4499,  -2303, -11761, -13979,  -3379,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
     32767,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,  32767
};
#endif

#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 4
const MappingMatrix mapping_matrix_fourthoa_mixing = { 27, 27, 0 };
const opus_int16 mapping_matrix_fourthoa_mixing_data[729] = {
      6669,   6694,   6696,   6558,   6472,   6669,   5961,   6749,
      6696,   6749,   6600,   6472,   6472,   6600,   5961,   6696,
      6558,   5961,   6600,   6669,   6749,   6694,   6534,   6558,
      6694,      0,      0,   -164,     -4,   3600, -11451,  -8590,
      5150,  -1787,   2899,  -7271,  -6967,    960,  -3636,  -9198,
      9798,  -1847,  -6909,  -1533,   9227,   4995,  -8160,   7150,
     10767,   8629,  -2337,   6663,      0,      0,  11111,  -5193,
      2010,    429,   5408,  -5310,  -9872, -11311,   8475,   1987,
      2367,   1207,  -3223,   3181,   8037,  -8810,   8783,    950,
     -8043,  -5298,   8836,  -5523,  -1366,  -6787,   7957,      0,
         0,   -170,  10931, -10515,   1475,  -4774,  -8294,   4008,
     -2732,   1695,   9556,  10040, -10542,  -5553,   1100,   6980,
     -1486,  -7347,  -5539,   4206,   5371,  -3822,   -179,   8406,
     -9053,   6224,      0,      0,   1015,   -737,  -7730,  -3218,
      8828,  -8466,  -1802,  -1744,  -3426, -12333,   3042,   8209,
      9817,   3140,  -2705,   2338,   1569, -10243,   5650,  -8746,
     -5514,  -1298,  13170,   4182,   6911,      0,      0,    405,
       581,   2749,   -914,  -8233,  -6504,   3563,  -5232, -13096,
     -2973,    405,  -1431,   5300,   7037,  -2254,  11780,  -2963,
      1088,  -9366,   8732,  11389, -10419,  -2141,   3465,   9045,
         0,      0,  14742,  -2974,  -6464,  -7256,  -1927,  -2792,
      9779,  12699,   4257,  -6292,  -6421,  -6753,  -6431,  -5713,
      4459,   7168,   5023,  -5939,   5476,  -2836,   4617,  -3002,
     -7411,    808,   3232,      0,      0,    253, -10392,  -4907,
      1186,  -5564,   9242,  -7626,   5747,   2872,   3380,   5603,
     -1588,   2900,    656,  11243,   3430, -12941,  -1281,  -8133,
     -6930,  -6025,    756,  -2085,  11354,   8864,      0,      0,
       175,  10047,   9948, -12598,  -4630,   3379,   2042,   -408,
     -4744,   3594,  11590,   9180,  -5253, -11131,   3903,  -4973,
      4475,  -5559,   -768,  -3130,  -2673,  -9941,   -345,   8057,
      -233,      0,      0,   -618,   -778,   9868,  13417,  -1067,
      8918,  -1477,     22,   3001, -13415,   4996, -10988,  -1448,
    -10798,  -2236,   3029,   -794,   1206,   2981,  -2014,    456,
    -10209,   9225,  -5196,   3655,      0,      0,   1645,    -74,
     -4384,  -2205,  10280,  11648,   3609,   3703,  -4768,  -5226,
      2882,   2654,  -6402,   2454,  -6468,  -4701,   3126,  -2420,
     -9895,  12296, -10193,   1253,  -4758,  -7130,  12601,      0,
         0,   -414,    993,  -2457,   9894,   -795,   1799,  -5982,
      8125, -11228,   5136,  -1497,   4394,   5817,  -6385,  -2518,
    -11982,  -3565,  -8987,   9151,  -1275,  12469,    943,  -7589,
     -3613,   7652,      0,      0,  17596,   8069,  -4311,  -1913,
     -6760,   8757,  -9576, -12112,  -2095,  -4572,  -4613,  -2573,
      5919,  -4853,  -1328,   -795,  -1998,  -2684,   1863,   8864,
     -1985,   7426,   1661,   5148,  -4491,      0,      0,    383,
      -701,   6730,   -448,   -577,    190,  12126,  -9996,   1758,
     -7118,  -7224,   9660,   3196,   -490,  12187,  -3695, -13033,
      4140,   8527,    484,  -5080,   -403,  -6770,  -6525,   5830,
         0,      0,  -1267, -11640,   4404,  -1587,  -6521,  -4320,
     -4027,   1005,  -9104,   -346,   7031,   1956,   4714,  -8042,
      9833,   8125,  11059,   -992,   1086,   5014,  -5211,  11719,
       189, -11638,   -914,      0,      0,    -88,   9843,  -7230,
     -6145,   9657,    895,   -632,   1733,  -3165,  -5001,  10875,
     -6197,  12036,  -4982,   2485,   1733,  -2765,  13890,  -2963,
     -7335,   3725,    494,  -9800,  -5613,  -3728,      0,      0,
     -1450,    -82, -15013,   7699,  -9793,  -4348,  -1340,    314,
      4315,  -6495,   4417,  15069,  -8477,  -7526,  -2637,    -47,
      2560,  13708,    579,   3627,   2028,  -1200,    291,   3255,
      1779,      0,      0,   -391,   -574,   8110,    121,    338,
    -13971,   5377,    542,   6357,  -3558,   2386,  -6669,   3515,
    -10601,  -6637,  -8922,  -2895,    709,  -7480,   3372,     96,
     13519,  -1398,  10056,   8319,      0,      0,   3034,    140,
      3474,   2072,   4259,  -4956,  -7545,  -5610,  -6034,   7744,
      -900,  -5129,  -6047,   -357,  -6912,   7129,   3088,   9006,
     13362,  -4224, -11701,  -2085,  -8992,   5908,  10684,      0,
         0,    356,   1939,  -2768,   5011,   8431,   4286,   7469,
     -9577,  -7877,   5564,  -4349,   3160, -10220, -10140,    591,
     10160,  -2963,  -2807,  -7907, -10770,   6828,  10086,   4759,
        60,   1094,      0,      0,  18781,  -4047,   4589,   6107,
     -1977,  -5398,   5714,   7576,  -7431,   5516,   4047,   4924,
      1438,   2162,  -6885,  -3828,  -7558,   5834,  -5688,  -5467,
     -7433,  -3095,   5375,  -5490,  -7130,      0,      0,   -301,
     11940,   6419,   -141,   5984,  -8405, -14756,  11947,  -1900,
     -4343, -11028,   4007,  -6376,  -2133,   8936,   4367,  -6369,
      2329,  -5543,   4279,  -1949,    242,   3803,  -2230,    543,
         0,      0,  -1590,   3327,  -4422,   9392,   -781,   -401,
      6752,    369, -11861,  -2293,  -7647,  -7510,   1857,   2203,
     13323, -10990,  12464,   5047,  -3906,  -2324,  -6276,  -2256,
      2651,   9172,  -1143,      0,      0,   1867, -11832,   -858,
     -1751,  13466,  -2609,  -1091,  -1894,  -7342,  -3292,   8282,
     -2959,  -8960,   -662,   5876,  -3956,  -7738,   1475,   7310,
     11616,   6430,  -1923,   2060,   8712,  -9211,      0,      0,
      1433,   7073,   4191,  11359,  -2830,  -2944,   -765,  -1280,
      -332, -10859,  10943,   1254,  -7950,  11970,   1657,   2684,
       392,  -9831,   -803,  -5058,  -1283,   7449, -13283,   2134,
     -6258,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,  32767,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
     32767
};
#endif

#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 5
const MappingMatrix mapping_matrix_fifthoa_mixing = { 38, 38, 0 };
const opus_int16 mapping_matrix_fifthoa_mixing_data[1444] = {
      5618,   5234,   5408,   5538,   5638,   5638,   5487,   5508,
      5706,   5134,   5514,   5156,   5662,   5591,   5514,   5678,
      5706,   5487,   5156,   5508,   5529,   5624,   5134,   5502,
      5538,   5191,   5678,   5413,   5404,   5191,   5408,   5529,
      5404,   5618,   5037,   5413,      0,      0,    294,     27,
     -5166,   5506,   7192,    -71,  -3659,  -5684,   9303,   3326,
      6980,   8695,   5758,  -4491,  -2747,  -4350,   5281,  -7053,
      -151,  -9224,  -5760,  -2409,    150,   7320,   1416,   2747,
      1920,  -8821,   2597,   7857,   2177,   5435,  -7178,  -8682,
     -5835,  -2713,      0,      0,   9453,  -2241,  -1976,  -1727,
      3640,  -5960,   7522,   7894,  -2072,   2883,   3785,   3064,
     -2370,   1197,  -9071,  -5232,  -7387,   3035,  -8627,   3215,
     -6853,   3949,  -1314,  -6215,  -7131,  -7930,   3055,  -1073,
      7477,  -1177,   7729,   7944,  -5442,  -2410,   3386,   7000,
         0,      0,    145,   8712,  -8088,   7792,  -4796,  -7243,
     -4240,    932,  -2413,  -7705,   5725,   -278,  -7795,   8637,
      2448,   6822,  -3768,  -5384,  -3258,   -260,  -3542,  -8344,
     -8775,   2656,   6415,   1267,   8934,   4464,   4455,   2988,
     -5615,    229,   1162,  -2878,   4035,   6521,      0,      0,
      -329,   -104,   9664,   9873,  -8522,    320,   3415,   -672,
     -5420,  -6088,   8875,    -70, -10060,  -8561,  -1076,  -7442,
     -4346,   9175,   -118,    534,   5088,   4583,   -624,   4334,
      2630,    608,   3907,  -7981,   2835,   6264,  -2669,    338,
     -2764,   5711,  -7570,  -3718,      0,      0,    181,    223,
      2815,  -2702,   6540,    231,  -6682,  -9938,  -4826,   1811,
      6721,   6287,  -2548,  -1026,   5960,   5317,  -9418,  -5648,
       878,  -6196,   8709,  -1696,   -406,  -9431,  -2727,  -5993,
      1147,   2325,   4268,  -2527,   4145,  10212,   9414,   4695,
     -6388,  -3776,      0,      0,  12291,  -4500,  -5569,  -5296,
     -3162,   1244,   5398,   6120,  -5482,  -4132,  -3312,  -3538,
     -5138,  -6089,  10273,   -907,   4079,  -3864,   9278,  -4135,
      3805,  -3077,  -5373,   1082,   3345,   9331,  -4514,  -5842,
      5777,  -5544,   5904,   5887,    272,  -5513,  -2624,   3499,
         0,      0,    359,  -4453,   3665,  -3684,  -4367,  10742,
     -7931,   1964,    727,  -5335,   4560,    439,   4385,   2053,
     -4338,  -8529,   6507,  -3393,   5969,   -721,   5908,  -7655,
      2268,  -3628, -10706,  -2610,   6814,   -785,   8795,   -679,
     -9708,   -264,  -1959,   1582,   4308,   9654,      0,      0,
       327,   9020,   4152,   3715,  -3576,   6338,    -80,  -3200,
     -9251,   6263,  -2304,  -8423,   2953,   6559,    -16,   3140,
     -1502,  -2419,    834,  -9720,  -1803,   7867,   9515,  -4818,
      4476,   -866,   9149,  -6488,   1892,  -7355,   3098,  -3903,
     -6355,  -7994,  -2712,   3525,      0,      0,      2,    -10,
    -11184,  10691,   2361,     40,  -1631,   1968,  -7362,   7880,
      3925,  -9442,  10036, -11084,   -357,  -7229,    535,  -4009,
      -114,  10010,  -1415,  -5883,    795,  -2603,   2659,    222,
      5507,   1876,   2258,  -4774,   2099,  -2255,   5186,   6445,
     -1713,  -3885,      0,      0,   -522,    401,  -6139,  -5724,
     -8542,   -537,   7771,  -2047,   2815,  -5018,   9003,    209,
      5941,  -2352,   3441,  10072,   9480,   7980,   -547,    133,
     -9355,   4604,    870,  -7357,  -4979,  -1891,   3258,   2268,
      7028,  -1510,  -5889,   -239,   4193,  -3184,  -7928,  -7621,
         0,      0,   -449,    155,   3589,  -4193,  -1120,   -418,
     -7458, -11102,  -6218,  -1863,  -1032,  -3591,  -3857,   3546,
     -8394,  -2002,   8875,   2754,   -505,   3347,  -8602,    451,
        97,   6492,   2665,   8092,   -726,   8193,   4956,  -7210,
      4933,  12438,  -5753,   7061,    110,  -3425,      0,      0,
     14778,   4969,   3882,   4104,  -5853,   5207,    694,   1975,
      4251,  -5624,  -6327,  -5304,   4802,  -2702,  -8473,   6026,
      2208,  -5391,  -9953,  -5909,   2247,  -6033,   2448,   5279,
      1929,  -9235,  -6420,   3114,   1467,   2271,   1129,   1124,
      4135,   5023,  -6109,  -1986,      0,      0,    865,  -5467,
      5848,  -5738,   1154,  -6623,  -9111,   2303,   1945,   4162,
      -855,   -163,   4488,  -7391,   6962,   2655,  -6030,   2331,
     -9352,     70,  -5759,   1630,   7040,   2445,   9928,   3663,
     -3981,  -3752,  10016,  -2700, -11680,   -315,   1196,   2067,
      -391,   8817,      0,      0,    853,  -5536,  -2198,  -1968,
     -3615, -10543,   1124,  -7812,   6193,   5699,  -1718,  -7858,
     -2723,   1699,    968,  -4461,   3297,  -2751,  -2262,  -7486,
      3270,   8258,  -3047,   8493,  -8957,   1492,   7684,   2141,
      3370,   1534,   5563,  -7987,   9445,   4954,  -3512,   7739,
         0,      0,    290,  10012,   2262,  -3097,   8898,  -5003,
      1843,  -1449,   8242,  -3249,  -7938,   -477,   4218,   1814,
      -786,  -1444,   2807,   8799,   -101,   1122,   3550,  -6172,
    -10794,  -4874,   2528,    102,   8092, -11214,   -263,  -8843,
     -1668,    163,  -2761,   8052,  -7165,   1847,      0,      0,
      1017,    315,   8016,   7073,   6830,    581,   -192,     71,
     10282,  -9807,  -2362,   -970,  -5045, -11313,   -354,  -3813,
       623,  -5160,    245,   -379,  -1680,   8169,  -1110,  -3631,
      1654,    445,   6385,  10095,    536, -11331,  -1897,   -526,
      3621,  -9875,   5211,  -2751,      0,      0,   -357,    533,
      6550,  -6157,   2184,   1384,  -3433,   4165,   6999,   8838,
      4557, -10262,  -8895,  -4314,    755,  11600,  -1479,  -2491,
     -1395,   8405,   3770,  -8199,   -266,   3137,  -6515,   1652,
      5081,    -20,   4818,   1839,   4445,  -4162,  -9439,  -4509,
     -2033,  -7074,      0,      0,  -1077,   -611,  -3534,  -4004,
     -2231,    433,  12113,  -4205,   3786,   2884,    716,   1021,
      5072,   7592,  -6520,  -5700, -12019,  -3207,   1475,   1693,
     10263,   -298,   -469,   6591,   5316,   3883,  -2008,   5618,
     10093,  -6466,  -8325,   -494,  -2543,  -4273,  -2661,  -7658,
         0,      0,   -283,   -138,  -3781,   3182,  -6199,   -883,
     -5508,  -9285,   6723,  -3417,  -6666,  -8502,   4336,   1871,
     10617,  -3712,  -3049,   7059,   1585,   8092,   3446,   1723,
      1007,   2325,    -41, -12332,  -1331,  -4081,   3976,   4023,
      3754,   9636,  -1933,  -7725,   6825,  -1786,      0,      0,
     17340,   2968,   3005,   3185,  -1032,  -7527,  -4319,  -3485,
      3102,    -52,   -994,   -181,   2894,   4488,   5594,  -5010,
     -7516,    623,   8765,    381,  -6290,  -1386,   5168,  -6937,
     -6944,   8243,    -59,   5559,  -4018,   4865,  -4329,  -4288,
     -6155,   3601,  -2352,  -5512,      0,      0,    239,   8376,
     -5830,   5853,   4433,  -1831,  -8321,   3519,  -1897,   7397,
     -5136,    510,  -5776,  -3286,  -8005,   3897,   2063,   5634,
     12343,   -177,   1958,   8337,  -4300,   1302,  -4425,  -4955,
    -10247,   2713,   9103,    696,  -7387,  -1314,    629,  -2688,
     -5311,   2887,      0,      0,   1114,  -5149,  -1998,  -2014,
      -546,  10473,   1877, -10372,   3352,  -1631,  -1189,   2198,
       205,  -4750,   -852,   3004,  -4411,  -1543,   4288,   2547,
     -5076,   2649,  -9069,  -7007,  10164,  -3165,  -1001,   4675,
      5911,   6331,   7109, -11379,  -8237,   6086,   -697,   7539,
         0,      0,   -358,  -8624,  -1202,   2344,  10231,   8356,
      2662,  -1633,  -5276,  -4372,  -9974,   -604,  -2022,   2231,
        33,   2012,  -6955,   8872,   1412,   1516,  -7358,  -7556,
      4525,   7512,  -4155,    894,   8292,   3292,    288,   3728,
     -1642,    313,   6667,  -6522, -10823,   3165,      0,      0,
      -434,  11674,  -7141,  -9776,  -6122,   3906,   -264,    369,
      4350,    569,  -7268,   8440,  -9837,  -3913,    601,  -5937,
      -752,  -9016,   -207,  10395,  -2593,   3589,  10983,    427,
      1355,   -624,   6068,  -2442,  -1226,   2423,   -266,   1972,
      3705,   3027,  -5924,   1418,      0,      0,   -649,     82,
     -4281,   1872,  -9265,     51,   -407,    778,   1182,   9197,
     -8694,  10782,  -2591,  -8620,   1192,    507,   -487,  11319,
     -1435, -12117,   2508,  -8972,   2195,   -366,    -31,    175,
      7380,   7058,   2384,  -3112,   2423,   -285,  -3637,   1688,
      8702,  -4232,      0,      0,    366,    530,  -6980,  -4773,
      8739,  -2076,   -897,   2444,  -7741, -11822,  -5216,   -634,
      4970,  -4650,     66,   6695,  -2717,  -3738,    -52,  -3582,
      3928,   9466,   2707,   6305,  -4374,    229,   7094,  -3142,
      4883,   5312,  -1738,  -2008,  -7139,   8309,   9026,  -8344,
         0,      0,  -1587,  -2549,   3164,  -3049,    656,  -3312,
     -8617,   9445,    205,  -3674,  -2196,   1971,  -1046,   9191,
     -2530,  -7501,   2559,   1798,   1855,  -1741,  -2550,  -1672,
      1308,  -4251,   8496,  -1740,    583,  -2407,  11605,   4179,
     10162,  -9167,   8575,  -2511,   -678, -12393,      0,      0,
      -292,   -726,   7453,   5623,   4092,    133,  12309,  -5763,
     -3659,   7526,  -7507,    555,  -6978,   2812,   6311,  -1324,
      7403,  -8438,   -150,    496,  -7970,  -4092,  -1908,  -1903,
     -2387,  -5018,  -2081,  -5565,   8927,   3192,  -8780,    233,
      2089,   4522,   7465,  -5655,      0,      0,    438,  -1218,
      -795,   2359,  -3655,   1558,   -406,  -5556,   2668,    990,
     -5450,  -3423,    612,  -2627, -13129,   6186,  -3494,    878,
     -2251,   2257,   2816,    950,  -1012, -11053,  -1189,  15938,
       765,  -8893,   1670,   8397,   -790,   4788,   9811,  -3703,
      7373,   -263,      0,      0,  16424,  -5538,  -3871,  -4353,
      3260,   3632,  -5560,  -7058,  -4588,   5543,   4223,   4678,
     -4906,   2343,  -2624,    515,   6172,   4498,  -4454,   5174,
      4826,   3751,  -3712,   3170,   6157,  -4873,   5729,  -4516,
     -6920,  -3150,  -7117,  -5913,   3311,  -5211,   4541,  -4840,
         0,      0,   -781,   2717,  -4404,   3190,   2419,  11418,
     -4328,   3551,   -467,   1698,  -3529,   -582,  -1482,   4765,
      9806,  -6718,   2471,   1598, -17865,  -1028,   4141,   6525,
     -7612,  -4963,  -5356,   8458,  -3212,   5222,   3314,   3369,
     -1950,    411,  -4217,   -334,  -5092,  -2712,      0,      0,
       898,   8291,   1927,   3111,   1773,  -3534,   1468,  -9731,
     -6087,  -6441,   1623,   9322,   2361,  -2889,   1694,    737,
      2215,   2385,  -5802,   7065,   3929,  -5554,   5564,   3153,
     -8170,   4341,  -8297,  -2442,   6588,  -5219,   7952, -12354,
       981,  -6092,   2540,   4483,      0,      0,   -979,  -3351,
     -2238,   -652,   6020, -10626,   4602,  -2968,  -2999,  -2060,
     -2494,  -3735,  -3554,  -1185,  -1917,  -1561,  11066,    730,
     -2564,     22,  11675,  -3361,  10396, -10090,   7845,  -1615,
      1036,   8151,    -36,   9436,  -2636,   1356,  -6886,  -3877,
     -5140,   4236,      0,      0,   -753,  -9864,   6147,   7262,
     -9013,  -6354,  -3394,   3349,  -4125,   1445,  -9759,  11578,
      7433,   2129,    -86,   7071,   2912,  -8166,    259,   9561,
      3584,   4551,  -5904,   -133,  -1847,   -138,   6045,     34,
       -42,  -2136,   1184,   2030,  -4703,  -2468,  -8313,   -571,
         0,      0,    211,  10824,  10152, -11514,  -1104,  -1993,
      1267,   -504,  -9750,   3575,  -1323,   -789,   9363,  -7701,
       417,  -5895,   -205,    -68,    630,   -316,  -1436,   -591,
    -11203,   2915,    756,   -920,   3853,   6857,  -1668,  11034,
     -1133,   1391,   3763,  -8844,    987,    569,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,  32767,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,      0,      0,      0,      0,      0,
         0,      0,      0,  32767
};
#endif

/* The built-in mixing matrices, indexed by ambisonic order. Order 0 has none. */
typedef struct {
  const MappingMatrix *matrix;
  const opus_int16 *data;
  opus_int32 data_size;
} AmbisonicMatrix;

#define MAPPING_MATRIX_ENTRY(stem) \
  { &mapping_matrix_ ## stem ## _mixing, mapping_matrix_ ## stem ## _mixing_data, \
    sizeof(mapping_matrix_ ## stem ## _mixing_data) }

static const AmbisonicMatrix ambisonic_mixing_matrices[
    MAPPING_MATRIX_MAX_AMBISONIC_ORDER + 1] = {
  { NULL, NULL, 0 },
  MAPPING_MATRIX_ENTRY(foa),
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 2
  MAPPING_MATRIX_ENTRY(soa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 3
  MAPPING_MATRIX_ENTRY(toa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 4
  MAPPING_MATRIX_ENTRY(fourthoa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 5
  MAPPING_MATRIX_ENTRY(fifthoa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 6
  MAPPING_MATRIX_ENTRY(sixthoa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 7
  MAPPING_MATRIX_ENTRY(seventhoa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 8
  MAPPING_MATRIX_ENTRY(eighthoa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 9
  MAPPING_MATRIX_ENTRY(ninthoa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 10
  MAPPING_MATRIX_ENTRY(tenthoa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 11
  MAPPING_MATRIX_ENTRY(eleventhoa),
#endif
#if MAPPING_MATRIX_MAX_AMBISONIC_ORDER >= 12
  MAPPING_MATRIX_ENTRY(twelfthoa)
#endif
};

int mapping_matrix_get_ambisonic(int order, const MappingMatrix **matrix,
  const opus_int16 **data, opus_int32 *data_size)
{
  const AmbisonicMatrix *entry;

  if (order < 1 || order > MAPPING_MATRIX_MAX_AMBISONIC_ORDER)
    return OPUS_UNIMPLEMENTED;

  entry = &ambisonic_mixing_matrices[order];
  if (matrix)
    *matrix = entry->matrix;
  if (data)
    *data = entry->data;
  if (data_size)
    *data_size = entry->data_size;
  return OPUS_OK;
}
