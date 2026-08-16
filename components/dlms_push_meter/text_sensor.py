import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import ENTITY_CATEGORY_DIAGNOSTIC

from . import CONF_DLMS_PUSH_METER_ID, DlmsPushMeter, obis_to_cpp
from .const import TEXT_SENSOR_OBIS

CONF_UNKNOWN_OBIS_LIST = "unknown_obis_list"

_DIAGNOSTIC_KEYS = {"serial_number", "device_name", "consumer_message"}

def _text_sensor_schema(key):
    kwargs = {}
    if key in _DIAGNOSTIC_KEYS:
        kwargs["entity_category"] = ENTITY_CATEGORY_DIAGNOSTIC
    return text_sensor.text_sensor_schema(**kwargs)


CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_DLMS_PUSH_METER_ID): cv.use_id(DlmsPushMeter),
        **{cv.Optional(key): _text_sensor_schema(key) for key in TEXT_SENSOR_OBIS},
        # Diagnostic: "OBIS=value" list of unrecognized fields from the latest
        # telegram, e.g. after a meter firmware update adds a new OBIS code.
        cv.Optional(CONF_UNKNOWN_OBIS_LIST): text_sensor.text_sensor_schema(
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    hub = await cg.get_variable(config[CONF_DLMS_PUSH_METER_ID])
    for key, obis in TEXT_SENSOR_OBIS.items():
        conf = config.get(key)
        if conf is None:
            continue
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(hub.register_text_sensor(obis_to_cpp(obis), sens))

    if (conf := config.get(CONF_UNKNOWN_OBIS_LIST)) is not None:
        sens = await text_sensor.new_text_sensor(conf)
        cg.add(hub.set_unknown_obis_list_text_sensor(sens))
