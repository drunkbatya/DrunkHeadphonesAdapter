# DrunkHeadphonesAdapter

## About

Yet another headphone adapter for mobile phones without 3.5mm jack..

I fucking hate the assholes who thought removing the headphone jack was a good idea.

This device integrated into a phone case and allows you to charge phone while listening to music.

Charging is limited to 60-watt PD, USB PD Fast Role Swap is also supported, so theoretically, music playback shouldn't be interrupted when connecting or disconnecting the charger (currently untested).

The board has a physical switch that connects the phone USB data lines directly to the charger port.

It also has two internal DIP switches: one to disable the microphone (by shorting Ring2 and Sleeve) and another to disable headphone detection.

The adapter uses readily available components and is designed for JLCPCB SMT assembly.

## Features
- CS43131 DAC with 24bit 96khz with independent oscillators for 24.576MHz and 22.5792MHz
- ES8327 codec with 24bit 48khz ADC only used for microphone
- STM32G0B1VCT6 acts as both USB-I2S converter and UCPD controller
- 60W PD with Fast Role Swap to prevent pausing music when connecting or disconnecting the charger
- Dual-role charger USB-C port, capable of both sinking and sourcing power
- CTIA headset pinout only, but pin 3 and 4 can be shorted via DIP switch
- Headset button decoding and forwarding to the phone as HID events
- UAC2+HID API, both Android and IOS are supported
- A lot of ESD protection on all external pins

## Firmware
HAL and arcitecture core is based on ported [Flipper Zero OS](https://github.com/flipperdevices/flipperzero-firmware)

### Building
```bash
git submodule update --init --recursive
make
```
