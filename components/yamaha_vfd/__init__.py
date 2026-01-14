import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import pins
from esphome.components import binary_sensor, text_sensor
from esphome.const import CONF_ID

AUTO_LOAD = ["text_sensor", "binary_sensor"]

yamaha_vfd_ns = cg.esphome_ns.namespace("yamaha_vfd")
YamahaVFD = yamaha_vfd_ns.class_("YamahaVFD", cg.Component)

CONF_CKFD_PIN = "ckfd_pin"
CONF_DTFD_PIN = "dtfd_pin"
CONF_CEFD_PIN = "cefd_pin"
CONF_SOURCE_SENSOR = "source_sensor"
CONF_VOLUME_SENSOR = "volume_sensor"
CONF_MUTE_SENSOR = "mute_sensor"
CONF_POWER_SENSOR = "power_sensor"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(YamahaVFD),
            cv.Required(CONF_CKFD_PIN): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_DTFD_PIN): pins.internal_gpio_input_pin_schema,
            cv.Required(CONF_CEFD_PIN): pins.internal_gpio_input_pin_schema,
            cv.Optional(CONF_SOURCE_SENSOR): cv.Any(
                text_sensor.text_sensor_schema(), cv.use_id(text_sensor.TextSensor)
            ),
            cv.Optional(CONF_VOLUME_SENSOR): cv.Any(
                text_sensor.text_sensor_schema(), cv.use_id(text_sensor.TextSensor)
            ),
            cv.Optional(CONF_MUTE_SENSOR): cv.Any(
                binary_sensor.binary_sensor_schema(), cv.use_id(binary_sensor.BinarySensor)
            ),
            cv.Optional(CONF_POWER_SENSOR): cv.use_id(binary_sensor.BinarySensor),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
)


async def _maybe_get_or_create_text_sensor(conf):
    if isinstance(conf, dict):
        return await text_sensor.new_text_sensor(conf)
    return await cg.get_variable(conf)


async def _maybe_get_or_create_binary_sensor(conf):
    if isinstance(conf, dict):
        return await binary_sensor.new_binary_sensor(conf)
    return await cg.get_variable(conf)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    ckfd = await cg.gpio_pin_expression(config[CONF_CKFD_PIN])
    cg.add(var.set_ckfd_pin(ckfd))

    dtfd = await cg.gpio_pin_expression(config[CONF_DTFD_PIN])
    cg.add(var.set_dtfd_pin(dtfd))

    cefd = await cg.gpio_pin_expression(config[CONF_CEFD_PIN])
    cg.add(var.set_cefd_pin(cefd))

    if CONF_SOURCE_SENSOR in config:
        sens = await _maybe_get_or_create_text_sensor(config[CONF_SOURCE_SENSOR])
        cg.add(var.set_source_sensor(sens))

    if CONF_VOLUME_SENSOR in config:
        sens = await _maybe_get_or_create_text_sensor(config[CONF_VOLUME_SENSOR])
        cg.add(var.set_volume_sensor(sens))

    if CONF_MUTE_SENSOR in config:
        sens = await _maybe_get_or_create_binary_sensor(config[CONF_MUTE_SENSOR])
        cg.add(var.set_mute_sensor(sens))

    if CONF_POWER_SENSOR in config:
        sens = await cg.get_variable(config[CONF_POWER_SENSOR])
        cg.add(var.set_power_sensor(sens))
