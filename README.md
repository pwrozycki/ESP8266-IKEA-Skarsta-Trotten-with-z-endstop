<h1>
  <p align="center">
     ESP8266 IKEA Skarsta/Trotten Web Dashboard Z-Endstop mod
  </p>
</h1> 

<p>This fork was created to address issues I had with original project. Following instructions precisely I ended up having problems with precision of ultrasound sensor.
It turned out I could not use it on a daily basis due to lack of reliability. It is possible that this imprecision was caused by faulty hardware (cheap sensor shipped from AliExpress or power supply quality issue).
<p>Nevertheless I decided to install a physical z-endstop switch (D5 pin) and rotational sensor (D6 pin) and modify the code appropriately. The result is this fork.</p>
<p>Apart from hardware modifications there should be no observable difference in usage of table with the mod. All fine features of original project are preserved. The only consequence of adding of a z-endstop is a need to calibrate the table.
This is achieved by lowering the table when power supply is connected (on program startup). Afterwards current position is tracked by reading state of opto-sensor attached to motor shaft. </p>

## Credits

- [Original project](https://github.com/pwrozycki/ESP8266-IKEA-Skarsta-Trotten-with-z-endstop) by flosommerfeld
- [3D design files](https://www.instructables.com/Motorizing-an-IKEA-SKARSTA-Table/) by user pashiran
- [Arduino library for HCSR04 ultrasonic sensor](https://github.com/gamegine/HCSR04-ultrasonic-sensor-lib) by gamegine
- [Arduino library for Cytron Motor Drivers](https://github.com/CytronTechnologies/CytronMotorDriver) by Cytron
- [Arduino ESP8266 Web Server library](https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer) by Arduino
- [Bootstrap HTML, CSS, JS Framework](https://getbootstrap.com/) by Bootstrap

## License

The contents of this repository are covered under the [GPLv3 license](LICENSE).
