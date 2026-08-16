import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_WATT,
    UNIT_WATT_HOURS,
)

from . import CONF_DLMS_PUSH_METER_ID, DlmsPushMeter, obis_to_cpp
from .const import SENSOR_OBIS

CONF_UNKNOWN_OBIS_COUNT = "unknown_obis_count"

_POWER_KEYS = {k for k in SENSOR_OBIS if k.startswith("power_")}
_ENERGY_KEYS = {k for k in SENSOR_OBIS if k.startswith("energy_")}

_SCHEMA = {}
for _key in SENSOR_OBIS:
    if _key in _POWER_KEYS:
        _SCHEMA[cv.Optional(_key)] = sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        )
    elif _key in _ENERGY_KEYS:
        _SCHEMA[cv.Optional(_key)] = sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT_HOURS,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_TOTAL_INCREASING,
        )
    else:  # power_limit
        _SCHEMA[cv.Optional(_key)] = sensor.sensor_schema(
            unit_of_measurement=UNIT_WATT,
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        )

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_DLMS_PUSH_METER_ID): cv.use_id(DlmsPushMeter),
        **_SCHEMA,
        # Diagnostic: how many OBIS codes in the latest telegram matched no
        # configured sensor/text_sensor/binary_sensor. Flags meter firmware
        # changes that add fields this config doesn't know about yet.
        cv.Optional(CONF_UNKNOWN_OBIS_COUNT): sensor.sensor_schema(
            accuracy_decimals=0,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_DLMS_PUSH_METER_ID])
    for key, obis in SENSOR_OBIS.items():
        conf = config.get(key)
        if conf is None:
            continue
        sens = await sensor.new_sensor(conf)
        cg.add(hub.register_sensor(obis_to_cpp(obis), sens))

    if (conf := config.get(CONF_UNKNOWN_OBIS_COUNT)) is not None:
        sens = await sensor.new_sensor(conf)
        cg.add(hub.set_unknown_obis_count_sensor(sens))
