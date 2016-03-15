/*
    ChibiOS - Copyright (C) 2006..2015 Giovanni Di Sirio

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include <array>
#include "ch.h"
#include "hal.h"
#include "chprintf.h"

#include "usbcfg.h"

/*
 * ADC configuration options (set in halconf.h)
 * ADC_USE_WAIT FALSE - ADC to be used asynchronously
 * ADC_USE_MUTUAL_EXCLUSION FALSE - ADC to be used in a single thread

 * ADC driver system settings (set in mcuconf.h)
 * STM32_ADC_USE_ADC1 TRUE
 */

/*
 * ADC conversion group.
 * Mode:        One shot, 1 sample of 3 channels, SW triggered.
 * Channels:    IN10, IN11, IN12.
 */

namespace {
    const eventmask_t evt_adc_complete = EVENT_MASK(0);
    const eventmask_t evt_adc_error = EVENT_MASK(1);
    event_source_t adc_event_source;

    void adcerrorcallback(ADCDriver* adcp, adcerror_t err) {
        (void)adcp;
        (void)err;
        chSysLockFromISR();
        chEvtBroadcastFlagsI(&adc_event_source, evt_adc_error);
        chSysUnlockFromISR();
    }
    void adccallback(ADCDriver* adcp, adcsample_t* buffer, size_t n) {
        (void)adcp;
        (void)buffer;
        (void)n;
        chSysLockFromISR();
        chEvtBroadcastFlagsI(&adc_event_source, evt_adc_complete);
        chSysUnlockFromISR();
    }

    std::array<adcsample_t, 3> adc_buffer;
    static const ADCConversionGroup adcgrpcfg1 = {
        false,
        adc_buffer.size(),
        adccallback,
        adcerrorcallback,
        0,                        /* CR1 */
        ADC_CR2_SWSTART,          /* CR2 */
        ADC_SMPR1_SMP_AN12(ADC_SAMPLE_3) |
        ADC_SMPR1_SMP_AN11(ADC_SAMPLE_3) |
        ADC_SMPR1_SMP_AN10(ADC_SAMPLE_3),
        0,                        /* SMPR2 */
        ADC_SQR1_NUM_CH(adc_buffer.size()),
        0,                        /* SQR2 */
        ADC_SQR3_SQ3_N(ADC_CHANNEL_IN12) |
        ADC_SQR3_SQ2_N(ADC_CHANNEL_IN11) |
        ADC_SQR3_SQ1_N(ADC_CHANNEL_IN10)
    };

    const systime_t loop_time = MS2ST(1); // loop at 1 kHz
} // namespace

/*
 * This is a periodic thread that simply flashes an LED and allows visual
 * inspection to see that the code has not halted.
 */
static THD_WORKING_AREA(waLEDThread, 128);
static THD_FUNCTION(LEDThread, arg) {
    (void)arg;
    chRegSetThreadName("led");
    while (true) {
        palToggleLine(LINE_LED);
        if (SDU1.config->usbp->state == USB_ACTIVE) {
            chThdSleepMilliseconds(100);
        } else {
            chThdSleepMilliseconds(1000);
        }
    }
}

static THD_WORKING_AREA(waSerialThread, 128);
static THD_FUNCTION(SerialThread, arg) {
    (void)arg;
    chRegSetThreadName("serial");

    /*
     * Initializes a serial-over-USB CDC driver.
     */
    sduObjectInit(&SDU1);
    sduStart(&SDU1, &serusbcfg);

    /*
     * Activates the USB driver and then the USB bus pull-up on D+.
     * Note, a delay is inserted in order to not have to disconnect the cable
     * after a reset.
     */
    board_usb_lld_disconnect_bus();   //usbDisconnectBus(serusbcfg.usbp);
    chThdSleepMilliseconds(1500);
    usbStart(serusbcfg.usbp, &usbcfg);
    board_usb_lld_connect_bus();      //usbConnectBus(serusbcfg.usbp);

    event_listener_t adc_listener;
    chEvtRegisterMask(&adc_event_source, &adc_listener,
            evt_adc_error | evt_adc_complete);

    while (true) {
        eventmask_t evt = chEvtWaitAny(ALL_EVENTS);

        if (SDU1.config->usbp->state == USB_ACTIVE || SDU1.state == SDU_READY) {
            if (evt & evt_adc_error) {
                chprintf((BaseSequentialStream*)&SDU1,
                        "ERROR in ADC conversion.\r\n");
            }
            if (evt & evt_adc_complete) {
                chprintf((BaseSequentialStream*)&SDU1,
                        "%d\t%d\t%d\r\n",
                        adc_buffer[0],
                        adc_buffer[1],
                        adc_buffer[2]);
            }
        }
    }
}

/*
 * Application entry point.
 */
int main(void) {

    /*
     * System initializations.
     * - HAL initialization, this also initializes the configured device drivers
     *   and performs the board-specific initializations.
     * - Kernel initialization, the main() function becomes a thread and the
     *   RTOS is active.
     */
    halInit();
    chSysInit();

    /*
     * ADC init
     */
    adcStart(&ADCD1, nullptr);

    /*
     * Event Source init
     */
    chEvtObjectInit(&adc_event_source);

    /*
     * Creates the LED blink and USB serial threads.
     */
    chThdCreateStatic(waLEDThread, sizeof(waLEDThread), NORMALPRIO-1,
            LEDThread, nullptr);
    chThdCreateStatic(waSerialThread, sizeof(waSerialThread), NORMALPRIO+1,
            SerialThread, nullptr);

    /*
     * Normal main() thread activity. In this demo it sends ADC values over serial.
     */
    rtcnt_t start = 0;
    rtcnt_t dt = 0;
    systime_t deadline = chVTGetSystemTimeX();
    while (true) {
        // Use RTC directly due to a bug in last time computation in TM module.
        start = chSysGetRealtimeCounterX();
        deadline += loop_time;
        adcStartConversion(&ADCD1, &adcgrpcfg1, adc_buffer.data(), 1);
        dt = chSysGetRealtimeCounterX() - start;

        /*
         * Sleep thread until next deadline if it hasn't already been missed.
         */
        bool miss = false;
        chSysLock();
        systime_t sleep_time = deadline - chVTGetSystemTimeX();
        if ((sleep_time > (systime_t)0) and (sleep_time < loop_time)) {
            chThdSleepS(sleep_time);
        } else {
            miss = true;
        }
        chSysUnlock();

        /*
         * If deadline missed, wait until button is pressed before continuing.
         */
        if (miss) {
            chprintf((BaseSequentialStream*)&SDU1,
                    "loop time was: %d us\r\n", RTC2US(STM32_SYSCLK, dt));
            chprintf((BaseSequentialStream*)&SDU1, "Press button to continue.\r\n");
            while (palReadLine(LINE_BUTTON)) { /* Button is active LOW. */
                chThdSleepMilliseconds(10);
            }
            deadline = chVTGetSystemTimeX();
        }
    }
}
