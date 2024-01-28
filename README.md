This project is a Coefficient Of Performance heat meter for
an air-to-water heat pump, using an ESP8266 node-mcu board with
OLED display, only about $7.

It measures water flow from a hall-effect turbine meter, 
temperatures across the refrigerant to water heat exchanger
with DS18b20 sensors, collects electric power consumption
over MQTT from an Emporia Vue-2 monitor which has been ESPhome'd.
It calculates and publishes the COP and data to MQTT and the
cute little OLED screen.  An IOTstack RPI collects and plots the data.


