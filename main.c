#include <msp430g2452.h>
#include <stdint.h>
#include <stdbool.h>

/* Schematics:
 *
 *     CL21A226MQQNNNE
 *      22 µF  6.3 V
 * GND ------||-------- VCC
 *         |    |
 *         --)|--
 *       5 F  2.7 V
 *        BCAP0005
 *
 * P2.6 ----------|☐|-------- P2.7
 *       32.768 kHz 12.5 pF
 *         AB26T-32.768KHZ
 *
 *            ___        ↑↑↑
 * P1.6 -----|___|-------|>|---- GND
 *           1 kΩ     0603 Green
 *
 *            ___        ↑↑↑
 * P2.3 -----|___|-------|>|---- GND
 *           200 Ω    0603 Red
 *                                      SRR1210-270M     P1.3-P1.5
 *                                       27µH  5 A           |
 *     -----------------------------------^^^^^^^------------------|>|----- VCC
 *    _|_                    |                               | MBR120ESFT1G
 *   / | \                   |                               |
 *  | _|_ | ←  solar cell   _|_  132 µF 6.3 V            | |--
 *  |  _  | ←   (ebay)      ___       6 x         P1.2 --| |<|  BSL802SN
 *   \_|_/                   |   CL21A226MQQNNNE         | |-|
 *     |                     |                               |
 *    GND                   GND                             GND
 */

#define MOSFET BIT2
#define SENSOR (BIT3|BIT4|BIT5)
#define LED_G BIT6
#define LED_R BIT3

#define XIN BIT6
#define XOUT BIT7

int main() {
  BCSCTL3 = XCAP_3;
  WDTCTL = WDT_ADLY_1000;

  P1DIR = MOSFET | LED_G;
  P1OUT = 0x00;
  P1REN = ~(MOSFET | LED_G | SENSOR);
  P1SEL = MOSFET | LED_G;
  P2DIR = LED_R;
  P2OUT = 0x00;
  P2SEL = XIN | XOUT;
  P2REN = (uint8_t)~(LED_R | XIN | XOUT);

  TACTL = TASSEL_1;
  TACCTL1 = 0;

  IE1 |= WDTIE;
  __enable_interrupt();

  LPM3;
}

// (2¯¹⁵ s)² * (1.5 V / 512)² / 54 µH = 0.15 nJ
// 2.5 V * 4.7 nC = 11.75 nJ
// assuming 50% efficiency, energy is proportional to x^2 - 16
// where x = ADC10MEM >> 1
// start charging at 94 mV (x = 32)
// always increase duty cycle at or above 374 mV (x = 128)
// log_energy[y] = 1024 * log₂ (((y+32)² - 16)/1008)
static const uint16_t log_energy[96] = {
     0,   92,  182,  269,  353,  435,  515,  592,  668,  741,  813,  883,  952,
  1019, 1084, 1148, 1211, 1272, 1332, 1391, 1449, 1506, 1561, 1616, 1669, 1722,
  1773, 1824, 1874, 1923, 1971, 2019, 2065, 2111, 2157, 2201, 2245, 2289, 2331,
  2373, 2415, 2456, 2496, 2536, 2575, 2614, 2652, 2690, 2727, 2764, 2800, 2836,
  2871, 2906, 2941, 2975, 3009, 3043, 3076, 3108, 3141, 3173, 3204, 3236, 3267,
  3297, 3328, 3358, 3388, 3417, 3446, 3475, 3504, 3532, 3560, 3588, 3615, 3643,
  3670, 3696, 3723, 3749, 3775, 3801, 3827, 3852, 3877, 3902, 3927, 3951, 3976,
  4000, 4024, 4048, 4071, 4095
};

#define STATES 42
#define FIRST_STATE 12

// intervals = takeWhile (/=1) $ iterate ((`div` 5) . (*4)) 32768

// interval - 1
static const uint16_t interval1[STATES] = {
  32767, 26213, 20970, 16775, 13419, 10735, 8587, 6869, 5495, 4395, 3515, 2811,
  2248, 1798, 1438, 1150, 919, 735, 587, 469, 375, 299, 239, 191, 152, 121, 96,
  76, 60, 47, 37, 29, 23, 18, 14, 11, 8, 6, 4, 3, 2, 1
};

// 1024 * log₂ (32768 / interval)
static const uint16_t log_count[STATES] = {
  0, 330, 659, 989, 1319, 1648, 1978, 2308, 2638, 2968, 3298, 3628, 3958, 4288,
  4617, 4947, 5278, 5608, 5940, 6270, 6600, 6934, 7263, 7593, 7928, 8263, 8602,
  8943, 9287, 9641, 9986, 10335, 10665, 11010, 11359, 11689, 12114, 12485,
  12982, 13312, 13737, 14336
};

static int8_t state = -1;
static int16_t last_energy;
static bool up;

#pragma vector=WDT_VECTOR
__interrupt void WDT_ISR() {
  ADC10CTL0 = REFON | REFBURST; // references take 30 µs @ 250 µA to settle
  P2OUT |= LED_R;
  P2OUT &= ~LED_R;
  __delay_cycles(10);

  // 4 ADC10CLKs @ 6.3 MHz is enough since input is directly connected to a large cap
  ADC10CTL0 = SREF_1 | ADC10SHT_0 | REFON | ADC10ON | ADC10IE;

  ADC10CTL1 = INCH_11; // at 1 MHz, this should take enough of time for the reference buffer to settle
  ADC10CTL0 |= ENC | ADC10SC;
  while (ADC10CTL1 & ADC10BUSY);
  uint16_t vcc = ADC10MEM;

  ADC10CTL0 &= ~ENC;
  ADC10CTL1 = INCH_3;
  ADC10CTL0 |= ENC | ADC10SC;
  while (ADC10CTL1 & ADC10BUSY);
  uint16_t sensor = ADC10MEM;

  ADC10CTL0 &= ~ENC;
  ADC10CTL0 = 0x0000;

  if (vcc > 852) { // > 2.5 V: turn on LED
    P2OUT |= LED_R;
    if (vcc > 869) { // > 2.55 V: disable MPPT
      TACTL = TASSEL_1;
      TACCTL1 = 0;
      state = -1;
      return;
    }
  }

  bool was_off = state == -1;

  if (was_off) { // no meaningful point of comparison
    if (sensor < 64) // save as much energy as we can
      return;
    state = FIRST_STATE;
    up = false;
    last_energy = 0;
  } else {
    if (sensor < 64) { // probably dark
      state -= 4;
      if (state < -1)
        state = -1;
      up = false;
      last_energy = 0;
    } else if (sensor > 255) { // very bright
      state += 4;
      if (state >= STATES)
        state = STATES - 1;
      up = true;
      last_energy = 0xFFFF;
    } else { // MPPT
      uint16_t energy = log_energy[(sensor >> 1) - 32] + log_count[state];
      if (up) {
        if (energy > last_energy) {
          if (state != STATES - 1)
            state++;
        } else {
          state--;
          up = false;
        }
      } else { // !up
        if (energy < last_energy) {
          state++;
          up = true;
        } else {
          state--;
        }
      }
      last_energy = energy;
    }
  }

  if (state == -1) {
    TACTL = TASSEL_1;
    TACCTL1 = 0;
  } else {
    TACCR0 = interval1[state];
    if (was_off) {
      TACCR1 = 1;
      TACCTL1 = OUTMOD_7;
      TACTL = TASSEL_1 | MC_1 | TACLR;
    }
  }
}
