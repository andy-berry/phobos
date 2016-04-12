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

#include "ch.h"
#include "hal.h"
#include "chprintf.h"

#include "usbcfg.h"
#include "encoder.h"

namespace {
    const systime_t loop_time = MS2ST(100); /* loop at 10 Hz */
    Encoder encoder(&GPTD3, /* CH1, CH2 connected to PC6, PC7 and enabled by board.h */
            {PAL_NOLINE, /* no index channel */
             152000, /* counts per revolution */
             EncoderConfig::filter_t::CAPTURE_128}); /* 128 * 84 MHz (TIM3 on APB1) = 1.52 us for valid edge */
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

static THD_WORKING_AREA(waSerialThread, 256);
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

    while (true) {
        if (SDU1.config->usbp->state == USB_ACTIVE) {
            chprintf((BaseSequentialStream*)&SDU1, "%d\r\n", encoder.count());
        }
        chThdSleep(loop_time);
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

    encoder.start();

    /*
     * Creates the LED blink and USB serial threads.
     */
    chThdCreateStatic(waLEDThread, sizeof(waLEDThread), NORMALPRIO-1,
            LEDThread, nullptr);
    chThdCreateStatic(waSerialThread, sizeof(waSerialThread), NORMALPRIO+1,
            SerialThread, nullptr);

    /*
     * Normal main() thread activity. In this demo it does nothing.
     */
    while (true) {
        chThdSleep(MS2ST(500));
    }
}
