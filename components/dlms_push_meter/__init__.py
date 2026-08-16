"""EGD/E.ON HAN DLMS/COSEM RS485 push meter (data-notification, unidirectional, 9600 Bd)."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart
from esphome.const import CONF_ID

CODEOWNERS = ["@vojtechvladyka"]
DEPENDENCIES = ["uart"]
MULTI_CONF = True

dlms_push_meter_ns = cg.esphome_ns.namespace("dlms_push_meter")
DlmsPushMeter = dlms_push_meter_ns.class_("DlmsPushMeter", cg.Component, uart.UARTDevice)

CONF_DLMS_PUSH_METER_ID = "dlms_push_meter_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(DlmsPushMeter),
        }
    )
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.COMPONENT_SCHEMA)
)

FINAL_VALIDATE_SCHEMA = uart.final_validate_device_schema(
    "dlms_push_meter",
    baud_rate=9600,
    require_rx=True,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)


def obis_to_cpp(obis_str):
    """Convert 'A-B:C.D.E.F' OBIS notation to a C++ Obis{} initializer."""
    a_b, c_d_e_f = obis_str.split(":")
    a, b = a_b.split("-")
    c, d, e, f = c_d_e_f.split(".")
    parts = [a, b, c, d, e, f]
    inner = ", ".join(parts)
    return cg.RawExpression(f"::esphome::dlms_push_meter::Obis{{{inner}}}")
