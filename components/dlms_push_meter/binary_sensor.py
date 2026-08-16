import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY

from . import CONF_DLMS_PUSH_METER_ID, DlmsPushMeter, obis_to_cpp
from .const import BINARY_SENSOR_OBIS

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_DLMS_PUSH_METER_ID): cv.use_id(DlmsPushMeter),
        **{
            cv.Optional(key): binary_sensor.binary_sensor_schema(
                device_class=DEVICE_CLASS_CONNECTIVITY,
            )
            for key in BINARY_SENSOR_OBIS
        },
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_DLMS_PUSH_METER_ID])
    for key, obis in BINARY_SENSOR_OBIS.items():
        conf = config.get(key)
        if conf is None:
            continue
        sens = await binary_sensor.new_binary_sensor(conf)
        cg.add(hub.register_binary_sensor(obis_to_cpp(obis), sens))
