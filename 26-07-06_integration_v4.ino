#include <DHT11.h>
#include <SPI.h>
#include <SD.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// --- Pin assignments ---
const int pin_ir_receiver_1 = D6;
const int pin_ir_receiver_2 = D8;
const int pin_temp_humidity = D3;
const int pin_sd_cs = 4;

// Per-beam causal debounce; validated safe range 15-20ms (real bounce found at
// 1-49ms, genuine pulses never shorter than ~38ms once debounced).
const unsigned long DEBOUNCE_MS = 15;

const char *SNAPSHOT_PATH = "/COUNTS.CSV";

// --- BLE ---
#define SERVICE_UUID        "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcd1234-1234-1234-1234-1234567890ab"

BLECharacteristic *pChar;
bool ble_connected = false;
// Set on the BLE stack's own task in onConnect(); read/cleared from loop() so
// the first notify to a newly connected client is sent from the main loop,
// not synchronously inside the connect callback -- a client typically hasn't
// written the notify-subscribe (CCCD) descriptor yet at the exact moment
// onConnect fires, so a notify() sent right there is commonly dropped.
volatile bool ble_just_connected = false;

class ServerCB : public BLEServerCallbacks {
  void onConnect(BLEServer *s) {
    ble_connected = true;
    ble_just_connected = true;
  }
  void onDisconnect(BLEServer *s) {
    ble_connected = false;
    s->startAdvertising();
  }
};

float temperature = 999.99;
int humidity = 0;
int temperature_humidity_readout_interval_minutes = 15;
unsigned long int temperature_humidity_readout_millis = 0;

DHT11 dht11(pin_temp_humidity);

unsigned long int prMillis = 0;


// Debounces a single digital input: a new raw value is only accepted as the
// real `debounced` state once it has been stable for DEBOUNCE_MS.
struct DebouncedInput {
  bool raw_last;
  bool debounced;
  unsigned long last_change_millis;

  void init(bool initial) {
    raw_last = debounced = initial;
    last_change_millis = millis();
  }

  void update(bool raw) {
    unsigned long now = millis();
    if (raw != raw_last) {
      raw_last = raw;
      last_change_millis = now;
    }
    if (raw != debounced && (now - last_change_millis) >= DEBOUNCE_MS) {
      debounced = raw;
    }
  }
};

DebouncedInput barrier1;  // debounced == true means "closed", i.e. beam intact
DebouncedInput barrier2;

enum CounterState { IDLE, ARMED_1, ARMED_2, CLEARING };
CounterState counter_state = IDLE;
bool prev_b1_closed = true;
bool prev_b2_closed = true;

long count_in = 0;       // barrier1-first ("1-first")
long count_out = 0;      // barrier2-first ("2-first")
long count_unknown = 0;  // both beams broke in the same debounced sample
int last_direction = 0;  // 0=none, +1=IN, -1=OUT, 2=unknown (Serial diagnostics only)


void write_sd_snapshot() {
  SD.remove(SNAPSHOT_PATH);
  File dataFile = SD.open(SNAPSHOT_PATH, FILE_WRITE);
  if (dataFile) {
    dataFile.println("count_in,count_out,count_unknown,net,temperature,humidity,uptime_millis");
    dataFile.print(count_in);
    dataFile.print(",");
    dataFile.print(count_out);
    dataFile.print(",");
    dataFile.print(count_unknown);
    dataFile.print(",");
    dataFile.print(count_in - count_out);
    dataFile.print(",");
    dataFile.print(temperature);
    dataFile.print(",");
    dataFile.print(humidity);
    dataFile.print(",");
    dataFile.print(millis());
    dataFile.print("\n");
    dataFile.close();
  } else {
    Serial.println("Error while opening snapshot file for write.");
  }
}


bool read_sd_snapshot() {
  File dataFile = SD.open(SNAPSHOT_PATH, FILE_READ);
  if (!dataFile) {
    Serial.println("No snapshot file found -- starting counts at 0.");
    return false;
  }
  dataFile.readStringUntil('\n');  // discard header
  String row = dataFile.readStringUntil('\n');
  dataFile.close();
  row.trim();

  int p1 = row.indexOf(',');
  int p2 = row.indexOf(',', p1 + 1);
  int p3 = row.indexOf(',', p2 + 1);  // net column boundary; value recomputed, not trusted
  int p4 = row.indexOf(',', p3 + 1);
  int p5 = row.indexOf(',', p4 + 1);
  if (p1 < 0 || p2 < 0 || p3 < 0 || p4 < 0 || p5 < 0) {
    Serial.println("Snapshot file malformed -- starting counts at 0.");
    return false;
  }

  count_in = row.substring(0, p1).toInt();
  count_out = row.substring(p1 + 1, p2).toInt();
  count_unknown = row.substring(p2 + 1, p3).toInt();
  temperature = row.substring(p4 + 1, p5).toFloat();
  humidity = row.substring(p5 + 1).toInt();

  Serial.print("Restored from snapshot: count_in=");
  Serial.print(count_in);
  Serial.print(" count_out=");
  Serial.print(count_out);
  Serial.print(" count_unknown=");
  Serial.println(count_unknown);
  return true;
}


// Always refreshes the characteristic's stored value (cheap, no BLE traffic),
// so a client that does a plain GATT read right after connecting -- instead
// of or before subscribing to notifications -- still gets current data rather
// than whatever was last notified (or nothing, on a fresh boot with no
// bat/DHT events yet).
void update_ble_value() {
  char json[64];
  snprintf(json, sizeof(json), "{\"bats\":%d,\"temp\":%.1f,\"hum\":%d}",
           (int)(count_in - count_out), temperature, humidity);
  pChar->setValue(json);
}

void notify_ble() {
  update_ble_value();
  if (ble_connected) {
    pChar->notify();
  }
}


void count_ir_barriers_logic() {
  barrier1.update(digitalRead(pin_ir_receiver_1) == 1);
  barrier2.update(digitalRead(pin_ir_receiver_2) == 1);

  bool cur_b1 = barrier1.debounced;
  bool cur_b2 = barrier2.debounced;

  bool b1_broke = prev_b1_closed && !cur_b1;
  bool b1_released = !prev_b1_closed && cur_b1;
  bool b2_broke = prev_b2_closed && !cur_b2;
  bool b2_released = !prev_b2_closed && cur_b2;

  switch (counter_state) {
    case IDLE:
      if (b1_broke && b2_broke) {
        count_unknown++;
        last_direction = 2;
        write_sd_snapshot();
        notify_ble();
        counter_state = CLEARING;
      } else if (b1_broke) {
        counter_state = ARMED_1;
      } else if (b2_broke) {
        counter_state = ARMED_2;
      }
      break;

    case ARMED_1:  // completion checked before abort
      if (b2_broke) {
        count_in++;
        last_direction = 1;
        write_sd_snapshot();
        notify_ble();
        counter_state = CLEARING;
      } else if (b1_released) {
        counter_state = IDLE;  // abort, no count
      }
      break;

    case ARMED_2:
      if (b1_broke) {
        count_out++;
        last_direction = -1;
        write_sd_snapshot();
        notify_ble();
        counter_state = CLEARING;
      } else if (b2_released) {
        counter_state = IDLE;
      }
      break;

    case CLEARING:  // no settle timer -- proven sufficient on real data
      if (cur_b1 && cur_b2) {
        counter_state = IDLE;
      }
      break;
  }

  prev_b1_closed = cur_b1;
  prev_b2_closed = cur_b2;
}


void read_temperature_humidity_sensor() {
  int current_temperature = 0;
  int current_humidity = 0;
  int result = dht11.readTemperatureHumidity(current_temperature, current_humidity);

  if (result == 0) {
    temperature = current_temperature;
    humidity = current_humidity;
  } else {
    Serial.println(DHT11::getErrorString(result));
  }
}


void setup() {
  Serial.begin(115200);

  delay(2000);

  pinMode(pin_ir_receiver_1, INPUT_PULLUP);
  pinMode(pin_ir_receiver_2, INPUT_PULLUP);

  if (!SD.begin(pin_sd_cs)) {
    Serial.println("SD board initialization failed! Counts will not persist across reboots.");
  } else {
    read_sd_snapshot();  // auto-resume; falls back to zeros internally on missing/malformed file
  }

  dht11.setDelay(1000);
  read_temperature_humidity_sensor();
  temperature_humidity_readout_millis = millis();

  barrier1.init(digitalRead(pin_ir_receiver_1) == 1);
  barrier2.init(digitalRead(pin_ir_receiver_2) == 1);
  prev_b1_closed = barrier1.debounced;
  prev_b2_closed = barrier2.debounced;

  prMillis = millis();

  BLEDevice::init("BatBox-Planten-un-Blomen");

  BLEServer *server = BLEDevice::createServer();
  server->setCallbacks(new ServerCB());

  BLEService *service = server->createService(SERVICE_UUID);
  pChar = service->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
  pChar->addDescriptor(new BLE2902());  // required for notifications
  service->start();

  BLEAdvertising *adv = server->getAdvertising();
  adv->addServiceUUID(SERVICE_UUID);
  // Longer advertising interval saves power (values are in units of 0.625ms)
  adv->setMinInterval(0x100);  // ~160ms
  adv->setMaxInterval(0x200);  // ~320ms
  adv->start();

  update_ble_value();  // prime the characteristic before any client ever connects
}


void loop() {
  count_ir_barriers_logic();  // unthrottled, no delay()

  // A loop() that never yields at all can starve the ESP32 idle task, which
  // trips the task watchdog and reboots the chip -- looking like a random BLE
  // disconnect. A 1ms yield every ~20ms costs ~5% of the polling budget at
  // most (still comfortably above the validated ~610-915Hz floor) and is
  // rate-limited so it doesn't throttle every single iteration.
  static unsigned long lastYieldMillis = 0;
  if (millis() - lastYieldMillis >= 20) {
    lastYieldMillis = millis();
    delay(1);
  }

  if (ble_just_connected) {
    ble_just_connected = false;
    notify_ble();  // catch the client up immediately, don't wait for the next bat pass or DHT tick
  }

  if (millis() - temperature_humidity_readout_millis >= temperature_humidity_readout_interval_minutes * 60 * 1000) {
    temperature_humidity_readout_millis = millis();
    read_temperature_humidity_sensor();
    write_sd_snapshot();
    notify_ble();
  }

  // Arduino Nano ESP32 uses native USB CDC for Serial, not a separate USB-
  // serial chip: if nothing is actively reading it (Serial Monitor closed, or
  // running standalone on battery), Serial.print() can block once its
  // internal buffer fills, stalling loop() indefinitely. `if (Serial)` is
  // true only while a host actually has the CDC port open.
  if (Serial && millis() - prMillis >= 400) {
    prMillis = millis();

    Serial.print(millis());
    Serial.print("ms B1:");
    Serial.print(barrier1.debounced ? "C" : "O");
    Serial.print(" B2:");
    Serial.print(barrier2.debounced ? "C" : "O");
    Serial.print(" state=");
    Serial.print(counter_state);
    Serial.print(" IN=");
    Serial.print(count_in);
    Serial.print(" OUT=");
    Serial.print(count_out);
    Serial.print(" UNK=");
    Serial.print(count_unknown);
    Serial.print(" NET=");
    Serial.print(count_in - count_out);
    Serial.print(" last=");
    Serial.print(last_direction == 1 ? "IN" : last_direction == -1 ? "OUT" : last_direction == 2 ? "UNK" : "-");
    Serial.print(" T=");
    Serial.print(temperature);
    Serial.print("C H=");
    Serial.print(humidity);
    Serial.print("% BLE=");
    Serial.println(ble_connected ? "connected" : "-");
  }
}
