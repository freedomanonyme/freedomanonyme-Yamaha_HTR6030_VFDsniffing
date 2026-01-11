# freedomanonyme-Yamaha_HTR6030_VFDsniffing

This is a project in development to add my **Yamaha HTR-6030 (ex RX-V361)** into **Home Assistant** using **ESPHome**.  
Here I share the approach I took to achieve full control along with reliable, real-time state feedback.

## How the setup works

- **Discrete IR Control**  
  A hidden IR transmitter is installed inside the front panel to preserve the receiver’s original aesthetic.

- **Power Feedback**  
  An ADC pin on the ESP32 monitors the receiver’s power rail directly, ensuring the on/off state is always accurate in Home Assistant.

- **Real-time Data (VFD sniffing)**  
  For everything else — **Volume, Mute, and Source selection** — the internal VFD (display) signals are sniffed and decoded.

## Result

Even though the project is still a work in progress, the results are already very solid.  
This effectively turns a **2007-era, non-networked AV receiver** into a fully smart device with **real-time feedback**.

If anyone is interested to contribute, you can check the dev branch.
