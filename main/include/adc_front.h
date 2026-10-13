#ifndef ADC_FRONT_H
#define ADC_FRONT_H

#include <stdint.h>

void     AdcFront_Init(void);
void     AdcFront_Tick(void);
int32_t  AdcFront_ReadCounts(void);
/** Average of @p sample_count fresh DRDY reads (monitor-cal style). Returns 0 on success. */
int       AdcFront_AverageCounts(int sample_count, int32_t *avg_out);
int32_t  AdcFront_CountsToMillivolts(int32_t counts);
uint8_t  AdcFront_IsReady(void);

#endif
