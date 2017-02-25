# ESP8266-rcswitch

Subscribes to an MQTT topic/patch and reacts on switch commands for rc power sockets. It also publishes humidity and temperature regulary

![Schematic](https://raw.githubusercontent.com/b4ckspace/esp8266-rcswitch/master/wiring.jpg "How to connect")

## config.h

You have to copy the config.h.example to config.h and replace the strings with your credentials

## dependencies

* PubSubClient
* ArrayQueue
* Arduino/ESP8266
