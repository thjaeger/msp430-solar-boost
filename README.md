The project is built around a single solar cell that I scored on ebay.  (I paid
$1 including shipping for 10 of those!).  In bright sunlight they put out 500
mA at 0.5 V, but indoors you're lucky to get a few hundred µA at 0.2 V.

I use an msp430 to drive an Infineon BSL802SN mosfet. This thing is absolutely
amazing: it'll turn on at moderate loads at 1.2 V; 4.5 nC @ 2.5 V buys you an
Rᴅꜱ₍ᴏɴ₎ of 17 mΩ and it comes in a tiny package that is nonetheless dead easy
to use on stripboard -- just be sure to wear an antistatic wrist-band when
handling it.  I didn't bother with a gate resistor -- the diode ensures that
the DS voltage never goes above 3 V.

The boost converter operates in discontinuous mode: the mosfet gets turned on
for 32 µs (timed using an external crystal -- I can't afford the 55 µA that
the DCO draws), which charges the 27 µF inductor (the part I chose was probably
an overkill).  At 0.2 V input voltage, for example, this will result in a
240 mA current in the inductor.  When the mosfet turns off, this gets
discharged through a diode into the supercap.  It's surprisingly difficult
to find a Schottky diode with a low leakage current, and once a again the
part I chose was probably overkill.

The energy gets stored on a 5 F Maxwell supercap.  This value was probably too
high:  Leakage current (after waiting a few days to account for dielectric
absorption) is still around 3 µA, and it's only going to get worse as the
capacitor ages.

The msp430 wakes up once a second and measures Vᴄᴄ and the voltage of the solar
cell.  If Vᴄᴄ is above 2.5 V, the red LED gets turned on, otherwise it just
flashes briefly.  Above 2.55 V charging stops to prevent damaging the supercap.
A simple MPPT algorithm selects the optimal duty cycle for the boost converter.
The green LED gets turned on at the same time as the mosfet for debugging
purposes (with the 10 kΩ resistor it only draws 50 µA @ 2.5 V while still being
pretty bright).

BOM:
----

| Item                     | Price         |
| ------------------------ | ------------- |
| MSP430G2332 will do      | $1.51 (@ 10)  |
| Solar Cell (ebay)        | $0.10 (@ 10)  |
| BCAP0005                 | $1.68 (@ 10)  |
| CL21A226MQQNNNE (7x)     | $0.09 (@ 100) |
| AB26T-32.768KHZ          | $0.18 (@ 50)  |
| SRR1210-270M             | $0.93 (@ 10)  |
| BSL802SN                 | $0.54 (@ 100) |
| MBR120ESFT1G             | $0.35 (@ 10)  |
| Socket                   | $0.17 (@ 50)  |
| Various Resistors & Leds | $0.10         |
| Veroboard (1.5" x 2.1")  | $0.20         |
| Total                    | $6.39         |
