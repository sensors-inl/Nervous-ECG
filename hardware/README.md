# Nervous - ECG Sensor

The **Nervous** initiative is a project embracing open-hardware and open-source
principles, with the goal of delivering wearable sensors and accompanying
software for the analysis of Autonomic Nervous System (SNA) activity.

The entire project is published under the MIT Licence.

## ECG Sensor

Contained within this repository are ECAD files for the production of PCBs and
the assembly of circuits for a wireless, wrist-worn ECG sensor.

Originally intended for measuring heart rate via electrodes positioned on the
forearms or hands, this sensor utilizes the AD8232 in conjunction with passive
components, as outlined in the "EXERCISE APPLICATION: HEART RATE MEASURED AT
THE HANDS" section on page 27 of the datasheet:
<https://www.analog.com/media/en/technical-documentation/data-sheets/ad8232.pdf>.
You have the freedom to adjust this configuration to suit different
applications and electrode placements.

The microcontroller employed is an ISP1807 (Insight SiP), which houses a
nRF52840 (Nordic Semiconductor).

## Hardware features

- USB-C connector for charging battery
- Embedded LiPo battery charger and I2C fuel-gauge
- On/Off SPST button (enables / disables voltage regulator)
- RGB status Led
- AD8232 ECG AFE featuring a 7 Hz - 24 Hz bandpass filter (56 dB @ 13 Hz) and
RLD
- BLE wireless communication

## Manufacturing and assembling

Gerber files, drill files, assembly drawings, pick-and-place files, and the
Bill of Materials (BOM) are accessible in the ```Export``` folder:

- ```Export/Manufacturing```: Gerber (.gbr) and Drill files (.drl)
- ```Export/Assembly```: Assembly drawing (.pdf), pick and place files (.pos),
BOM (.csv)

### PCB properties for manufacturing

|                          |                   |
| :----------------------- | :---------------: |
| Dimensions               | 21.5 mm x 33.5 mm |
| Number of layers         | 2                 |
| Minimal insulation width | 0.150 mm          |
| Minimal trace width      | 0.150 mm          |
| Miniaml hole size        | 0.200 mm          |

## Modification

The ```Source``` folder holds the original KiCad project, designed using KiCad
version 7.0.8.

## Note on cables and electrodes placement

This sensor is designed to capture ECG signals across various conditions while minimizing discomfort for the subject.
To achieve this, it incorporates high gain to accommodate electrode placement on the hands or forearms, where signal strength is typically lower.
However, this positioning also amplifies sensitivity to motion artifacts, which can interfere with accurate heart beat detection.
To mitigate the impact of these artifacts, we recommend utilizing solid gel electrodes like the 3M 2650.
For connectivity, we implemented a simple 2.54 mm, 3-way header on the sensor, allowing for custom electrode connections.
It's advised to use short cables for connecting the two electrodes in proximity to the device (RLD and RA), and a longer cable for the third electrode on the opposite arm (LA).
We constructed cables featuring female crimp terminals on the sensor end and 3 mm banana plugs with a snap adapter on the electrode end.
Finally, securely hold the electrodes in place, along with the cables near the electrodes, using medical tape to minimize noise from connector friction.

## Authors and contact

- Authors: Bertrand Massot, Matthieu Mesnage, Hugo Buy
- Affiliation: INSA Lyon, Ecole Centrale Lyon, Universite Claude Bernard Lyon
1, CPE Lyon, CNRS, INL UMR 5270, 69600 Villeurbanne France.
- Contact email: <bertrand.massot@insa-lyon.fr>

## Funding

This work was supported the French National Reasearch Agency (ANR) under the
grant ANR-22-CE31-0023-03 RENFORCE.
