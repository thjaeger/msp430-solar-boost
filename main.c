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
 *            ___        ↑↑
 * P1.6 -----|___|-------|>|---- GND
 *           1 kΩ     0603 Green
 *
 *            ___        ↑↑
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

// 47 mV -- 374 mV
#define SENSOR_MIN 32
#define SENSOR_MAX 256

// (2¯¹⁵ s)² * (1.5 V / 512)² / 54 µH = 0.15 nJ
// 2.5 V * 4.7 nC = 11.75 nJ
// assuming 50% efficiency, energy is proportional to x^2 - 16
// where x = ADC10MEM / 2
// log_energy[y] = 1024 * log₂ (x² - 16)
// putStrLn $ concat $ Data.List.intersperse ", " $ ["INT16_MIN" | _ <- [0..4]] ++ [show $ round $ 1024 * log (x*x - 16) / log 2 | x <- [5..127]]
static const int16_t log_energy[SENSOR_MAX/2] = {
  INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN,
   3246,  4426,  5165,  5719,  6167,  6546,  6875,  7168,  7432,  7672,  7892,
   8097,  8287,  8465,  8633,  8791,  8941,  9083,  9219,  9348,  9472,  9591,
   9705,  9815,  9921, 10023, 10121, 10217, 10309, 10399, 10485, 10570, 10652,
  10731, 10809, 10884, 10958, 11030, 11100, 11169, 11236, 11301, 11365, 11428,
  11489, 11549, 11608, 11666, 11722, 11778, 11832, 11886, 11938, 11990, 12041,
  12091, 12140, 12188, 12236, 12282, 12328, 12373, 12418, 12462, 12505, 12548,
  12590, 12631, 12672, 12713, 12752, 12792, 12830, 12869, 12906, 12944, 12980,
  13017, 13053, 13088, 13123, 13158, 13192, 13226, 13259, 13292, 13325, 13357,
  13389, 13421, 13452, 13483, 13514, 13544, 13575, 13604, 13634, 13663, 13692,
  13720, 13749, 13777, 13804, 13832, 13859, 13886, 13913, 13940, 13966, 13992,
  14018, 14043, 14069, 14094, 14119, 14144, 14168, 14193, 14217, 14241, 14264,
  14288, 14311
};

#define STATES 42
#define FIRST_STATE 12
#define FAST_STEPS 4

// intervals = takeWhile (/=1) $ iterate ((`div` 5) . (*4)) 32768

// interval - 1
static const uint16_t interval1[STATES] = {
  32767, 26213, 20970, 16775, 13419, 10735, 8587, 6869, 5495, 4395, 3515, 2811,
  2248, 1798, 1438, 1150, 919, 735, 587, 469, 375, 299, 239, 191, 152, 121, 96,
  76, 60, 47, 37, 29, 23, 18, 14, 11, 8, 6, 4, 3, 2, 1
};

// 1024 * log₂ (32768 / interval)
static const int16_t log_count[STATES] = {
  0, 330, 659, 989, 1319, 1648, 1978, 2308, 2638, 2968, 3298, 3628, 3958, 4288,
  4617, 4947, 5278, 5608, 5940, 6270, 6600, 6934, 7263, 7593, 7928, 8263, 8602,
  8943, 9287, 9641, 9986, 10335, 10665, 11010, 11359, 11689, 12114, 12485,
  12982, 13312, 13737, 14336
};

static int16_t state = -1;
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
    if (sensor < SENSOR_MIN) // save as much energy as we can
      return;
    state = FIRST_STATE;
    up = false;
    last_energy = 0;
  } else {
    if (sensor < SENSOR_MIN) { // probably dark
      state -= FAST_STEPS;
      if (state < -1)
        state = -1;
      up = false;
      last_energy = 0;
    } else if (sensor >= SENSOR_MAX) { // very bright
      state += FAST_STEPS;
      if (state >= STATES)
        state = STATES - 1;
      up = true;
      last_energy = INT16_MAX;
    } else { // MPPT
      int16_t energy = log_energy[sensor/2] + log_count[state];
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
