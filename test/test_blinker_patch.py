"""Host regression tests for the methods patched in the actual Blinker header."""

import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("patch_blinker", ROOT / "scripts" / "patch_blinker.py")
patcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(patcher)


class PatchTests(unittest.TestCase):
    def test_idempotent_and_preserves_line_endings(self):
        for newline in ("\n", "\r\n"):
            with self.subTest(newline=newline), tempfile.TemporaryDirectory() as temporary:
                header = Path(temporary) / "src" / "Adapters" / "BlinkerMQTT.h"
                header.parent.mkdir(parents=True)
                original = "\n".join(before for _, before, _ in patcher.REPLACEMENTS)
                header.write_bytes(original.replace("\n", newline).encode())
                mqtt_header = Path(temporary) / "src" / "modules" / "mqtt" / "Adafruit_MQTT.h"
                mqtt_header.parent.mkdir(parents=True)
                mqtt_original = "\n".join(before for _, before, _ in patcher.TIMEOUT_REPLACEMENTS)
                mqtt_header.write_bytes(mqtt_original.replace("\n", newline).encode())
                patcher.patch_library(temporary)
                patched = header.read_bytes()
                mqtt_patched = mqtt_header.read_bytes()
                patcher.patch_library(temporary)
                self.assertEqual(patched, header.read_bytes())
                self.assertEqual(mqtt_patched, mqtt_header.read_bytes())
                for _, _, after in patcher.REPLACEMENTS:
                    self.assertIn(after.replace("\n", newline).encode(), patched)
                for _, _, after in patcher.TIMEOUT_REPLACEMENTS:
                    self.assertIn(after.replace("\n", newline).encode(), mqtt_patched)

    def test_unknown_dependency_is_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            header = Path(temporary) / "src" / "Adapters" / "BlinkerMQTT.h"
            header.parent.mkdir(parents=True)
            original = "\n".join(before for _, before, _ in patcher.REPLACEMENTS[:-1]).encode()
            header.write_bytes(original)
            with self.assertRaisesRegex(RuntimeError, "patch mismatch"):
                patcher.patch_library(temporary)
            self.assertEqual(original, header.read_bytes())

    def test_actual_cpp_methods(self):
        library = ROOT / ".pio" / "libdeps" / "wt32-eth01" / "Blinker"
        source = (library / "src" / "Adapters" / "BlinkerMQTT.h").read_text(encoding="utf-8")
        for _, _, after in patcher.REPLACEMENTS:
            self.assertIn(after, source, "Run the PlatformIO build to apply the fixes first")
        mqtt_source = (library / "src" / "modules" / "mqtt" / "Adafruit_MQTT.h").read_text(encoding="utf-8")
        for _, _, after in patcher.TIMEOUT_REPLACEMENTS:
            self.assertIn(after, mqtt_source)

        methods = []
        for start, end in (
            ("int BlinkerMQTT::connect()", "int BlinkerMQTT::connected()"),
            ("void BlinkerMQTT::disconnect()", "void BlinkerMQTT::ping()"),
            ("void BlinkerMQTT::subscribe()", "void BlinkerMQTT::parseData("),
            ("void BlinkerMQTT::connectWiFi(const char*", "\n#endif"),
        ):
            offset = source.index(start)
            methods.append(source[offset:source.index(end, offset)])

        compiler = shutil.which("g++")
        if compiler is None and Path("C:/msys64/ucrt64/bin/g++.exe").exists():
            compiler = "C:/msys64/ucrt64/bin/g++.exe"
        self.assertIsNotNone(compiler, "A host C++ compiler is required for these regression tests")

        with tempfile.TemporaryDirectory() as temporary:
            source_file = Path(temporary) / "regression.cpp"
            executable = Path(temporary) / ("regression.exe" if os.name == "nt" else "regression")
            source_file.write_text(STUBS + "\n".join(methods) + CASES, encoding="utf-8")
            environment = os.environ.copy()
            environment["PATH"] = str(Path(compiler).parent) + os.pathsep + environment.get("PATH", "")
            compilation = subprocess.run(
                [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-comment",
                 "-Wno-unused-variable", str(source_file), "-o", str(executable)],
                env=environment, capture_output=True, text=True)
            self.assertEqual(compilation.returncode, 0, compilation.stdout + compilation.stderr)
            result = subprocess.run([str(executable)], check=True, env=environment,
                                    capture_output=True, text=True)
            self.assertIn("10 C++ regression checks passed", result.stdout)


STUBS = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#define BLINKER_LOG(...) do {} while (0)
#define BLINKER_LOG_ALL(...) do {} while (0)
#define BLINKER_ERR_LOG(...) do {} while (0)
#define BLINKER_LOG_FreeHeap() do {} while (0)
#define BLINKER_MQTT_CONNECT_TIMESLOT 5000UL
#define BLINKER_SERVER_CONNECT_LIMIT 3
#define WL_CONNECTED 3
#define WIFI_STA 1
#define BLINKER_F(value) value
using String = std::string;
uint32_t fakeTime = 10000;
uint32_t millis() { return fakeTime; }
void yield() {}
struct Adafruit_MQTT_Subscribe { uint8_t lastread[64]{}; };
struct FakeMqtt {
    bool tcp = true;
    bool extra = true;
    int result = 0;
    int connections = 0;
    int stops = 0;
    std::string topic;
    uint8_t data[64]{};
    bool connected() { return tcp; }
    int connect() { ++connections; tcp = true; return result; }
    void disconnectServer() { ++stops; tcp = false; }
    void subscribeTopic(const char*) {}
    Adafruit_MQTT_Subscribe* readSubscription(int) { return nullptr; }
    bool check_extra() { bool value = extra; extra = false; return value; }
    char* get_extra_topic() { return topic.data(); }
    uint8_t* get_extra_data() { return data; }
};
FakeMqtt* mqtt_MQTT = nullptr;
Adafruit_MQTT_Subscribe* iotSub_MQTT = nullptr;
char* BLINKER_RRPC_SUB_TOPIC_MQTT = nullptr;
char* BLINKER_SUB_TOPIC_MQTT = nullptr;
bool isMQTTinit = true;
struct FakeWebSocket { void loop() {} void disconnect() {} } webSocket_MQTT;
struct FakeWiFi {
    int state = WL_CONNECTED;
    int begins = 0;
    int modes = 0;
    std::string ssid = "saved-network";
    int status() { return state; }
    std::string SSID() { return ssid; }
    void mode(int) { ++modes; }
    void begin(const char*) { ++begins; }
    void begin(const char*, const char*) { ++begins; }
} WiFi;
std::string macDeviceName() { return "test"; }
class BlinkerMQTT {
public:
    bool isMQTTset = false;
    bool handle = false;
    bool* isHandle = &handle;
    uint32_t latestTime = 0;
    unsigned reconnect_time = 0;
    unsigned _reRegister_times = 0;
    uint32_t _reRegister_time = 0;
    bool is_rrpc = false;
    char message_id[24]{};
    uint32_t canary = 0x12345678;
    int parsed = 0;
    bool checkInit() { return true; }
    bool reRegister() { return false; }
    void parseData(const char*) { ++parsed; }
    int connect();
    void disconnect();
    void subscribe();
    void connectWiFi(const char*, const char*);
};
'''

CASES = r'''
int main() {
    FakeMqtt socket;
    mqtt_MQTT = &socket;
    BlinkerMQTT client;
    socket.topic = "/device/test/unmatched";
    client.subscribe();
    assert(client.parsed == 0 && !socket.extra);

    char prefix[] = "/sys/test/rrpc/request/";
    BLINKER_RRPC_SUB_TOPIC_MQTT = prefix;
    socket.extra = true;
    client.subscribe();
    assert(client.parsed == 0);

    socket.topic = std::string(prefix) + "123456";
    socket.extra = true;
    client.subscribe();
    assert(client.parsed == 1 && std::string(client.message_id) == "123456");

    socket.topic = std::string(prefix) + std::string(23, 'x');
    socket.extra = true;
    client.subscribe();
    assert(client.parsed == 2 && std::strlen(client.message_id) == 23);

    socket.topic = std::string(prefix) + std::string(24, 'x');
    socket.extra = true;
    client.subscribe();
    assert(client.parsed == 2 && client.canary == 0x12345678);

    client.disconnect();
    assert(!socket.tcp && socket.stops == 1);

    socket.result = -2;
    assert(!client.connect());
    assert(!socket.tcp && socket.connections == 1);
    assert(client.latestTime == fakeTime);

    assert(!client.connect() && socket.connections == 1);
    fakeTime += BLINKER_MQTT_CONNECT_TIMESLOT;
    socket.result = 0;
    assert(client.connect() && socket.connections == 2);

    client.connectWiFi("saved-network", "password");
    assert(WiFi.begins == 0 && WiFi.modes == 0);

    WiFi.state = 0;
    client.connectWiFi("saved-network", "password");
    assert(WiFi.begins == 1 && WiFi.modes == 1);
    std::cout << "10 C++ regression checks passed\n";
}
'''


if __name__ == "__main__":
    unittest.main()
