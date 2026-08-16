"""OBIS code table for the EGD/E.ON HAN push interface (see egd_rs485.pdf, page 1)."""

SENSOR_OBIS = {
    "power_import": "1-0:1.7.0.255",
    "power_import_l1": "1-0:21.7.0.255",
    "power_import_l2": "1-0:41.7.0.255",
    "power_import_l3": "1-0:61.7.0.255",
    "power_export": "1-0:2.7.0.255",
    "power_export_l1": "1-0:22.7.0.255",
    "power_export_l2": "1-0:42.7.0.255",
    "power_export_l3": "1-0:62.7.0.255",
    "energy_import": "1-0:1.8.0.255",
    "energy_import_rate1": "1-0:1.8.1.255",
    "energy_import_rate2": "1-0:1.8.2.255",
    "energy_import_rate3": "1-0:1.8.3.255",
    "energy_import_rate4": "1-0:1.8.4.255",
    "energy_export": "1-0:2.8.0.255",
    "power_limit": "0-0:17.0.0.255",
}

TEXT_SENSOR_OBIS = {
    "serial_number": "0-0:96.1.0.255",
    "device_name": "0-0:42.0.0.255",
    "active_tariff": "0-0:96.14.0.255",
    "consumer_message": "0-0:96.13.0.255",
}

BINARY_SENSOR_OBIS = {
    "disconnect_status": "0-0:96.3.10.255",
    "relay1": "0-1:96.3.10.255",
    "relay2": "0-2:96.3.10.255",
    "relay3": "0-3:96.3.10.255",
    "relay4": "0-4:96.3.10.255",
    "relay5": "0-5:96.3.10.255",
    "relay6": "0-6:96.3.10.255",
}
