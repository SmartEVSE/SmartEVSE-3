# Easy integration with Home Assistant
If you want to integrate your SmartEVSE with Home Asisstant, please have a look at the [SmartEVSE `custom_component` for Home Assistant](https://github.com/dingo35/ha-SmartEVSEv3). This `custom_component` uses the SmartEVSE API to share data from the SmartEVSE to Home Assistant, and enables you to set SmartEVSE settings from Home Assistant. You will need firmware version 1.5.2 or higher to use this integration.

The YAML file in the home-assistant folder is deprecated.

# Use ESPHome to provide `current` information to SmartEVSE
Your SmartEVSE has an API endpoint to send L1/L2/L3 data, which means that you don't need to connect a SensorBox to retrieve the required information for load-balancing or solar-charging. 
If you are using a P1 reader (often also called DSMR-reader) with your electricity meter, you might want to send the `current` information (L1/L2/L3) directly from your ESPHome device to your SmartEVSE. 

See the [esphome folder](/integrations/esphome/) for an example ESPHome configuration.
