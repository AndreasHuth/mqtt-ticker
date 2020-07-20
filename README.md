# MqttTicker
MAX7219 Mqtt Ticker

Little nice project with an ESP8266 and a MAX7219 module. 
The MAX7219 is connected to the ESP8266 via SPI. 
It is possible to control the displayed value and its speed and intensity via the MQTT protocol.

Currently I use it as a digital clock or to display a telegam message via a MQTT-Brocker. 

Using the following libraries:
- Wifi administrator
https://github.com/tzapu/WiFiManager.git
- MD_MAX72XX
https://github.com/MajicDesigns/MD_MAX72XX.git
- PubSubClient
https://github.com/knolleary/pubsubclient.git

Translated with www.DeepL.com/Translator (free version)