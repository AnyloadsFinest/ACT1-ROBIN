#include "analog_dac.h"
#include "board.h"

void AnalogDac_Init(void)
{
    /* SPI/I2C to the analog module. Select 4-20 mA vs voltage range here. */
}

void AnalogDac_Write(uint16_t counts)
{
    (void)counts;
    /* Write 16-bit counts to the DAC / 4-20 mA IC. */
}

void AnalogDac_SetType(int32_t ana_type)
{
    (void)ana_type;
    /* Switch current vs voltage hardware range to match F7.1.1 SIGNAL TYPE. */
}
