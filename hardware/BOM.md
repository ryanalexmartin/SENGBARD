# SENGBARD Hardware Bill of Materials

## MCU & Core
| Qty | Part | Value/Description | Package | Notes |
|-----|------|-------------------|---------|-------|
| 1 | STM32F103C8T6 | ARM Cortex-M0 MCU | LQFP-48 | Or Blue Pill module for prototyping |
| 1 | Crystal | 8MHz HC49 | Through-hole | |
| 2 | Capacitor | 20pF ceramic | 0805/TH | Crystal load caps |
| 1 | Capacitor | 4.7uF ceramic | 0805/TH | VBAT |
| 4 | Capacitor | 100nF ceramic | 0805/TH | VDD decoupling |
| 1 | Resistor | 10K | 0805/TH | NRST pull-up |
| 1 | Header | 4-pin male | 2.54mm | SWD programming |
| 1 | Button | Tactile | 6x6mm | Reset (optional) |

## Power Supply
| Qty | Part | Value/Description | Package | Notes |
|-----|------|-------------------|---------|-------|
| 1 | Connector | 2x5 shrouded | 2.54mm | Eurorack power |
| 2 | MP1584 module | Buck converter | Module | Set to 3.3V and 5V |
| 1 | Diode | 1N4001 | DO-41 | Reverse polarity |
| 3 | Capacitor | 100uF electrolytic | TH | Input + outputs |
| 1 | Inductor | 10uH | 0805/TH | Optional LC filter |

## DAC & CV Outputs
| Qty | Part | Value/Description | Package | Notes |
|-----|------|-------------------|---------|-------|
| 2 | MCP4822 | Dual 12-bit DAC | DIP-8/SOIC-8 | |
| 2 | TL072 | Dual op-amp | DIP-8 | Output buffers |
| 4 | Capacitor | 100nF ceramic | 0805/TH | DAC decoupling |
| 2 | Capacitor | 10uF electrolytic | TH | Op-amp supply |
| 3 | Resistor | 100R | 0805/TH | DAC output series |
| 3 | Resistor | 1K | 0805/TH | Output protection |
| 3 | Jack | 3.5mm mono | Thonkiconn | CV outputs |

## Gate Outputs
| Qty | Part | Value/Description | Package | Notes |
|-----|------|-------------------|---------|-------|
| 1 | 74HC244 | Octal buffer | DIP-20 | Optional for 5V gates |
| 5 | Resistor | 470R | 0805/TH | Series protection |
| 1 | Capacitor | 100nF ceramic | 0805/TH | Buffer decoupling |
| 5 | Jack | 3.5mm mono | Thonkiconn | Gate outputs |

## CV Inputs
| Qty | Part | Value/Description | Package | Notes |
|-----|------|-------------------|---------|-------|
| 3 | Jack | 3.5mm mono switched | Thonkiconn | Clock, Reset, Scene |
| 3 | Resistor | 100K | 0805/TH | Series protection |
| 2 | Resistor | 47K | 0805/TH | Clock/Reset divider |
| 1 | Resistor | 68K | 0805/TH | Scene CV divider |
| 1 | Resistor | 33K | 0805/TH | Scene CV divider |
| 3 | Capacitor | 100nF ceramic | 0805/TH | Input filtering |
| 2 | BAT54S | Dual Schottky diode | SOT-23 | ESD protection |

## Encoders
| Qty | Part | Value/Description | Package | Notes |
|-----|------|-------------------|---------|-------|
| 8 | Encoder | EC11 rotary, 24 detent | Through-hole | |
| 8 | Knob | D-shaft, 6mm | - | |
| 1 | MCP23017 | I2C GPIO expander | DIP-28 | |
| 2 | Resistor | 4.7K | 0805/TH | I2C pull-ups |
| 1 | Capacitor | 100nF ceramic | 0805/TH | Decoupling |

## User Interface
| Qty | Part | Value/Description | Package | Notes |
|-----|------|-------------------|---------|-------|
| 16 | Button | Tactile switch | 6x6mm | |
| 36 | LED | 3mm diffused | Through-hole | Various colors |
| 36 | Resistor | 330R | 0805/TH | LED current limit |
| 5 | 74HC595 | Shift register | DIP-16 | LED drivers |
| 5 | Capacitor | 100nF ceramic | 0805/TH | Decoupling |
| 1 | OLED | SSD1306 128x64 | I2C module | 0.96" display |

## Jacks Summary
| Qty | Part | Notes |
|-----|------|-------|
| 3 | Thonkiconn PJ398SM (switched) | Clock In, Reset In, Scene CV In |
| 8 | Thonkiconn PJ398SM (mono) | 3x CV Out, 5x Gate Out |

## Resistors Summary
| Value | Qty | Usage |
|-------|-----|-------|
| 100R | 3 | DAC output |
| 330R | 36 | LED current limit |
| 470R | 5 | Gate output |
| 1K | 3 | CV output protection |
| 4.7K | 2 | I2C pull-ups |
| 10K | 1 | MCU reset pull-up |
| 33K | 1 | Scene CV divider |
| 47K | 2 | Clock/Reset divider |
| 68K | 1 | Scene CV divider |
| 100K | 3 | Input protection |

## Capacitors Summary
| Value | Qty | Type | Usage |
|-------|-----|------|-------|
| 20pF | 2 | Ceramic | Crystal load |
| 100nF | 20 | Ceramic | Decoupling throughout |
| 4.7uF | 1 | Ceramic | MCU VBAT |
| 10uF | 2 | Electrolytic | Op-amp supply |
| 100uF | 3 | Electrolytic | Buck converter I/O |

## ICs Summary
| Part | Qty | Package | Notes |
|------|-----|---------|-------|
| STM32F103C8T6 | 1 | LQFP-48 | Main MCU |
| MCP4822 | 2 | DIP-8 | DAC |
| MCP23017 | 1 | DIP-28 | GPIO expander |
| TL072 | 2 | DIP-8 | Op-amp |
| 74HC595 | 5 | DIP-16 | Shift register |
| 74HC244 | 1 | DIP-20 | Gate buffer (optional) |
| MP1584 module | 2 | Module | Buck converters (3.3V, 5V) |

---

## Supplier Notes

**Thonkiconn jacks**: Available from Thonk (UK), Synthcube (US), Modular Addict (US)

**STM32F103C8T6**: LCSC, Mouser, DigiKey. "Blue Pill" module from AliExpress/Amazon for prototyping

**Encoders**: EC11 widely available on Amazon, AliExpress, Tayda Electronics

**OLED module**: SSD1306 I2C modules widely available on Amazon, AliExpress

**MP1584 modules**: Amazon, AliExpress, eBay (~$1-2 each). Pre-set output with trimpot before use!

**General components**: Tayda Electronics (cheap), Mouser/DigiKey (reliable), LCSC (very cheap, from China)

---

## Estimated Cost (approx USD)
- MCU (Blue Pill): $3-5
- DACs (2x MCP4822): $6-8
- Port expander (MCP23017): $2-3
- Op-amps (2x TL072): $1-2
- Shift registers (5x 74HC595): $2-3
- Buck converters (2x MP1584): $2-4
- Encoders (8x EC11): $8-12
- Jacks (11x Thonkiconn): $8-11
- OLED display: $3-5
- Buttons (16x): $2-3
- LEDs (36x): $2-3
- Resistors/Capacitors: $5-8
- PCB (JLCPCB/PCBWay): $5-15

**Total estimate: $50-80 USD** (excluding panel)
