# ads7830
ADS7830 ADC VarServer Interface

## Overview

The ADS7830 is an 8-bit, 8-Channel Sampling A/D Converter with an I2C
interface.  The ADS7830 server maps the ADS7830 ADC channels to VarServer
variables via a JSON configuration file.

Each Analog input can be individually configured to be sampled at an interval
(specified in milliseconds), or sampled on-demand when the variable value is
requested.

The ADS7830 VarServer variables are populated with channel counts.  These
counts can be converted to voltages using the formula:

```
v = ( chval / 255 ) * 3.3
```

The /HW/ADS7830/INFO variable renders a full ADC channel list with
sample interval, counts, and voltage per channel.

## Configuration File

The configuration file contains a setting for the i2c device to use to
communicate with the ADC7830, and the i2c slave address of the ADS7830
device.

Each channel of the ADS7830 is specified with a channel number, an
associated VarServer variable name, and an optional sampling rate
(in milliseconds).

A sample configuration file is shown below:

```
{
    "device" : "/dev/i2c-1",
    "address" : "0x4b",
    "channels" : [
        {
          "channel" : "0",
          "var" : "/HW/ADS7830/A0"
        },
        {
          "channel" : "1",
          "var" : "/HW/ADS7830/A1",
          "interval" : "100"
        },
        {
          "channel" : "2",
          "var" : "/HW/ADS7830/A2"
        },
        {
          "channel" : "3",
          "var" : "/HW/ADS7830/A3",
          "interval" : "1000"
        },
        {
          "channel" : "4",
          "var" : "/HW/ADS7830/A4"
        },
        {
          "channel" : "5",
          "var" : "/HW/ADS7830/A5"
        },
        {
          "channel" : "6",
          "var" : "/HW/ADS7830/A6"
        },
        {
          "channel" : "7",
          "var" : "/HW/ADS7830/A7"
        }
    ]
}
```

## Prerequisites

The ADS7830 service requires the following components:

- varserver : variable server ( https://github.com/tjmonk/varserver )
- tjson : JSON parser library ( https://github.com/tjmonk/libtjson )
- varcreate : create variables from a JSON file ( https://github.com/tjmonk/varcreate )

Of course you must have a device with a I2C interface connected to an ADS7830
IC to run the ADS7830 service with any meaningful results.

## Build

```
./build.sh
```

## Set up the VarServer variables

```
varserver &
varcreate test/vars.json
```

## Start the ADS7830 service

```
ads7830 test/ads7830.json &
```

## Query the ADS7830 ADC channels

```
vars -vn /ADS7830/A
```

```
/HW/ADS7830/A0=0
/HW/ADS7830/A1=103
/HW/ADS7830/A2=0
/HW/ADS7830/A3=0
/HW/ADS7830/A4=0
/HW/ADS7830/A5=0
/HW/ADS7830/A6=0
/HW/ADS7830/A7=0
```

## Get an ADS7830 Channel Summary

```
getvar /HW/ADS7830/INFO
```

```
ADS7830 Status:
Configuration File: /home/pi/tgp/ads7830/test/ads7830.json
Device: /dev/i2c-1
Address: 0x4b
Exclusive: false
Verbose: false
Channels:
        A0: /HW/ADS7830/A0 ------- 000 0.00V
        A1: /HW/ADS7830/A1  100 ms 103 1.33V
        A2: /HW/ADS7830/A2 ------- 000 0.00V
        A3: /HW/ADS7830/A3 1000 ms 000 0.00V
        A4: /HW/ADS7830/A4 ------- 000 0.00V
        A5: /HW/ADS7830/A5 ------- 000 0.00V
        A6: /HW/ADS7830/A6 ------- 000 0.00V
        A7: /HW/ADS7830/A7 ------- 000 0.00V
```


