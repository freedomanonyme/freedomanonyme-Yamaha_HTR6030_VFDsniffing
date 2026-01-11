## Disclaimer

This project is the result of an ongoing experimental effort.  
At its current stage, the results are already very satisfying and fully usable. However, as a perfectionist, I believe further improvements are still possible.

### Identified issue

Most of the data emitted by the VFD is correctly decoded and readable.  
That said, there is an occasional **bit-shifting issue**, which can result in sporadic decoding errors such as:

- loss of the negative sign (`-`) in the volume value,
- missing or corrupted digits in the volume level,
- partially incorrect ASCII characters.

These errors are infrequent, but they do occur.

### Current situation

Most of these issues can currently be **mitigated at the software level** using filtering or validation rules (for example: detecting invalid formats, applying contextual corrections, or discarding inconsistent frames).

This approach already provides a very reliable behavior within Home Assistant.

### Goal

However, in order to further extend the project’s capabilities, my goal is to achieve a **fully accurate, low-level capture of the VFD bus**, meaning:

- capturing **100% of the transmitted bytes**,
- with no loss,
- no bit shifting,
- and without relying on post-processing corrections.

The main challenge is therefore to ensure perfect synchronization with the actual VFD protocol, eliminating any ambiguity at the lowest decoding level.

## Files

You need to put the yaml file and the components folder into the esphome folder of your Home Assistant setup.

## Hardware Setup & Pinout

I used an **ESP32 DevKit V1**. To protect the circuits, I added resistors to the data lines.  
Here is the pin mapping I used:

| Receiver Pin | ESP32 GPIO | Component        | Function        |
|--------------|------------|------------------|-----------------|
| CKFD         | GPIO 18    | 1kΩ resistor     | SPI Clock       |
| DTFD         | GPIO 23    | 1kΩ resistor     | SPI Data        |
| CEFD         | GPIO 27    | 1kΩ resistor     | Chip Enable     |
| +3.3s        | GPIO 34    | 10kΩ resistor    | Power Sense     |
| MG           | GND        | Direct           | Signal Ground   |

### How it works
The **+3.3s** pin monitors power as a binary sensor.  
The other pins act as a *sniffer* to decode the front panel display data.

It’s not 100% perfect yet, but I can reliably track:
- Volume level
- Selected input
- Mute status  

all in real time.

---

## Stealth IR Control

I also tucked a **940 nm infrared emitter** behind the front panel, right next to the original receiver LED.

- **GPIO 4 → 220Ω resistor → IR LED anode**
- **IR LED cathode → ESP32 GND**

This allows me to send IR commands from Home Assistant while keeping the stock look.

---

## Power & Mounting

I hijacked a **4-core cable** from the back of the receiver and used an **LDO converter** to step the 12V voltage down to **5 V**.

Since the chassis is metal, I designed and 3D-printed a **non-conductive mount** (using a *K1 Max*) to avoid any risk of shorts.

---
### Power and signal connections

The followings pictures can help you to find where to start on how to power your ESP and how to connect the pins.

<img src="https://github.com/user-attachments/assets/4cf26b0d-0c7e-4d6e-a2ca-24caa3331685" width="450">

*Power wiring reference*

<img src="https://github.com/user-attachments/assets/326db9fd-de63-4666-b654-d43cbf79dd08" width="450">

*Front panel signal taps*

<img src="https://github.com/user-attachments/assets/61409fa5-7964-4874-a7ae-c90aa9fdef20" width="450">

*ESP mounting and insulation*

---

These pictures show what you should get into Home assistant 

<table>
  <tr>
    <td align="center">
      <img width="250" alt="Power wiring" src="https://github.com/user-attachments/assets/8742a9c4-e331-417c-8c5c-688d03238bb1" />
    </td>
    <td align="center">
      <img width="250" alt="Signal taps" src="https://github.com/user-attachments/assets/49f448fa-3e3d-44a3-8362-8d6908b16769" />
    </td>
    <td align="center">
      <img width="250" alt="ESP mounting" src="https://github.com/user-attachments/assets/f2443519-51ff-4527-a147-7ddcc3ef21c7" />
    </td>
  </tr>
</table>





