#include "adc_sample.h"
#include "arch_time.h"
#include "mic.h"
#include "mmio.h"

#define CLOCK_ENABLE_0 0x03002000u
#define CLOCK_SARADC (1u << 10)

#define SARADC_CONTROL 0x030F0004u
#define SARADC_START (1u << 0)
#define SARADC_SELECT_GP26 (1u << 5) /* channel 1 */
#define SARADC_CYCLE 0x030F000Cu
#define SARADC_CLOCK_DIVIDE_16 (0xFu << 12)
#define SARADC_RESULT_GP26 0x030F0014u
#define SARADC_RESULT_MASK 0x0FFFu
#define SARADC_INTERRUPT_CLEAR 0x030F0024u
#define SARADC_INTERRUPT_RAW 0x030F002Cu
#define SARADC_DONE (1u << 0)

#define TIMER_HZ 25000000u
#define SAMPLE_PERIOD (TIMER_HZ / MIC_SAMPLE_RATE)

_Static_assert(TIMER_HZ % MIC_SAMPLE_RATE == 0, "timer must split the sample rate evenly");

static AdcBias bias;
static unsigned long next_sample_time;

static uint16_t readAdc(void)
{
    /* The done flag, not the busy bit: busy may not be up yet right after start */
    mmio_write_32(SARADC_INTERRUPT_CLEAR, SARADC_DONE);
    mmio_write_32(SARADC_CONTROL, SARADC_SELECT_GP26 | SARADC_START);
    while ((mmio_read_32(SARADC_INTERRUPT_RAW) & SARADC_DONE) == 0) {
    }
    return (uint16_t)(mmio_read_32(SARADC_RESULT_GP26) & SARADC_RESULT_MASK);
}

bool InitMic(void)
{
    /* Runs before Linux boots; uboot.patch keeps Linux from gating it as unused afterwards */
    mmio_write_32(CLOCK_ENABLE_0, mmio_read_32(CLOCK_ENABLE_0) | CLOCK_SARADC);
    mmio_write_32(SARADC_CYCLE, mmio_read_32(SARADC_CYCLE) | SARADC_CLOCK_DIVIDE_16);
    StartAdcBias(&bias, readAdc());
    next_sample_time = GetSysTime();
    return true;
}

/* ponytail: spins between samples, so tasks below this one never run. Pace with a timer interrupt if they must */
int16_t ReadMicSample(void)
{
    next_sample_time += SAMPLE_PERIOD;
    while ((long)(GetSysTime() - next_sample_time) < 0) {
    }
    return ConvertAdcSample(&bias, readAdc());
}
