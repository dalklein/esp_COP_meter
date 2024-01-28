This project is a COP (coefficient of performance) heat meter for
an air to water heat pump, using an ESP8266 node-mcu board with
OLED display, only about $7.

It measures water flow and temperatures across the refrigerant 
to water heat exchanger, collects electric power consumption
over MQTT from an Emporia Vue-2 monitor which has been ESPhome'd.
It then publishes the COP and data to MQTT, and an IOT stack
RPI collects and plots the data.

