# Bluetooth scales library for ESP on Arduino Framework

This library defines3 main abstract concepts:
* A `RemoteScales`  which is used as a common interface to connect to scales, retrieve their weight and tare. It also supports a callback that is triggered when a new weight is received. 
* A `RemoteScalesScanner` which is used to scan for `RemoteScales` instances that are supported, and
* A `RemoteScalesPluginRegistry` which holds all the scales that are supported by the library. 

This allows for easy extention of the library for more bluetooth enabled scales. 

### Currently implemented scales

* [Acaia Lunar](https://acaia.co/collections/coffee-scales/products/lunar_2021) - [Tested]
* [Acaia Pearl](https://acaia.co/collections/coffee-scales/products/pearl) - [Tested]
* [Bookoo Themis](https://bookoocoffee.com/shop/bookoo-mini-scale/?coupon=gaggiuino) - [Tested]

*__There is a big possibility other acaia models work out of the box as well but thye have not ben tested!__*

Want a specific model? Implement it 🚀 Read on to find out how... 

### How do implement new scales

We can do this either in this repo or in a separate repo. In both cases we need to:
1. Create a class for the new Scales (i.e. `AcaiaScales`) that implements the protocol of the scales and extends `RemoteScales`. This is 99.9% of the work as it involves reverse engineering or reading the datasheet of the scales and implementing it accordingly. 
2. Create a plugin (i.e. `AcaiaScalesPlugin`) that extends `RemoteScalesPlugin` and implement an `apply()` method which should register the plugin to the `RemoteScalesPluginRegistry` singleton.
3. Import your new library together with the `remote_scales` library and apply your plugin (i.e. `MyScalesPlugin::apply()`) during the initialisaion phase. 

