# Hardware guide

Full bill of materials, wiring tables, and build instructions for the BatMetrics sensor unit. See the [main README](../README.md) for an overview of how the system works.

Building this requires basic soldering/crimping skills but is approachable for most makers. Total build cost is under €200 for a single unit, or under €150 per unit when building several.

## Table of contents

- [Bill of materials](#bill-of-materials)
- [Wiring tables](#wiring-tables)
- [Build instructions](#build-instructions)

## Bill of materials

| # | Qty | Manufacturer | Part | Description | Approx. cost/unit |
|---|---|---|---|---|---|
| 1 | 1 | Arduino | Nano ESP32 (with headers) | Microcontroller used for the counting system | 22€ |
| 2 | 2 | Adafruit | ADA2168 (5 mm) | IR beam-break sensors | 11€ |
| 3 | 2 | Generic (AZ-Delivery) | LM2596S | Step-down converters | 4€ |
| 4 | 1 | Radiomaster | 2S 21700 5000 mAh Li-ion | Battery | 25€ |
| 5 | 1 set | Generic (GTIWUNG) | Pre-soldered XT30 connector set with cables | Connectors and wires for power distribution | 8€ |
| 6 | 1 set | Generic (QWORK) | 22 AWG silicone wire set | Power cables for distributing power to components | 15€ |
| 7 | 1 set | Generic (ELEGOO) | Jumper cable set | Male-male, male-female, female-female jumper cables | 7€ |
| 8 | 1 | Generic (AZDelivery) | Mini breadboard, 400 pin | Breadboard | 4€ |
| 9 | 1 | Lexar | 32 GB UHS-I, U1, A1, V10, C10 micro SD card | Storage for config and occupancy data | 18€ |
| 10 | 1 | Generic (Create idea) | Micro SD card module, SPI, 3.3 V | Micro SD card module | 2€ |
| 11 | 1 | Generic (AZDelivery) | DHT11 temperature and humidity sensor | Temperature and humidity module | 5€ |
| 12 | 1 | Generic (WILDLIFE HOME) | Large wood bat box, built to NABU standards | Bat box | 30€ |
| 13 | 1 set | Generic | Wood screws | For attaching the 3D-printed housing to the bat box, and the IR emitter/sensors | <10€ |
| 14 | 1 set | Generic (Bolatus) | Heat-set inserts for 3D prints | For securing the housing's lid | 12€ |
| 15 | 1 set | Generic | Machine screw set (metric) | For securing the housing's lid | 11€ |

**Total cost per unit:** < €199
**Total cost per unit (building multiple systems):** < €145

## Wiring tables

### SD card module

| SD card module | Arduino Nano ESP32 |
|---|---|
| GND | Shared ground rail (or GND on the Arduino) |
| MISO | D12 (CIPO / GPIO047) |
| CLK | D13 (SCK / GPIO048) |
| MOSI | D11 (COPI / GPIO038) |
| CS | D4 (GPIO07) |
| 3V3 | 3V3 OUT |

### Temperature and humidity sensor

| DHT11 sensor | Arduino Nano ESP32 |
|---|---|
| I/O | D3 (GPIO06) |
| GND | Shared ground rail (or GND on the Arduino) |
| VCC | 3V3 OUT |

### IR beam-break sensors and emitter

| | IR sensor 1 | IR sensor 2 | IR emitter |
|---|---|---|---|
| 5V | 5V Out step-down converter (V OUT +) | 5V Out step-down converter (V OUT +) | 5V Out step-down converter (V OUT +) |
| GND | V OUT − (or shared ground rail) | V OUT − (or shared ground rail) | V OUT − (or shared ground rail) |
| DATA | Arduino D6 | Arduino D8 | — |

### Step-down converters

| 7V Out converter | Arduino Nano ESP32 |
|---|---|
| V OUT + | VIN |
| V OUT − | GND |

| 7V Out converter | Battery |
|---|---|
| V IN + | V OUT + |
| V IN − | V OUT − |

| 5V Out converter | 5V power rail on breadboard |
|---|---|
| V OUT + | + rail |
| V OUT − | − rail |

| 5V Out converter | Battery |
|---|---|
| V IN + | V OUT + |
| V IN − | V OUT − |

## Build instructions

Soldering and crimping are recommended for reliable connections; jumper cables and pin headers are usable for testing or to lower the barrier to entry (our prototype relies heavily on jumper cables to keep soldering to a minimum). Anyone building this system for actual deployment should replace jumper connections with soldered joints, crimps, or other more durable connections.

### Enclosure prep

1. 3D print the enclosure (we used PLA) — source files are in [`../enclosure/`](../enclosure/).
2. Drill one or two holes in the underside of the enclosure to pass the wires for the IR emitter and sensors through.
3. Install the four heat-set inserts into the corresponding holes in the print.
4. Glue the breadboard into the enclosure using its adhesive backing.
5. Glue the step-down converters to the inside of the enclosure. Future iterations should mount these more securely, e.g. with heat-set inserts and screws.

### Power wiring

![Assembled electronics inside the enclosure](../images/enclosure-internals.jpg)

6. Build a power splitter cable by crimping a pre-soldered male XT30 connector to two red and two black silicone wires, so each wire from the connector splits into two. Tin the free ends to ease insertion into the step-down converters' terminals.
7. Connect one red/black pair from the splitter to the input of each step-down converter, following the wiring tables above.
8. Set one converter to output 7 V and the other 5 V. If the converters have a display, switch it to show output voltage and adjust the potentiometer accordingly; otherwise connect a battery to the input and adjust the potentiometer while measuring output with a multimeter. Labelling the underside of each converter helps keep track of which is which.
9. Solder jumper connectors to one end of two red and two black wire lengths, and tin the other ends.
10. Connect the 7 V converter's output to the Arduino and the ground rail using these wires.
11. Connect the 5 V converter's output to one of the breadboard's power rails using the second set of wires — this rail supplies 5 V to the remaining components and serves as the shared ground.

### Electronics and sensors

12. Place the Arduino on the breadboard, leaving enough space for the micro SD card module and the temperature/humidity sensor.
13. Place the micro SD card module on the breadboard and wire it per the table above.
14. Format the micro SD card as FAT32 and insert it into the module. Cards larger than 32 GB caused issues for us and should be avoided.
15. Place the temperature and humidity sensor on the breadboard and wire it per the table above.
16. Strip the wire ends of the IR emitter and both IR sensors, and solder jumper connectors onto each — we recommend covering the joints with heat shrink to prevent shorts. In a production build these should be wired directly to the Arduino, with leads extended as needed for the target bat box.

![IR sensor and emitter mounted at the box entrance](../images/sensor-emitter-mounting.jpg)

17. Mount the IR emitter and both sensors at the entrance of the bat box, emitter on one side and both sensors on the other (e.g. sensors on the left, emitter on the right) — all three come with screw holes for this. Space the sensors approximately 3.5 cm apart vertically, measured at the sensor itself, to improve counting reliability.
18. Wire the IR emitter and both sensors to the Arduino and step-down converter per the table above.

### Firmware, assembly & testing

19. Clone this repository, install the required libraries (see the [main README](../README.md#requirements)), and compile/upload the firmware via the Arduino IDE.
20. Place the battery inside the enclosure and secure it (we used electrical tape).
21. Connect the battery's XT30 connector to the splitter's XT30 connector — the system should power up and begin counting.
22. Fit the lid and fasten the retaining screws.
23. Confirm proper operation by testing the Bluetooth connection with the app. Simulate bat crossings by hand at the entrance and verify the count updates correctly both in the app and on the micro SD card.
