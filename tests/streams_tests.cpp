#include <QtTest>

#include "streams.h"

namespace {

// `pactl -f json list sink-inputs` from the development machine: a browser
// playing through EasyEffects, plus an unlinked, corked pw-cat stream named
// FakeApp. Volume and format fields are trimmed and the user, host and
// machine id replaced; everything else is as printed.
const QByteArray SinkInputs = R"([
 {"index":544,"driver":"PipeWire","owner_module":null,"client":"543","sink":143,
  "sample_specification":"float32le 2ch 48000Hz","channel_map":"front-left,front-right",
  "corked":false,"mute":false,"balance":0.0,"buffer_latency_usec":0.0,"sink_latency_usec":0.0,
  "resample_method":"PipeWire",
  "properties":{"client.api":"pipewire-pulse","pulse.server.type":"unix",
   "application.name":"Helium","application.process.id":"1000",
   "application.process.user":"user","application.process.host":"host",
   "application.process.binary":"helium","application.language":"en_US.UTF-8",
   "window.x11.display":":0","application.process.machine_id":"0000",
   "application.process.session_id":"2","application.icon_name":"helium",
   "media.name":"Playback","pulse.corked":"false","node.rate":"1/48000",
   "node.latency":"1024/48000","stream.is-live":"true","node.name":"Helium",
   "node.want-driver":"true","node.autoconnect":"true","media.class":"Stream/Output/Audio",
   "port.group":"stream.0","object.register":"false","factory.id":"7",
   "client.id":"119","object.id":"97","object.serial":"544","node.driver-id":"83",
   "module-stream-restore.id":"sink-input-by-application-name:Helium"}},
 {"index":768,"driver":"PipeWire","owner_module":null,"client":"767","sink":4294967295,
  "sample_specification":"float32le 2ch 48000Hz","channel_map":"front-left,front-right",
  "corked":true,"mute":false,"balance":0.0,"buffer_latency_usec":0.0,"sink_latency_usec":0.0,
  "resample_method":"PipeWire",
  "properties":{"application.name":"FakeApp","node.name":"pw-cat","node.dont-fallback":"true",
   "node.autoconnect":"false","media.name":"fake","node.rate":"1/48000",
   "node.latency":"4800/48000","media.type":"Audio","media.category":"Playback",
   "media.role":"music","target.object":"999999","media.class":"Stream/Output/Audio",
   "client.id":"158","object.id":"154","object.serial":"768",
   "module-stream-restore.id":"sink-input-by-media-role:music"}}
])";

// The parts of `pw-dump` that matter, from the same machine: the settings
// and default metadata, the USB DAC's ALSA sink, and EasyEffects' null sink
// (no Latency of its own). Node props are trimmed; params are as printed.
const QByteArray AlsaDump = R"([
 {"id":31,"type":"PipeWire:Interface:Metadata","props":{"metadata.name":"settings"},
  "metadata":[{"subject":0,"key":"clock.rate","type":"","value":48000},
              {"subject":0,"key":"clock.quantum","type":"","value":1024}]},
 {"id":40,"type":"PipeWire:Interface:Metadata","props":{"metadata.name":"default"},
  "metadata":[
   {"subject":0,"key":"default.configured.audio.sink","type":"Spa:String:JSON",
    "value":{"name":"alsa_output.usb-C-Media_Electronics_Inc._USB_HIFI_AUDIO-00.analog-stereo"}},
   {"subject":0,"key":"default.audio.sink","type":"Spa:String:JSON",
    "value":{"name":"alsa_output.usb-C-Media_Electronics_Inc._USB_HIFI_AUDIO-00.analog-stereo"}},
   {"subject":0,"key":"default.audio.source","type":"Spa:String:JSON",
    "value":{"name":"alsa_input.usb-Webcam-02.analog-stereo"}},
   {"subject":97,"key":"target.object","type":"Spa:Id","value":143}]},
 {"id":83,"type":"PipeWire:Interface:Node","info":{"state":"running",
  "props":{"api.alsa.headroom":512,"device.api":"alsa","factory.name":"api.alsa.pcm.sink",
   "media.class":"Audio/Sink","node.description":"USB HIFI AUDIO Analog Stereo","node.driver":true,
   "node.name":"alsa_output.usb-C-Media_Electronics_Inc._USB_HIFI_AUDIO-00.analog-stereo",
   "object.id":83,"object.serial":100},
  "params":{"Latency":[
    {"direction":"Input","minQuantum":1.0,"maxQuantum":1.0,"minRate":512,"maxRate":512,"minNs":0,"maxNs":0},
    {"direction":"Output","minQuantum":0.0,"maxQuantum":0.0,"minRate":0,"maxRate":0,"minNs":0,"maxNs":0}],
   "ProcessLatency":[{"quantum":0.0,"rate":0,"ns":0}]}}},
 {"id":95,"type":"PipeWire:Interface:Node","info":{"state":"running",
  "props":{"factory.name":"support.null-audio-sink","media.class":"Audio/Sink",
   "node.description":"Easy Effects Sink","node.driver":true,"node.name":"easyeffects_sink",
   "object.id":95,"object.serial":143},
  "params":{"Latency":[],"ProcessLatency":[]}}}
])";

// No Bluetooth sink was connected while this was written, so this one is
// built from PipeWire 1.6.9's spa/plugins/bluez5/media-sink.c: set_latency()
// reports packet, codec and transport delay in minNs, with minQuantum 0 for
// A2DP. The latency sits on the input port only here, to exercise the port
// fallback.
const QByteArray BluetoothDump = R"([
 {"id":40,"type":"PipeWire:Interface:Metadata","props":{"metadata.name":"default"},
  "metadata":[{"subject":0,"key":"default.audio.sink","type":"Spa:String:JSON",
               "value":{"name":"bluez_output.00_11_22_33_44_55.1"}}]},
 {"id":201,"type":"PipeWire:Interface:Node","info":{"state":"running",
  "props":{"device.api":"bluez5","api.bluez5.codec":"aac","media.class":"Audio/Sink",
   "node.name":"bluez_output.00_11_22_33_44_55.1"},
  "params":{"ProcessLatency":[{"quantum":0.0,"rate":0,"ns":0}]}}},
 {"id":202,"type":"PipeWire:Interface:Port","info":{"direction":"input",
  "props":{"node.id":201,"port.name":"playback_FL"},
  "params":{"Latency":[
    {"direction":"Input","minQuantum":0.0,"maxQuantum":0.0,"minRate":0,"maxRate":0,
     "minNs":187000000,"maxNs":187000000}]}}}
])";

} // namespace

class StreamsTests : public QObject {
    Q_OBJECT

private slots:
    void parsesSinkInputs() {
        const QVector<streams::SinkInput> list = streams::parseSinkInputs(SinkInputs);
        QCOMPARE(list.size(), 2);
        QCOMPARE(list[0].serial, 544);
        QCOMPARE(list[0].appName, QString("Helium"));
        QCOMPARE(list[0].binary, QString("helium"));
        QVERIFY(!list[0].corked);
        QCOMPARE(list[1].serial, 768);
        QVERIFY(list[1].binary.isEmpty());
        QVERIFY(list[1].corked);
    }

    void badJsonHasNoStreams() {
        QVERIFY(streams::parseSinkInputs("").isEmpty());
        QVERIFY(streams::parseSinkInputs("{\"not\":\"a list\"}").isEmpty());
    }

    void matchesNameOrBinaryIgnoringCase() {
        const auto list = streams::parseSinkInputs(SinkInputs);
        QVERIFY(streams::matches(list[0], "helium"));
        QVERIFY(streams::matches(list[0], "HELIUM"));
        QVERIFY(streams::matches(list[1], "fakeapp"));
        QVERIFY(!streams::matches(list[0], "heli"));
        QVERIFY(!streams::matches(list[0], ""));
    }

    void picksPlayingThenNewest() {
        using streams::SinkInput;
        const QVector<SinkInput> list = {
            {10, "Firefox", "firefox", false}, {12, "Firefox", "firefox", true},
            {11, "Firefox", "firefox", false}, {20, "mpv", "mpv", true}};
        QCOMPARE(streams::pickTarget(list, "firefox"), 11); // newest playing, not the newer paused one
        QCOMPARE(streams::pickTarget(list, "mpv"), 20);     // a paused stream still counts
        QCOMPARE(streams::pickTarget(list, "spotify"), -1);
        QCOMPARE(streams::pickTarget(list, ""), -1);
        QCOMPARE(streams::pickTarget(streams::parseSinkInputs(SinkInputs), "Helium"), 544);
    }

    void classifiesSubscribeEvents() {
        using streams::Event;
        QCOMPARE(streams::classifyEvent("Event 'new' on sink-input #755\n"), Event::Streams);
        QCOMPARE(streams::classifyEvent("Event 'remove' on sink-input #755"), Event::Streams);
        QCOMPARE(streams::classifyEvent("Event 'change' on server #-1"), Event::Outputs);
        QCOMPARE(streams::classifyEvent("Event 'change' on sink #100"), Event::Outputs);
        QCOMPARE(streams::classifyEvent("Event 'new' on client #754"), Event::None);
        QCOMPARE(streams::classifyEvent("Event 'change' on source-output #558"), Event::None);
    }

    void readsDefaultSink() {
        QCOMPARE(streams::defaultSinkName(AlsaDump),
                 QString("alsa_output.usb-C-Media_Electronics_Inc._USB_HIFI_AUDIO-00.analog-stereo"));
        QCOMPARE(streams::defaultSinkName(BluetoothDump), QString("bluez_output.00_11_22_33_44_55.1"));
        // Older pw-dump prints the value as a string holding JSON.
        const QByteArray asString = "[{\"type\":\"PipeWire:Interface:Metadata\",\"props\":{\"metadata.name\":"
                                    "\"default\"},\"metadata\":[{\"subject\":0,\"key\":\"default.audio.sink\","
                                    "\"value\":\"{\\\"name\\\":\\\"usb\\\"}\"}]}]";
        QCOMPARE(streams::defaultSinkName(asString), QString("usb"));
        QVERIFY(streams::defaultSinkName("[]").isEmpty());
    }

    void alsaLatencyIsOnePeriodPlusHeadroom() {
        const auto latency = streams::outputLatency(AlsaDump, 256);
        QVERIFY(latency.has_value());
        QVERIFY(qAbs(*latency - (256.0 + 512.0) / 48000.0) < 1e-9);
    }

    void bluetoothLatencyIsCodecDelay() {
        const auto latency = streams::outputLatency(BluetoothDump, 256);
        QVERIFY(latency.has_value());
        QVERIFY(qAbs(*latency - 0.187) < 1e-9);
    }

    void unknownSinkHasNoLatency() {
        // EasyEffects' sink as the default: it has no Latency param anywhere.
        QByteArray eeDefault = AlsaDump;
        eeDefault.replace("{\"name\":\"alsa_output.usb-C-Media_Electronics_Inc._USB_HIFI_AUDIO-00.analog-stereo\"}",
                          "{\"name\":\"easyeffects_sink\"}");
        QCOMPARE(streams::defaultSinkName(eeDefault), QString("easyeffects_sink"));
        QCOMPARE(streams::outputLatency(eeDefault, 256), std::optional<double>(0.0));
        QCOMPARE(streams::outputLatency("[]", 256), std::optional<double>(0.0));
        QVERIFY(!streams::outputLatency("not json", 256).has_value());
    }

    void targetArgumentsSwapMonitorForStream() {
        const QStringList monitor = {"--raw", "-P", "stream.capture.sink=true", "-P", "node.passive=true", "-"};
        const QStringList args = streams::targetArguments(monitor, 544);
        QVERIFY(!args.contains("stream.capture.sink=true"));
        QVERIFY(args.contains("node.passive=true"));
        QCOMPARE(args.at(args.indexOf("--target") + 1), QString("544"));
        QVERIFY(args.contains("node.dont-reconnect=true"));
        QVERIFY(args.contains("node.dont-fallback=true"));
        QCOMPARE(args.last(), QString("-"));
        QCOMPARE(streams::targetArguments({}, 7), QStringList({"--target", "7", "-P", "node.dont-reconnect=true",
                                                               "-P", "node.dont-fallback=true"}));
    }

    void recognizesVanishedTarget() {
        QVERIFY(streams::isTargetGone("remote error: id=2 seq:45 res:-2 (No such file or directory): defined target not found"));
        QVERIFY(!streams::isTargetGone("stream node 154 error: something else"));
    }
};

int runStreamsTests(int argc, char *argv[]) {
    StreamsTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "streams_tests.moc"
