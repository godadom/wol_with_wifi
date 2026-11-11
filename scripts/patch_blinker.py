"""Apply the MQTT recovery fixes to the pinned Blinker dependency."""

from pathlib import Path


REPLACEMENTS = (
    (
        "close initialized MQTT client",
        "    if (isMQTTset) {",
        "    if (mqtt_MQTT != nullptr) {",
    ),
    (
        "discard failed MQTT handshake",
        "    if ((ret = mqtt_MQTT->connect()) != 0)\n    {\n",
        "    if ((ret = mqtt_MQTT->connect()) != 0)\n    {\n"
        "        disconnect();\n"
        "        this->latestTime = millis();\n",
    ),
    (
        "guard optional RRPC topic",
        "    if (mqtt_MQTT->check_extra())",
        "    if (mqtt_MQTT->check_extra() && BLINKER_RRPC_SUB_TOPIC_MQTT != nullptr)",
    ),
    (
        "bound RRPC message identifier",
        "            memmove(message_id, \n"
        "                mqtt_MQTT->get_extra_topic()+strlen(BLINKER_RRPC_SUB_TOPIC_MQTT),\n"
        "                strlen(mqtt_MQTT->get_extra_topic()) - strlen(BLINKER_RRPC_SUB_TOPIC_MQTT));",
        "            size_t prefixLength = strlen(BLINKER_RRPC_SUB_TOPIC_MQTT);\n"
        "            size_t messageLength = strlen(mqtt_MQTT->get_extra_topic()) - prefixLength;\n"
        "            if (messageLength >= sizeof(message_id)) {\n"
        "                BLINKER_ERR_LOG(BLINKER_F(\"RRPC message identifier too long\"));\n"
        "                return;\n"
        "            }\n"
        "            memcpy(message_id, mqtt_MQTT->get_extra_topic() + prefixLength, messageLength);",
    ),
    (
        "bound MQTT TLS handshake",
        "        client_s.setInsecure();\n",
        "        client_s.setInsecure();\n"
        "        client_s.setHandshakeTimeout(15);\n"
        "        client_s.setTimeout(10);\n",
    ),
    (
        "bound authentication TLS handshake",
        "    HTTPClient http;\n\n    String url_iot = host;",
        "    WiFiClientSecure authClient;\n"
        "    authClient.setInsecure();\n"
        "    authClient.setHandshakeTimeout(15);\n"
        "    authClient.setTimeout(10);\n"
        "    HTTPClient http;\n"
        "    http.setConnectTimeout(10000);\n"
        "    http.setTimeout(5000);\n\n"
        "    String url_iot = host;",
    ),
    (
        "use bounded authentication client",
        "    http.begin(url_iot);\n",
        "    http.begin(authClient, url_iot);\n",
    ),
    (
        "reuse connected Wi-Fi",
        "void BlinkerMQTT::connectWiFi(const char* _ssid, const char* _pswd)\n{\n",
        "void BlinkerMQTT::connectWiFi(const char* _ssid, const char* _pswd)\n{\n"
        "    if (_ssid && WiFi.status() == WL_CONNECTED && WiFi.SSID() == _ssid) {\n"
        "        return;\n"
        "    }\n\n",
    ),
)

TIMEOUT_REPLACEMENTS = (
    ("MQTT heartbeat reply window", "#define PING_TIMEOUT_MS    500\n",
     "#define PING_TIMEOUT_MS    5000\n"),
    ("MQTT subscription reply window", "#define SUBACK_TIMEOUT_MS  500\n",
     "#define SUBACK_TIMEOUT_MS  5000\n"),
)


def patch_library(library_dir):
    source_dir = Path(library_dir) / "src"
    adapter = source_dir / "Adapters" / "BlinkerMQTT.h"
    mqtt = source_dir / "modules" / "mqtt" / "Adafruit_MQTT.h"
    updates = []
    for header, replacements in ((adapter, REPLACEMENTS), (mqtt, TIMEOUT_REPLACEMENTS)):
        original = header.read_bytes()
        source = original.decode("utf-8").replace("\r\n", "\n")
        for name, before, after in replacements:
            if source.count(after) == 1:
                continue
            if source.count(before) != 1:
                raise RuntimeError(f"Blinker patch mismatch ({name}) in {header}")
            source = source.replace(before, after, 1)
        newline = "\r\n" if b"\r\n" in original else "\n"
        patched = source.replace("\n", newline).encode("utf-8")
        if patched != original:
            updates.append((header, patched))

    for header, patched in updates:
        header.write_bytes(patched)
        print(f"Applied Blinker MQTT recovery fixes to {header.name}")
    return adapter


try:
    Import("env")
except NameError:
    if __name__ == "__main__":
        import argparse

        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("library_dir", type=Path)
        patch_library(parser.parse_args().library_dir)
else:
    patch_library(Path(env.subst("$PROJECT_LIBDEPS_DIR")) / env.subst("$PIOENV") / "Blinker")
