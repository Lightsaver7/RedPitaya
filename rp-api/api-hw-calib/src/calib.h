/**
 * $Id: $
 *
 * @brief Red Pitaya Calibration Module.
 *
 * @Author Red Pitaya
 *
 * (c) Red Pitaya  http://www.redpitaya.com
 *
 * This part of code is written in C programming language.
 * Please visit http://en.wikipedia.org/wiki/C_(programming_language)
 * for more details on the language used herein.
 */

#ifndef __CALIB_H
#define __CALIB_H

#include <stdint.h>
#include <stdio.h>
#include "rp_hw-calib.h"
#include "calib_common.h"

int calib_Init(bool use_factory_zone);
int calib_InitModel(rp_HPeModels_t model,bool use_factory_zone);

rp_calib_params_t calib_GetParams();
rp_calib_params_t calib_GetDefaultCalib();

int calib_WriteParams(rp_HPeModels_t model, rp_calib_params_t *calib_params,bool use_factory_zone);
int calib_SetParams(rp_calib_params_t *calib_params);
int calib_WriteDirectlyParams(rp_calib_params_t *calib_params,bool use_factory_zone);

void calib_SetToZero();
int calib_LoadFromFactoryZone();
int calib_Reset(bool use_factory_zone);
int calib_GetEEPROM(rp_eepromWpData_t *calib_params,bool use_factory_zone);
int calib_ConvertEEPROM(rp_eepromWpData_t *calib_params,rp_calib_params_t *out);

int calib_Print(rp_calib_params_t *calib);
int calib_PrintEx(FILE *__restrict out,rp_calib_params_t *calib);

int calib_GetFastADCFilter(rp_channel_calib_t channel,channel_filter_t *out);
int calib_GetFastADCFilter_1_20(rp_channel_calib_t channel,channel_filter_t *out);

int calib_GetFastADCCalibValue(rp_channel_calib_t channel,rp_acq_ac_dc_mode_calib_t mode, double *gain,int32_t *offset, uint_gain_calib_t *calib);
int calib_GetFastADCCalibValue_1_20(rp_channel_calib_t channel,rp_acq_ac_dc_mode_calib_t mode, double *gain,int32_t *offset, uint_gain_calib_t *calib);

int calib_GetFastDACCalibValue(rp_channel_calib_t channel,rp_gen_gain_calib_t mode, double *gain,int32_t *offset, uint_gain_calib_t *calib);

              // int calib_SetFrontEndOffset(rp_channel_t channel, rp_pinState_t gain, rp_calib_params_t* out_params);
              // int calib_SetFrontEndScaleLV(rp_channel_t channel, float referentialVoltage, rp_calib_params_t* out_params);
              // int calib_SetFrontEndScaleHV(rp_channel_t channel, float referentialVoltage, rp_calib_params_t* out_params);


          // int32_t calib_GetDataMedian(rp_channel_t channel, rp_pinState_t gain);
          //   float calib_GetDataMedianFloat(rp_channel_t channel, rp_pinState_t gain);
          //     int calib_GetDataMinMaxFloat(rp_channel_t channel, rp_pinState_t gain, float* min, float* max);

              // int calib_setCachedParams();

// #if defined Z10 || defined Z20_125 || defined Z20_250_12 || defined Z20
//               int calib_SetBackEndOffset(rp_channel_t channel);
//               int calib_SetBackEndScale(rp_channel_t channel);
//               int calib_CalibrateBackEnd(rp_channel_t channel, rp_calib_params_t* out_params);
// #endif

// #if defined Z10 || defined Z20_125 || defined Z20
//           int32_t calib_getGenOffset(rp_channel_t channel);
//          uint32_t calib_getGenScale(rp_channel_t channel);
// #endif

// #if defined Z10 || defined Z20_125 || defined Z20_125_4CH
//               int calib_SetFilterCoff(rp_channel_t channel, rp_pinState_t gain, rp_eq_filter_cof_t coff , uint32_t value);
//          uint32_t calib_GetFilterCoff(rp_channel_t channel, rp_pinState_t gain, rp_eq_filter_cof_t coff);
// #endif

// #if defined Z10 || defined Z20_125 || defined Z20_125_4CH || defined Z20
//          uint32_t calib_GetFrontEndScale(rp_channel_t channel, rp_pinState_t gain);
//           int32_t calib_getOffset(rp_channel_t channel, rp_pinState_t gain);
// #endif

// #if defined Z20_250_12
//          uint32_t calib_GetFrontEndScale(rp_channel_t channel, rp_pinState_t gain, rp_acq_ac_dc_mode_t power_mode);
//           int32_t calib_getOffset(rp_channel_t channel, rp_pinState_t gain, rp_acq_ac_dc_mode_t power_mode);
//           int32_t calib_getGenOffset(rp_channel_t channel, rp_gen_gain_t gain);
//          uint32_t calib_getGenScale(rp_channel_t channel, rp_gen_gain_t gain);
// #endif

#endif //__CALIB_H
