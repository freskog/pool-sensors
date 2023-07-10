# pool-sensors
Arduino project for uploading sensor data from an atlas scientific pool kit to a MQTT broker.

This is the .ino file for uploading sensor data from a atlas scientific pool kit to an MQTT broker. It's just a quick adaptation of the pool-kit example provided as part of the EZO Lib.
To use it you need to add the EZO library from Atlas Scientific, and the PubSubClient library for MQTT communication. It doesn't need the thingspeak library (as it's not used).

