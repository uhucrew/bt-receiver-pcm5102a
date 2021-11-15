# bluetooth adapter with Burr-Brown PCM5102A D/A converter and SSD1306 display

Well working bluetooth adapter with a nice display and volume control buttons. The quality is mean because of the SBC bluetooth codec. But all devices supports it. Better algorithms are not royalty free and it's a lot of work to implement aptX, AAC or similar. So I'm satisfied with this solution.

Special feature of this implementation is the exponential nearly lossless volume control. The 16 bit samples received from bluetooth layer are multiplied with a 16 bit volume value. In turn we get a 32 bit scaled value which is sent without loss to the 32 bit DA converter. With this approach no information is lost especially with low volumes. The 16 bit volume factor is calculated with x^3 function. This function meets the human sense for loudness best in my opinion.

A project description in more detail in English as well as German language can be found on my blog:
https://bastelblog.runlevel3.de/en/weekend-project/bluetooth-audio-receiver-with-pcm5102a/

## hardware

ESP32-WROOM-32 connected to SSD1306 128x64 display and PCM5102A breakout board, schematics as follows:

![schematics](bt_receiver_pcm5102a_schematics.png?raw=true "schematics")


## quick start

### prerequisites

The most simple way is to install docker and use the docker image with the Espressif development toolkit.

### build and run

Connect the ESP32-WROOM-32 usb port with yout computer. Check the ttyUSB number, needed by the flash command. ```dmesg``` command may be useful. Clone software, build and flash it.

```
git clone https://github.com/uhucrew/bt-receiver-pcm5102a.git
cd bt-receiver-pcm5102a
docker run --rm -v $PWD:/project -w /project espressif/idf idf.py build
docker run -ti --rm -v $PWD:/project --privileged --device=/dev/ttyUSB0 -w /project espressif/idf idf.py -p /dev/ttyUSB0 flash monitor
```


## usage

- Press plus or minus buttons to adjust volume.
- Press and hold to rapidly change volume.
- Press both buttons to mute and both buttons again to restore volume.
- Press and hold both buttons to reset and pair with a new device.


## background

This software is based on the "a2dp_sink" example from the Espressif development toolkit found in the path: esp-idf/examples/bluetooth/bluedroid/classic_bt/a2dp_sink
Added persistent device names based on bluetooth address, volume control and a display with vu-meter to the example. Because of the interrupt-driven I2C I had to add functions for the display driver to transfer changes only to the display. Maybe a display with SPI connection is better suited.
For the OLED display and push buttons I modified code from this two projects:

https://github.com/PIC-Nico/SSD1306

https://github.com/craftmetrics/esp32-button


