# emmbus2influx
## read wired M-Bus devices (Serial or TCP) and write to infuxdb (V1 or V2) and/or MQTT

still work in progress, works but not yet long time tested

### Definitions
- **MeterType** - a definition of the fields (from records)
- **Meter** - a definition of a physicalM-Bus device based on a MeterType

### Features

 - M-Bus serial via one serial port (if you need more serial ports, you can start emmbus2Influx multiple times)
 - unlimited number of metertypes and meters
 - supports formulas for changing values after read or for defining new fields (in the metertype as well as in the meter definition)
 - MQTT data is written on every query to have near realtime values (if MQTT is enabled), InfluxDB writes can be restricted to n queries where you can specify for each field if max,min or average values will be posted to InfluxDB
 - Virtual devices=Meters can be defined, these can post values from different M-Bus devices to MQTT and/or InfluxDB
 - Using formulas and virtual meters without any M-Bus device, test data for MQTT and/or InfluxDB can be generated (example: config-02.conf)
 - supports dryrun for testing definitions
 - supports interactive formula testing
 - use of [libmbus](https://github.com/rscada/libmbus) for M-Bus communication
 - use of [paho-c](https://github.com/eclipse/paho.mqtt.c) for MQTT
 - use of [muparser](https://beltoforion.de/en/muparser/) for formula parsing
 - paho-c and muparser can by dynamic linked (default) or downloaded, build and linked static automatically when not available on target platform, e.g. Victron Energy Cerbox GX (to be set at the top of Makefile)

### Restrictions
- currently only one serial M-Bus interface (=serial port) is supported (can be used by multiple Meters)
- all Meters will be queried at the specified poll interval, however, InfluxDB writes can be delayed

### Get started

emmbus2Influx requires a configuration file. By default ./emmbus2Influx.conf is used. You can define another config file using the

```
--configfile=pathOfConfigFile
```

command line parameter.
The config file consists of two sections where the first section ends when [ was found as the first character of a line. The first section contains command line parameters in long format while the second section contains

- first the meter types and
- second the meter definitions.

Parameters given in the first section can be overridden by command line parameters.
Example uncomplete config file:

```
device=/dev/ttyUSB0
baud=2400

[MeterType]
name = "Engelmann_SensoStar"
"serialNumber"  = 0,influx=0
"kwh"     = 1
"temp"    = 7
"tempRet" = 8
measurement="heatMeter"
influxwritemult=12

[Meter]
type="Engelmann_SensoStar"
address=0
#hostname="localhost"
#port="4161"
measurement="HeatConsumption"
name="flat1"
disabled=0
```
If emmbus2Influx is started with --baud=300 the 2400 baud in the config file will be ignored.
Comments can be included using #. Everything after # in a line will be ignored.
Numbers can be specified decimal or, when prefixwed with 0x, hexadecimal.

## command line options or options in the first section of the config file

Long command line options requires to be prefixed with -- while as in the config file the option has to be specified without the prefix. Short command line options can only be used on command line. The descriptions below show the options within the config file, if used on command line, a prefix of -- is required.

```
 -h, --help              show this help and exit
  --configfile=           config file name
  -d, --device=           specify serial device name
  --baud=                 baudrate (2400)
  -m, --measurement=      Influxdb measurement (energyMeter)
  -g, --tagname=          Influxdb tag name (Device)
  -s, --server=           influxdb server name or ip (lnx.armin.d)
  -o, --port=             influxdb port (8086)
  -b, --db=               Influxdb v1 database name
  -u, --user=             Influxdb v1 user name
  -p, --password=         Influxdb v1 password
  -B, --bucket=           Influxdb v2 bucket (ad)
  -O, --org=              Influxdb v2 org (diehl)
  -T, --token=            Influxdb v2 auth api token
  --influxwritemult=      Influx write multiplicator
  -c, --cache=            #entries for influxdb cache (1000)
  -M, --mqttserver=       mqtt server name or ip (lnx.armin.d)
  -C, --mqttprefix=       prefix for mqtt publish (ad/house/energy/)
  -R, --mqttport=         ip port for mqtt server (1883)
  -Q, --mqttqos=          default mqtt QOS, can be changed for meter (0)
  -r, --mqttretain=       default mqtt retain, can be changed for meter (0)
  -v, --verbose[=]        increase or set verbose level
  -G, --modbusdebug       set debug for libmodbus
  -P, --poll=             poll intervall in seconds
  -y, --syslog            log to syslog insead of stderr
  -Y, --syslogtest        send a testtext to syslog and exit
  -e, --version           show version and exit
  -D, --dumpregisters     Show registers read from all meters and exit, twice to show received data
  -U, --dryrun[=]         Show what would be written to MQQT/Influx for one query and exit
  -t, --try               try to connect returns 0 on success
  --formtryt=             interactive try out formula for register values for a given meter name
  --formtry               interactive try out formula (global for formulas in meter definition)
  --scanserial            scan for serial mbus devices (0)
```

### serial port
```
device=/dev/ttyUSB0
baud=2400
```

Specify the serial port parameters.

### InfluxDB - common for version 1 and 2

```
server=ip_or_server_name_of_influxdb_host
port=8086
measurement=energyMeter
tagname=Meter
cache=1000
```

If server is not specified, post to InfluxDB will be disabled at all (if you would like to use MQTT only). tagname will be the tag used for posting to Influxdb. Cache is the number of posts that will be cached in case the InfluxDB server is not reachable. This is implemented as a ring buffer. The entries will be posted after the InfluxDB server is reachable again. One post consists of the data for all meters queried at the same time.
measurement sets the default measurement and can be overriden in a meter type or in a meter definition.

### InfluxDB version 1

For version 1, database name, username and password are used for authentication.

```
db=
user=
password=
```

### InfluxDB version 2

Version 2 requires bucket, org and token:

```
--bucket=
--org=
--token=
```

### MQTT

```
mqttserver=
mqttprefix=ad/house/energy/
mqttport=1883
mqttqos=0
mqttretain=0
```

Parameters for MQTT. If mqttserver is not specified, MQTT will be disabled at all (if you would like to use InfluxDB only).
mqttqos and mqttretain sets the default, these can be overriden per meter or MeterType definition.
__mqttqos__:
- At most once (0)
- At least once (1)
- Exactly once (2)

__mqttretain__:
- no (0)
- yes (1)

### additional options
```

verbose=0
syslog
poll=5
```

__verbose__: sets the verbisity level
__syslog__: enables messages to syslog instead of stdout.
__poll__: sets the poll interval in seconds

### command line only parameters

```
--configfile=
--syslogtest
--version
--dryrun
--dryrun=n
--try
--formtryt=MeterName
--formtry
--scanserial
```
__configfile__: sets the config file to use, default is ./emModbus2influx.conf
**syslogtest**: sends a test message to syslog.
**dryrun**: perform one query of all meters and show what would be posted to InfluxDB / MQTT
**dryrun=n**: perform n querys of all meters and show what would be posted to InfluxDB / MQTT
**try**: try to reach the first defined serial M-Bus device (to detect the serial port in scripts). Return code is 0 if the first device can be reached or 1 on failure.
**formtryt**: interactively try out a formula for a MeterType
**formtry**: interactively try out a formula for a Meter

# MeterType definitions
Defines a meter type. A meter type is the base definition and can be used within multiple meters. It defines registers and register options for M-Bus. Registers can be values from records, values plus a formula or formula only registers. Additional formula only registers can be added in a meter definition.
Example:
```
# energy meter via Victon Energy Cerbos GX (Modbus TCP)
[MeterType]
name = "Engelmann_SensoStar"
"serialNumber"  = 0,influx=0
"kwh"           = 1,force=int
"temp"          = 7,force=int
"tempRet"       = 8,force=int
measurement="heatMeter"
influxwritemult=12
```

to use this meter type a minimal meter definition could be:
```
[Meter]
type="Engelmann_SensoStar"
name="flat1"
disabled=0
address=0                   # M-Bus address
#hostname="localhost"       # if hostname is specified, TCP will be used, serial otherwise
#port="4161"
measurement="HeatConsumption"
```

Each meter type definition starts with
```
[MeterType]
```
where the [ has to be the first character of a line. Options that can be specified for a meter type are:

```name="NameOfMeter"```
Mandatory, name of the MeterType. The name is used in a meter definition and is case sensitive.

```influx=0|1```
0 will disable this register for influxdb, sets default for registers below
```mqtt=0|1```
0 will disable this register for mqtt, sets default for registers below

```measurement="InfluxMeasurement"```
Overrides the default InfluxDB measurement for this meter.

```mqttqos=```
```mqttretain=```
Overrides the MQTT default for QOS and RETAIN. Default are 0 bus can be specified via command line parameters or in the command line section of the config file. Can be set by MeterType as well.

```influxwritemult=```
Overrides the default from command line or config file. 0=Disable or >= 2.
2 means we will write data to influx on every second query. Values written can be the max, min value or the average. See Options (imax,imin,iavg)

### Register definitions within MeterTypes

for each register,
```"name"=recordNumber```
 or
 ```"name"="Formula"```
 has to be specified. Registers of this MeterType can be referenced within formulas by using its name, the following sample calculates the maximum of each phase voltage and saves the result in the new register uMax:
```
"uMax"="max(u1,u2,3)"
```
 Additional, optional parameters may be specified (separated by comma). If no data type is specified, int16 will be assumed. A name must be specified with quotes to be able to use reserved words like "name". Example:
 ```
"p1" = 0x0012,int32,arr="p",dec=0,div=10,imax
"p2" = 0x0012,int32,arr="p",dec=0,div=10,imax
"p3" = 0x0012,int32,array="p",dec=0,div=10,imax
"p" = "p1+p2+p3",float,influx=0,iavg
"kwh"=0x0040,int32,force=int,div=10,imax
```

#### Options

```arr=name```
This option is applicable for mqtt only and will be ignored for influx. Values will be written as an array for fields with the same name e.g.
```
"PowerL1"=1,int16,arr=PWR
"PowerL2"=2,int16,arr=PWR
"PowerL3"=3,arr=PWR
```
will be written as "PWR":[-13, -72, -35]

```force=```
Can be int or float. Forces the data type to be saved to influxDB. (Interally all registers are stored as float)

```dec=```
Number of decimals, has to be >= 0

```div=```
Divider

```mul=```
Multiplier

```formula="..."```
The result of the formula will be the new value. The current value as well as the values of other registers can be accessed by its name. Formulas will be executed after all registers within a meter has been read.

```influx=```
0 or 1, 0 will disable this register for influxdb. The default if influx= is not specified is 1.

```mqtt=```
0 or 1, 0 will disable this register for influxdb. The default if influx= is not specified is 1.

```imax```
```imin```
```iavg```
When using influxwritemult= , these options specify if the maximun, minimum or the average value will be written to influxdb.

# Meter definitions
Each meter definition starts with
```[Meter]```
where the [ has to be the first character of a line. Options that can be specified for a meter type are:

```name="NameOfMeter"```
Mandatory, name of the Meter. The name is used for InfluxDB as well as MQTT.

```type="NameOfMeterType"```
Type of the Meter, mandatory if M-Bus queries are required. The name is case sensitive and requires the MeterType to be defined in the config file before the meter definition. In case the meter consists of formulas only, MeterType is not required.

```address=M-BusAddress```
Modbus slave address, mandatory if modbus queries are required.

```hostname="HostnameOfModbusSlave"```
The hostname for M-Bus TCP gateways or devices. If not specified, modbus-rtu will be used. All meters with the same hostname share a TCP connection.

```port="PortOfModbusSlave"```
The port for the M-Bus gateway or device.

```Disabled=1```
1 will disable the meter, 0 will enable it. Defaults to 0 if not specified.

```measurement="InfluxMeasurement"```
Overrides the default (or the value from the meter type) InfluxDB measurement for this meter.

```mqttqos="```
```mqttretain="```
Overrides the MQTT default (or the value from the meter type) for QOS and RETAIN. Default are 0 bus can be specified via command line parameters or in the command line section of the config file. Can be set by MeterType as well.

```influxwritemult=```
Overrides the default from command line, config file or meter type. 0=Disable or >= 2.
2 means we will write data to influx on every second query.

```"name"="Formula"```
Defines a virtual register. Registers of this or other meters can be accessed by MeterName.RegisterName. Formulas will be evaluated, in the sequence they appear in the config file, after all meters have been read. Sample for a virtual meter:
```
# "virtual" meter
[Meter]
name="virtual"
disabled=0
"u1_avg"="(Grid.u1+Grid.u2+Grid.u3)/3",dec=2,influx=1,mqtt=1
```
Options supported are dec=, influx=, mqtt=, arr=, imax, imin and iavg
