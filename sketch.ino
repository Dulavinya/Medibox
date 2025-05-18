#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>
#include <math.h>

// Function prototypes
void callback(char* topic, byte* payload, unsigned int length);
void reconnect();
void test_servo();

// WiFi credentials (Wokwi)
WiFiClient espClient;
PubSubClient client(espClient);

// Define OLED parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

#define BUZZER 5
#define LED_1 15
#define PB_CANCEL 34
#define PB_OK 32
#define PB_UP 33
#define PB_DOWN 35
#define DHTPIN 12
#define LDR_PIN 39

#define NTP_SERVER     "pool.ntp.org"
#define UTC_OFFSET     19800
#define UTC_OFFSET_DST 0

// Health threshold constants
#define MIN_HEALTHY_TEMP 24.0
#define MAX_HEALTHY_TEMP 32.0
#define MIN_HEALTHY_HUMIDITY 65.0
#define MAX_HEALTHY_HUMIDITY 80.0

// Servo
Servo shaded_servo;
#define SERVO_PIN 13

float theta_offset = 30.0;
float gamma_val = 0.75;
float Tmed = 30.0;

// Snooze parameters
#define SNOOZE_DURATION 5
#define TEMP_CHECK_INTERVAL 10000

// MQTT
#define MQTT_SERVER "broker.hivemq.com"
#define PUB_TOPIC "medibox1_380J/intensity"
#define SUB_TOPIC_SAMPLING "medibox1_380J/intensitySampling"
#define SUB_TOPIC_SENDING "medibox1_380J/intensitySending"
#define SUB_TOPIC_OFFSET "medibox1_380J/offset"
#define SUB_TOPIC_GAMMA "medibox1_380J/controlfactor"
#define SUB_TOPIC_TMED "medibox1_380J/tempset"

// Declare Objects
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;

// Global variables
int days = 0;
int hours = 0;
int minutes = 0;
int seconds = 0;

unsigned long timeNow = 0;
unsigned long timeLast = 0;
unsigned long lastTempCheck = 0;
unsigned long lastTimeUpdate = 0;

bool alarm_enabled = true;
int n_alarms = 2;
int alarm_hours[] = {0, 1};
int alarm_minutes[] = {1, 10};
bool alarm_triggered[] = {false, false};
bool alarm_active[] = {true, true};

bool snooze_active[] = {false, false};
int snooze_end_hour[] = {0, 0};
int snooze_end_minute[] = {0, 0};

int time_zone_offset = 19800;

// LDR
int ts = 5; // Sampling interval in seconds
int tu = 120; // Sending interval in seconds
const int maxSamples = 24; // Based on 120s / 5s
float ldr_values[maxSamples];
int ldr_index = 0;
int ldr_sample_count = 0;
unsigned long lastLDRSample = 0;
unsigned long lastLDRSend = 0;

int n_notes = 8;
int C = 262;
int D = 294;
int E = 330;
int F = 349;
int G = 392;
int A = 440;
int B = 494;
int C_H = 523;
int notes[] = {C, D, E, F, G, A, B, C_H};

int current_mode = 0;
int max_modes = 6; // Includes test mode
String modes[] = {"1- Set Time Zone", "2- Set Alarm 1", "3- Set Alarm 2", "4- View Alarms", "5- Delete Alarm", "6- Test Servo"};

bool display_warning = false;
unsigned long warning_end_time = 0;
String temp_status = "";
String humidity_status = "";
float current_temp = 0;
float current_humidity = 0;

void setup() {
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(PB_CANCEL, INPUT);
  pinMode(PB_OK, INPUT);
  pinMode(PB_UP, INPUT);
  pinMode(PB_DOWN, INPUT);
  pinMode(LDR_PIN, INPUT);
  

  Serial.begin(9600);
  Serial.println("Initializing Medibox...");

  dhtSensor.setup(DHTPIN, DHTesp::DHT11);
  Serial.println("DHT11 sensor initialized on pin " + String(DHTPIN));

  // Servo setup with debug
  Serial.println("Attaching servo to pin " + String(SERVO_PIN));
  shaded_servo.attach(SERVO_PIN, 500, 2500);
  if (shaded_servo.attached()) {
    Serial.println("Servo successfully attached to pin " + String(SERVO_PIN));
  } else {
    Serial.println("ERROR: Servo attachment failed on pin " + String(SERVO_PIN));
  }

  // Test servo
  Serial.println("Testing servo...");
  test_servo();
  Serial.println("Servo test complete");

  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;);
  }
  Serial.println("OLED initialized");
  display.display();
  delay(500);

  display.clearDisplay();
  print_line("Connecting to WIFI", 0, 0, 2);
  
  WiFi.begin("Wokwi-GUEST", "", 6);
  Serial.println("Connecting to WiFi (Wokwi-GUEST)...");
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 10000) {
    delay(250);
    display.print(".");
    display.display();
    Serial.println("WiFi connecting...");
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    for (;;);
  }
  Serial.println("WiFi connected successfully");
  delay(1000);

  display.clearDisplay();
  print_line("Connected to WIFI", 0, 0, 2);

  // Configure NTP
  Serial.println("Configuring NTP server: " + String(NTP_SERVER));
  configTime(time_zone_offset, UTC_OFFSET_DST, NTP_SERVER);

  // Wait for NTP sync
  bool timeSet = false;
  wifiStart = millis();
  while (millis() - wifiStart < 5000) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
      char timeHour[3];
      strftime(timeHour, 3, "%H", &timeinfo);
      hours = atoi(timeHour);
      char timeMinute[3];
      strftime(timeMinute, 3, "%M", &timeinfo);
      minutes = atoi(timeMinute);
      char timeSecond[3];
      strftime(timeSecond, 3, "%S", &timeinfo);
      seconds = atoi(timeSecond);
      char timeDay[3];
      strftime(timeDay, 3, "%d", &timeinfo);
      days = atoi(timeDay);
      timeSet = true;
      Serial.println("NTP time set successfully: " + String(hours) + ":" + String(minutes) + ":" + String(seconds));
      break;
    }
    delay(500);
    Serial.println("Waiting for NTP sync...");
  }
  if (!timeSet) {
    Serial.println("Failed to sync NTP time, proceeding");
  }

  display.clearDisplay();
  print_line("Welcome to Medibox", 10, 20, 2);
  delay(1000);
  display.clearDisplay();
  
  lastTimeUpdate = millis();

  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);
  Serial.println("MQTT server configured: " + String(MQTT_SERVER) + ", port 1883");
  reconnect();
}

void print_line(String text, int column, int row, int text_size) {
  display.setTextSize(text_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.println(text);
  display.display();
}

void print_time_now() {
  display.clearDisplay();
  
  String hourStr = (hours < 10) ? "0" + String(hours) : String(hours);
  String minStr = (minutes < 10) ? "0" + String(minutes) : String(minutes);
  String secStr = (seconds < 10) ? "0" + String(seconds) : String(seconds);
  
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 16);
  display.print(hourStr + ":" + minStr + ":" + secStr);
  
  display.setTextSize(1);
  display.setCursor(40, 0);
  display.print("Day: " + String(days));
  
  display.display();
}

void update_time() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  static unsigned long lastNtpSync = 0;
  if (millis() - lastNtpSync > 3600000) {
    configTime(time_zone_offset, UTC_OFFSET_DST, NTP_SERVER);
    lastNtpSync = millis();
    Serial.println("NTP synced");
  }

  char timeHour[3];
  strftime(timeHour, 3, "%H", &timeinfo);
  hours = atoi(timeHour);

  char timeMinute[3];
  strftime(timeMinute, 3, "%M", &timeinfo);
  minutes = atoi(timeMinute);

  char timeSecond[3];
  strftime(timeSecond, 3, "%S", &timeinfo);
  seconds = atoi(timeSecond);

  char timeDay[3];
  strftime(timeDay, 3, "%d", &timeinfo);
  days = atoi(timeDay);
}

void calculate_snooze_end_time(int alarm_index) {
  snooze_end_hour[alarm_index] = hours;
  snooze_end_minute[alarm_index] = minutes + SNOOZE_DURATION;
  
  if (snooze_end_minute[alarm_index] >= 60) {
    snooze_end_hour[alarm_index] = (snooze_end_hour[alarm_index] + 1) % 24;
    snooze_end_minute[alarm_index] = snooze_end_minute[alarm_index] - 60;
  }
}

void ring_alarm() {
  display.clearDisplay();
  print_line("MEDICINE TIME!", 0, 0, 2);
  print_line("Cancel: Stop", 0, 30, 1);
  print_line("OK: Snooze 5m", 0, 45, 1);

  digitalWrite(LED_1, HIGH);

  bool alarm_stopped = false;
  bool snooze_pressed = false;
  int triggered_alarm = -1;

  for (int i = 0; i < n_alarms; i++) {
    if (alarm_active[i] && alarm_triggered[i]) {
      triggered_alarm = i;
      break;
    }
  }

  while (!alarm_stopped) {
    for (int i = 0; i < n_notes && !alarm_stopped; i++) { 
      if (digitalRead(PB_CANCEL) == LOW) {
        delay(200);
        alarm_stopped = true;
        if (triggered_alarm >= 0) {
          alarm_triggered[triggered_alarm] = false;
          snooze_active[triggered_alarm] = false;
        }
        break;
      }

      if (digitalRead(PB_OK) == LOW && triggered_alarm >= 0) {
        delay(200);
        snooze_pressed = true;
        alarm_stopped = true;
        break;
      }

      tone(BUZZER, notes[i]);
      delay(500);
      noTone(BUZZER);
      delay(2);
    }
  }

  digitalWrite(LED_1, LOW);
  display.clearDisplay();

  if (snooze_pressed && triggered_alarm >= 0) {
    snooze_active[triggered_alarm] = true;
    alarm_triggered[triggered_alarm] = false;
    calculate_snooze_end_time(triggered_alarm);

    display.clearDisplay();
    print_line("Snoozed for 5 min", 0, 0, 2);
    delay(1000);
  }
}

void check_alarms() {
  if (alarm_enabled) {
    for (int i = 0; i < n_alarms; i++) {
      if (alarm_active[i] && !alarm_triggered[i] && 
          alarm_hours[i] == hours && alarm_minutes[i] == minutes && seconds == 0) {
        alarm_triggered[i] = true;
        ring_alarm();
      }
    }

    for (int i = 0; i < n_alarms; i++) {
      if (snooze_active[i] && hours == snooze_end_hour[i] && 
          minutes == snooze_end_minute[i] && seconds == 0) {
        snooze_active[i] = false;
        alarm_triggered[i] = false;
        ring_alarm();
      }
    }
  }

  if (hours == 0 && minutes == 0 && seconds == 0) {
    for (int i = 0; i < n_alarms; i++) {
      alarm_triggered[i] = false;
      snooze_active[i] = false;
    }
  }
}

void update_time_with_check_alarms() {
  update_time();
  check_alarms();
}

void display_temp_warning() {
  display.clearDisplay();
  
  String timeStr = (hours < 10 ? "0" : "") + String(hours) + ":" + 
                  (minutes < 10 ? "0" : "") + String(minutes);
  print_line(timeStr, 0, 0, 1);
  
  print_line("Temp: " + String(current_temp, 1) + "C", 0, 15, 1);
  if (temp_status != "") {
    print_line(temp_status, 0, 25, 1);
  }
  
  print_line("Humidity: " + String(current_humidity, 1) + "%", 0, 35, 1);
  if (humidity_status != "") {
    print_line(humidity_status, 0, 45, 1);
  }
  
  display.display();
}

int wait_for_button_press() {
  unsigned long last_time_update = millis();
  
  while (true) {
    unsigned long current_millis = millis();
    if (current_millis - last_time_update >= 1000) {
      update_time();
      last_time_update = current_millis;
    }
    
    if (digitalRead(PB_UP) == LOW) {
      delay(200);
      return PB_UP;
    }
    else if (digitalRead(PB_DOWN) == LOW) {
      delay(200);
      return PB_DOWN;
    }
    else if (digitalRead(PB_OK) == LOW) {
      delay(200);
      return PB_OK;
    }
    else if (digitalRead(PB_CANCEL) == LOW) {
      delay(200);
      return PB_CANCEL;
    }
  }
}

void go_to_menu() {
  unsigned long last_time_update = millis();
  
  while (digitalRead(PB_CANCEL) == HIGH) {
    unsigned long current_millis = millis();
    if (current_millis - last_time_update >= 1000) {
      update_time();
      last_time_update = current_millis;
    }
    
    display.clearDisplay();
    print_line(modes[current_mode], 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      delay(200);
      current_mode += 1;
      current_mode = current_mode % max_modes;
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      current_mode -= 1;
      if (current_mode < 0) {
        current_mode = max_modes - 1;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
      run_mode(current_mode);
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      break;
    }
  }
}

void set_alarm(int alarm) {
  int temp_hour = alarm_hours[alarm];
  int temp_minute = alarm_minutes[alarm];
  
  bool confirmed = false;

  while (true) {
    display.clearDisplay();
    print_line("Enter hour: " + String(temp_hour), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      temp_hour = (temp_hour + 1) % 24;
    }
    else if (pressed == PB_DOWN) {
      temp_hour = (temp_hour - 1 + 24) % 24;
    }
    else if (pressed == PB_OK) {
      break;
    }
    else if (pressed == PB_CANCEL) {
      return;
    }
  }

  while (true) {
    display.clearDisplay();
    print_line("Enter minute: " + String(temp_minute), 0, 0, 2);

    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      temp_minute = (temp_minute + 1) % 60;
    }
    else if (pressed == PB_DOWN) {
      temp_minute = (temp_minute - 1 + 60) % 60;
    }
    else if (pressed == PB_OK) {
      confirmed = true;
      break;
    }
    else if (pressed == PB_CANCEL) {
      return;
    }
  }

  if (confirmed) {
    alarm_hours[alarm] = temp_hour;
    alarm_minutes[alarm] = temp_minute;
    display.clearDisplay();
    print_line("Alarm Set!", 0, 0, 2);
    delay(1000);
  }
}

void view_alarms() {
  display.clearDisplay();
  print_line("Active Alarms:", 0, 0, 1);
  
  int y_pos = 15;
  bool no_alarms = true;
  
  for (int i = 0; i < n_alarms; i++) {
    if (alarm_active[i]) {
      no_alarms = false;
      String alarm_str = "A" + String(i+1) + ": ";
      
      if (alarm_hours[i] < 10) {
        alarm_str += "0";
      }
      alarm_str += String(alarm_hours[i]) + ":";
      
      if (alarm_minutes[i] < 10) {
        alarm_str += "0";
      }
      alarm_str += String(alarm_minutes[i]);
      
      if (snooze_active[i]) {
        alarm_str += " (Snoozed)";
      }
      
      print_line(alarm_str, 0, y_pos, 1);
      y_pos += 15;
    }
  }
  
  if (no_alarms) {
    print_line("No active alarms", 0, y_pos, 1);
  }
  
  while (digitalRead(PB_CANCEL) == HIGH) {
    if (digitalRead(PB_CANCEL) == LOW) {
      delay(200);
      break;
    }
    update_time();
  }
}

void delete_alarm() {
  int selected_alarm = 0;
  
  while (true) {
    display.clearDisplay();
    
    String alarm_info = "Alarm " + String(selected_alarm + 1) + ": ";
    if (alarm_active[selected_alarm]) {
      if (alarm_hours[selected_alarm] < 10) {
        alarm_info += "0";
      }
      alarm_info += String(alarm_hours[selected_alarm]) + ":";
      
      if (alarm_minutes[selected_alarm] < 10) {
        alarm_info += "0";
      }
      alarm_info += String(alarm_minutes[selected_alarm]);
    } else {
      alarm_info += "Inactive";
    }
    
    print_line(alarm_info, 0, 0, 1);
    print_line("UP/DOWN to select", 0, 20, 1);
    print_line("OK to delete", 0, 35, 1);
    print_line("CANCEL to exit", 0, 50, 1);
    
    int pressed = wait_for_button_press();
    if (pressed == PB_UP || pressed == PB_DOWN) {
      delay(200);
      selected_alarm = (selected_alarm + 1) % n_alarms;
    }
    else if (pressed == PB_OK) {
      delay(200);
      
      display.clearDisplay();
      print_line("Delete Alarm " + String(selected_alarm + 1) + "?", 0, 0, 1);
      print_line("OK to confirm", 0, 20, 1);
      print_line("CANCEL to abort", 0, 35, 1);
      
      int confirm = wait_for_button_press();
      if (confirm == PB_OK) {
        alarm_active[selected_alarm] = false;
        snooze_active[selected_alarm] = false;
        
        display.clearDisplay();
        print_line("Alarm " + String(selected_alarm + 1) + " deleted", 0, 0, 1);
        delay(1000);
        break;
      }
      else if (confirm == PB_CANCEL) {
        delay(200);
      }
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      break;
    }
  }
}

void set_time_zone() {
  int temp_offset_hours = time_zone_offset / 3600;
  
  display.clearDisplay();
  print_line("Set UTC offset", 0, 0, 1);
  delay(1000);
  
  while (true) {
    display.clearDisplay();
    print_line("Hours: " + String(temp_offset_hours), 0, 0, 2);
    print_line("Range: -12 to +14", 0, 20, 1);
    
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      delay(200);
      temp_offset_hours += 1;
      if (temp_offset_hours > 14) {
        temp_offset_hours = -12;
      }
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      temp_offset_hours -= 1;
      if (temp_offset_hours < -12) {
        temp_offset_hours = 14;
      }
    }
    else if (pressed == PB_OK) {
      delay(200);
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      return;
    }
  }
  
  int temp_offset_minutes = (time_zone_offset % 3600) / 60;
  
  while (true) {
    display.clearDisplay();
    print_line("Minutes: " + String(temp_offset_minutes), 0, 0, 2);
    print_line("(0, 15, 30, 45)", 0, 20, 1);
    
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      delay(200);
      if (temp_offset_minutes == 0) temp_offset_minutes = 15;
      else if (temp_offset_minutes == 15) temp_offset_minutes = 30;
      else if (temp_offset_minutes == 30) temp_offset_minutes = 45;
      else temp_offset_minutes = 0;
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      if (temp_offset_minutes == 0) temp_offset_minutes = 45;
      else if (temp_offset_minutes == 45) temp_offset_minutes = 30;
      else if (temp_offset_minutes == 30) temp_offset_minutes = 15;
      else temp_offset_minutes = 0;
    }
    else if (pressed == PB_OK) {
      delay(200);
      time_zone_offset = (temp_offset_hours * 3600) + (temp_offset_minutes * 60);
      
      configTime(time_zone_offset, UTC_OFFSET_DST, NTP_SERVER);
      
      display.clearDisplay();
      print_line("Time zone set", 0, 0, 2);
      delay(1000);
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      return;
    }
  }
}

void test_servo() {
  if (!shaded_servo.attached()) {
    Serial.println("ERROR: Servo not attached during test");
    return;
  }
  Serial.println("Moving servo to 0 degrees...");
  shaded_servo.write(0);
  delay(1000);
  Serial.println("Moving servo to 90 degrees...");
  shaded_servo.write(90);
  delay(1000);
  Serial.println("Moving servo to 180 degrees...");
  shaded_servo.write(180);
  delay(1000);
  Serial.println("Moving servo back to 0 degrees...");
  shaded_servo.write(0);
}

void run_mode(int mode) {
  if (mode == 0) {
    set_time_zone();
  }
  else if (mode == 1 || mode == 2) {
    set_alarm(mode - 1);
  }
  else if (mode == 3) {
    view_alarms();
  }
  else if (mode == 4) {
    delete_alarm();
  }
  else if (mode == 5) {
    display.clearDisplay();
    print_line("Testing Servo...", 0, 0, 2);
    test_servo();
    display.clearDisplay();
    print_line("Servo Test Done", 0, 0, 2);
    delay(1000);
  }
}

void check_temp() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  current_temp = data.temperature;
  current_humidity = data.humidity;
  Serial.println("Temperature: " + String(current_temp, 1) + "C, Humidity: " + String(current_humidity, 1) + "%");
  
  temp_status = "";
  humidity_status = "";
  
  bool has_warning = false;
  
  if (data.temperature > MAX_HEALTHY_TEMP) {
    temp_status = "TEMP HIGH";
    has_warning = true;
  } 
  else if (data.temperature < MIN_HEALTHY_TEMP) {
    temp_status = "TEMP LOW";
    has_warning = true;
  }
  
  if (data.humidity > MAX_HEALTHY_HUMIDITY) {
    humidity_status = "HUMIDITY HIGH";
    has_warning = true;
  } 
  else if (data.humidity < MIN_HEALTHY_HUMIDITY) {
    humidity_status = "HUMIDITY LOW";
    has_warning = true;
  }
  
  if (has_warning) {
    display_warning = true;
    warning_end_time = millis() + 3000;
    
    digitalWrite(LED_1, HIGH);
    
    for (int i = 0; i < 3; i++) {
      tone(BUZZER, notes[7]);
      delay(100);
      noTone(BUZZER);
      delay(100);
    }
    
    digitalWrite(LED_1, LOW);
  }

  // Update servo with new temperature
  float sum = 0;
  for (int i = 0; i < ldr_sample_count; i++) sum += ldr_values[i];
  float avg = (ldr_sample_count > 0) ? sum / ldr_sample_count : 0;
  update_servo_angle(avg, current_temp);
}

float read_ldr() {
  Serial.println("Reading LDR...");
  int raw = analogRead(LDR_PIN);
  Serial.println("LDR raw: " + String(raw));
  if (raw < 0 || raw > 4095) {
    Serial.println("Invalid LDR reading: raw=" + String(raw));
    return 0.0;
  }
  float lux = 100000.0 * pow(10, -4.0 * (float)raw / 4095.0);
  lux = constrain(lux, 0.1, 100000.0);
  float intensity = log10(lux / 0.1) / log10(100000.0 / 0.1);
  intensity = constrain(intensity, 0.0, 1.0);
  Serial.println("LDR reading: raw=" + String(raw) + ", lux=" + String(lux, 1) + ", intensity=" + String(intensity, 3));
  return intensity;
}

void reconnect() {
  unsigned long startAttempt = millis();
  int maxAttempts = 5;
  int attempt = 0;
  while (!client.connected() && attempt < maxAttempts && millis() - startAttempt < 10000) {
    Serial.println("Attempting MQTT connection to " + String(MQTT_SERVER) + " (Attempt " + String(attempt + 1) + ")");
    if (client.connect("medibox_esp32")) {
      Serial.println("Connected to MQTT successfully");
      client.subscribe(SUB_TOPIC_SAMPLING);
      client.subscribe(SUB_TOPIC_SENDING);
      client.subscribe(SUB_TOPIC_OFFSET);
      client.subscribe(SUB_TOPIC_GAMMA);
      client.subscribe(SUB_TOPIC_TMED);
      Serial.println("Subscribed to: " + String(SUB_TOPIC_SAMPLING) + ", " + String(SUB_TOPIC_SENDING) + ", " + String(SUB_TOPIC_OFFSET) + ", " + String(SUB_TOPIC_GAMMA) + ", " + String(SUB_TOPIC_TMED));
      break;
    }
    else {
      Serial.println("Failed to connect to MQTT, state: " + String(client.state()));
      delay(1000);
      attempt++;
    }
  }
  if (!client.connected()) {
    Serial.println("MQTT connection timed out after " + String(attempt) + " attempts");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String topicStr = String(topic);
  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();  // Remove any trailing whitespace
  Serial.println("Received on " + topicStr + ": [" + msg + "]");

  float value = atof(msg.c_str());

  // Update parameters based on topic
  if (topicStr == SUB_TOPIC_SAMPLING && value >= 1 && value <= 60) {
    ts = (int)value;
    Serial.println("Updated ts: " + String(ts));
  }
  else if (topicStr == SUB_TOPIC_SENDING && value >= 10 && value <= 600) {
    tu = (int)value;
    Serial.println("Updated tu: " + String(tu));
  }
  else if (topicStr == SUB_TOPIC_OFFSET && value >= 0 && value <= 120) {
    theta_offset = value;
    Serial.println("Updated theta_offset: " + String(theta_offset));
  }
  else if (topicStr == SUB_TOPIC_GAMMA && value >= 0 && value <= 1) {
    gamma_val = value;
    Serial.println("Updated gamma_val: " + String(gamma_val));
  }
  else if (topicStr == SUB_TOPIC_TMED && value >= 10 && value <= 40) {
    Tmed = value;
    Serial.println("Updated Tmed: " + String(Tmed));
  }

  // Only update servo and publish LDR average if there are valid samples
  if (ldr_sample_count > 0) {
    float sum = 0;
    for (int i = 0; i < ldr_sample_count; i++) sum += ldr_values[i];
    float avg = sum / ldr_sample_count;  // No need to check ldr_sample_count again
    update_servo_angle(avg, current_temp);
    publish_ldr_average(avg);
  } else {
    Serial.println("Skipping servo update and LDR publish: No valid LDR samples (ldr_sample_count=0)");
  }
}


void publish_ldr_average(float avg) {
  char msg[10];
  dtostrf(avg, 1, 2, msg);
  if (client.connected()) {
    client.publish(PUB_TOPIC, msg);
    Serial.println("Published LDR average to " + String(PUB_TOPIC) + ": " + String(msg));
  }
  else {
    Serial.println("Failed to publish LDR average: MQTT not connected");
  }
}

void update_servo_angle(float intensity, float temp) {
  if (!shaded_servo.attached()) {
    Serial.println("ERROR: Servo not attached in update_servo_angle");
    return;
  }

  float ratio = (float)ts / tu;
  float ln_term = (ratio > 0 && ratio != 1) ? fabs(log(ratio)) : 0.001;
  float temp_ratio = (temp > 0 && Tmed > 0) ? temp / Tmed : 1.0;
  float angle = theta_offset + (180 - theta_offset) * intensity * gamma_val * ln_term * temp_ratio;
  angle = constrain(angle, 0, 180);

  Serial.println("Servo updated => θ=" + String(angle, 2) +
                 " | I=" + String(intensity, 3) +
                 " | γ=" + String(gamma_val, 2) +
                 " | ln(ts/tu)=" + String(ln_term, 3) +
                 " | T=" + String(temp, 1) +
                 " | Tmed=" + String(Tmed));

  shaded_servo.write(angle);
  delay(15); // Allow servo to stabilize
}

void loop() {
  static unsigned long lastSecondTick = 0;
  unsigned long currentMillis = millis();
  
  if (currentMillis - lastSecondTick >= 1000) {
    lastSecondTick = currentMillis;
    update_time();
    check_alarms();
  }
  
  if (digitalRead(PB_OK) == LOW) {
    delay(200);
    go_to_menu();
  }
  
  if (currentMillis - lastTempCheck >= TEMP_CHECK_INTERVAL) {
    lastTempCheck = currentMillis;
    check_temp();
  }
  
  if (display_warning && currentMillis < warning_end_time) {
    display_temp_warning();
  }
  else {
    display_warning = false;
    print_time_now();
  }
  
  if (currentMillis - lastLDRSample >= ts * 1000) {
    lastLDRSample = currentMillis;
    float intensity = read_ldr();
    ldr_values[ldr_index] = intensity;
    ldr_index = (ldr_index + 1) % maxSamples;
    if (ldr_sample_count < maxSamples) ldr_sample_count++;
    Serial.println("LDR sample stored: index=" + String(ldr_index) + 
                   ", count=" + String(ldr_sample_count) + ", intensity=" + String(intensity, 3));

    // Update servo with current LDR average
    float sum = 0;
    for (int i = 0; i < ldr_sample_count; i++) sum += ldr_values[i];
    float avg = (ldr_sample_count > 0) ? sum / ldr_sample_count : 0;
    update_servo_angle(avg, current_temp);
  }

  if (currentMillis - lastLDRSend >= tu * 1000) {
    lastLDRSend = currentMillis;
    float sum = 0;
    for (int i = 0; i < ldr_sample_count; i++) sum += ldr_values[i];
    float avg = (ldr_sample_count > 0) ? sum / ldr_sample_count : 0;
    publish_ldr_average(avg);
    Serial.println("Periodic LDR average published: average_intensity=" + String(avg, 3));
    ldr_sample_count = 0;
    ldr_index = 0;
  }

  if (!client.connected()) {
    Serial.println("MQTT disconnected, attempting to reconnect...");
    reconnect();
  }
  client.loop();
}