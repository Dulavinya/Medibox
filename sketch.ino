#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>

// Define OLED parameters
#define SCREEN_WIDTH 128 //OLED display width, in pixels
#define SCREEN_HEIGHT 64 //OLED display height, in pixels
#define OLED_RESET -1 //Reset pin
#define SCREEN_ADDRESS 0x3C

#define BUZZER 5
#define LED_1 15
#define PB_CANCEL 34
#define PB_OK 32
#define PB_UP 33
#define PB_DOWN 35
#define DHTPIN 12

#define NTP_SERVER     "pool.ntp.org"
#define UTC_OFFSET     19800
#define UTC_OFFSET_DST 0

// Health threshold constants
#define MIN_HEALTHY_TEMP 24.0
#define MAX_HEALTHY_TEMP 32.0
#define MIN_HEALTHY_HUMIDITY 65.0
#define MAX_HEALTHY_HUMIDITY 80.0

// Snooze parameters
#define SNOOZE_DURATION 5 // Snooze for 5 minutes
#define TEMP_CHECK_INTERVAL 10000 // Check temp every 10 seconds

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
unsigned long lastTimeUpdate = 0; // Added for regular time updates

bool alarm_enabled = true;
int n_alarms = 2;
int alarm_hours[] = {0, 1};
int alarm_minutes[] = {1, 10};
bool alarm_triggered[] = {false, false};
bool alarm_active[] = {true, true}; // Track which alarms are active

// Snooze variables
bool snooze_active[] = {false, false};
int snooze_end_hour[] = {0, 0};
int snooze_end_minute[] = {0, 0};

int time_zone_offset = 19800; // Default UTC offset in seconds (5.5 hours)

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
int max_modes = 5; // Updated: reduced from 7 to 6 because we removed one menu option
String modes[] = {"1- Set Time Zone", "2- Set Alarm 1", "3- Set Alarm 2", "4- View Alarms", "5- Delete Alarm"}; // Removed "Disable Alarms" option

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

  dhtSensor.setup(DHTPIN, DHTesp::DHT22);

  Serial.begin(9600);
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Don't procedd, loop forever
  }

  //Show initial display buffer contents on the screen --
  //the library initializes this with an Adafruit splash screen
  display.display();
  delay(500);

  display.clearDisplay();
  print_line("Connecting to WIFI", 0, 0, 2);
  
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    display.print(".");
    display.display();
  }

  display.clearDisplay();
  print_line("Connected to WIFI", 0, 0, 2);

  configTime(time_zone_offset, UTC_OFFSET_DST, NTP_SERVER);

  //Clear the buffer
  display.clearDisplay();

  print_line("Welcome to Medibox", 10, 20, 2);
  delay(1000);
  display.clearDisplay();
  
  // Get initial time
  update_time();
  lastTimeUpdate = millis();
}


void print_line(String text, int column, int row, int text_size) {
  display.setTextSize(text_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row); //(column,row )
  display.println(text);

  display.display();
}

void print_time_now() {
  display.clearDisplay();
  
  // Format time with leading zeros for better readability
  String hourStr = (hours < 10) ? "0" + String(hours) : String(hours);
  String minStr = (minutes < 10) ? "0" + String(minutes) : String(minutes);
  String secStr = (seconds < 10) ? "0" + String(seconds) : String(seconds);
  
  // Display in larger font
  display.setTextSize(3);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(16, 16);
  display.print(hourStr + ":" + minStr + ":" + secStr);
  
  // Display date (optional)
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

  // Force a fresh NTP sync every hour to prevent drift
  static unsigned long lastNtpSync = 0;
  if (millis() - lastNtpSync > 3600000) { // Sync every hour
    configTime(time_zone_offset, UTC_OFFSET_DST, NTP_SERVER);
    lastNtpSync = millis();
  }

  char timeHour[3];
  strftime(timeHour,3,"%H",&timeinfo);
  hours = atoi(timeHour);

  char timeMinute[3];
  strftime(timeMinute,3,"%M",&timeinfo);
  minutes = atoi(timeMinute);

  char timeSecond[3];
  strftime(timeSecond,3,"%S",&timeinfo);
  seconds = atoi(timeSecond);

  char timeDay[3];
  strftime(timeDay,3,"%d",&timeinfo);
  days = atoi(timeDay);
}

// Calculate snooze end time
void calculate_snooze_end_time(int alarm_index) {
  snooze_end_hour[alarm_index] = hours;
  snooze_end_minute[alarm_index] = minutes + SNOOZE_DURATION;
  
  // Handle minute overflow
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

    // Find which alarm triggered
    for (int i = 0; i < n_alarms; i++) {
        if (alarm_active[i] && alarm_triggered[i]) {
            triggered_alarm = i;
            break;
        }
    }

    // Ring the buzzer
    while (!alarm_stopped) {
        for (int i = 0; i < n_notes && !alarm_stopped; i++) { 
            // Check for cancel button (stop alarm)
            if (digitalRead(PB_CANCEL) == LOW) {
                delay(200); // Debounce
                alarm_stopped = true;
                if (triggered_alarm >= 0) {
                    alarm_triggered[triggered_alarm] = false; // Reset trigger
                    snooze_active[triggered_alarm] = false; // Cancel snooze
                }
                break;
            }

            // Check for OK button (snooze alarm)
            if (digitalRead(PB_OK) == LOW && triggered_alarm >= 0) {
                delay(200); // Debounce
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

    // Handle snooze after stopping the alarm
    if (snooze_pressed && triggered_alarm >= 0) {
        snooze_active[triggered_alarm] = true;
        alarm_triggered[triggered_alarm] = false; // Reset trigger
        calculate_snooze_end_time(triggered_alarm);

        display.clearDisplay();
        print_line("Snoozed for 5 min", 0, 0, 2);
        delay(1000);
    }
}

// Fix for snooze functionality
void check_alarms() {
    if (alarm_enabled) {
        // Check regular alarms
        for (int i = 0; i < n_alarms; i++) {
            if (alarm_active[i] && !alarm_triggered[i] && 
                alarm_hours[i] == hours && alarm_minutes[i] == minutes && seconds == 0) {
                alarm_triggered[i] = true;
                ring_alarm();
            }
        }

        // Check snooze alarms (Fixed issue)
        for (int i = 0; i < n_alarms; i++) {
            if (snooze_active[i] && hours == snooze_end_hour[i] && 
                minutes == snooze_end_minute[i] && seconds == 0) {
                
                snooze_active[i] = false;  // **Reset snooze flag before ringing**
                alarm_triggered[i] = false; // **Ensure it doesn't retrigger continuously**
                ring_alarm();
            }
        }
    }

    // Reset alarm_triggered flags at midnight
    if (hours == 0 && minutes == 0 && seconds == 0) {
        for (int i = 0; i < n_alarms; i++) {
            alarm_triggered[i] = false;
            snooze_active[i] = false; // Also reset snooze at midnight
        }
    }
}

void update_time_with_check_alarms(void) {
  update_time();
  check_alarms();
}

void display_temp_warning() {
  display.clearDisplay();
  
  // Show current time at top
  String timeStr = (hours < 10 ? "0" : "") + String(hours) + ":" + 
                  (minutes < 10 ? "0" : "") + String(minutes);
  print_line(timeStr, 0, 0, 1);
  
  // Show temperature reading and status
  print_line("Temp: " + String(current_temp, 1) + "C", 0, 15, 1);
  if (temp_status != "") {
    print_line(temp_status, 0, 25, 1);
  }
  
  // Show humidity reading and status
  print_line("Humidity: " + String(current_humidity, 1) + "%", 0, 35, 1);
  if (humidity_status != "") {
    print_line(humidity_status, 0, 45, 1);
  }
  
  display.display();
}

int wait_for_button_press() {
  unsigned long last_time_update = millis();
  
  while (true) {
    // Keep updating time while waiting for button press
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
    // Keep updating time while in menu
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


void set_alarm(int alarm){
  int temp_hour = alarm_hours[alarm];
  int temp_minute = alarm_minutes[alarm];
  
  bool confirmed = false; // Track if user confirmed changes

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
      break;  // Proceed to setting minutes
    }
    else if (pressed == PB_CANCEL) {
      return; // Exit without saving
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
      confirmed = true; // Mark changes as confirmed
      break;
    }
    else if (pressed == PB_CANCEL) {
      return; // Exit without saving
    }
  }

  if (confirmed) { // Save only if user confirmed
    alarm_hours[alarm] = temp_hour;
    alarm_minutes[alarm] = temp_minute;
    display.clearDisplay();
    print_line("Alarm Set!", 0, 0, 2);
    delay(1000);
  }
}


// New function to view active alarms
void view_alarms() {
  display.clearDisplay();
  print_line("Active Alarms:", 0, 0, 1);
  
  int y_pos = 15;
  bool no_alarms = true;
  
  for (int i = 0; i < n_alarms; i++) {
    if (alarm_active[i]) {
      no_alarms = false;
      String alarm_str = "A" + String(i+1) + ": ";
      
      // Format hours with leading zero if needed
      if (alarm_hours[i] < 10) {
        alarm_str += "0";
      }
      alarm_str += String(alarm_hours[i]) + ":";
      
      // Format minutes with leading zero if needed
      if (alarm_minutes[i] < 10) {
        alarm_str += "0";
      }
      alarm_str += String(alarm_minutes[i]);
      
      // Show snooze status
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
  
  // Wait for button press to return
  while (digitalRead(PB_CANCEL) == HIGH) {
    if (digitalRead(PB_CANCEL) == LOW) {
      delay(200);
      break;
    }
    update_time();
  }
}

// New function to delete a specific alarm
void delete_alarm() {
  int selected_alarm = 0;
  
  while (true) {
    display.clearDisplay();
    
    // Show alarm info
    String alarm_info = "Alarm " + String(selected_alarm + 1) + ": ";
    if (alarm_active[selected_alarm]) {
      // Format time with leading zeros
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
      
      // Confirm deletion
      display.clearDisplay();
      print_line("Delete Alarm " + String(selected_alarm + 1) + "?", 0, 0, 1);
      print_line("OK to confirm", 0, 20, 1);
      print_line("CANCEL to abort", 0, 35, 1);
      
      int confirm = wait_for_button_press();
      if (confirm == PB_OK) {
        // Delete the alarm by setting it inactive
        alarm_active[selected_alarm] = false;
        snooze_active[selected_alarm] = false;
        
        display.clearDisplay();
        print_line("Alarm " + String(selected_alarm + 1) + " deleted", 0, 0, 1);
        delay(1000);
        break;
      }
      else if (confirm == PB_CANCEL) {
        delay(200);
        // Continue without deleting
      }
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      break;
    }
  }
}

// New function to set time zone
void set_time_zone() {
  int temp_offset_hours = time_zone_offset / 3600;
  
  display.clearDisplay();
  print_line("Set UTC offset", 0, 0, 1);
  delay(1000);
  
  // Set hours part of offset
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
      return; // Exit without saving
    }
  }
  
  // Set minutes part of offset (0, 15, 30, 45)
  int temp_offset_minutes = (time_zone_offset % 3600) / 60;
  
  while (true) {
    display.clearDisplay();
    print_line("Minutes: " + String(temp_offset_minutes), 0, 0, 2);
    print_line("(0, 15, 30, 45)", 0, 20, 1);
    
    int pressed = wait_for_button_press();
    if (pressed == PB_UP) {
      delay(200);
      // Cycle through valid offset values
      if (temp_offset_minutes == 0) temp_offset_minutes = 15;
      else if (temp_offset_minutes == 15) temp_offset_minutes = 30;
      else if (temp_offset_minutes == 30) temp_offset_minutes = 45;
      else temp_offset_minutes = 0;
    }
    else if (pressed == PB_DOWN) {
      delay(200);
      // Cycle through valid offset values in reverse
      if (temp_offset_minutes == 0) temp_offset_minutes = 45;
      else if (temp_offset_minutes == 45) temp_offset_minutes = 30;
      else if (temp_offset_minutes == 30) temp_offset_minutes = 15;
      else temp_offset_minutes = 0;
    }
    else if (pressed == PB_OK) {
      delay(200);
      // Calculate new offset in seconds
      time_zone_offset = (temp_offset_hours * 3600) + (temp_offset_minutes * 60);
      
      // Update time with new offset
      configTime(time_zone_offset, UTC_OFFSET_DST, NTP_SERVER);
      
      display.clearDisplay();
      print_line("Time zone set", 0, 0, 2);
      delay(1000);
      break;
    }
    else if (pressed == PB_CANCEL) {
      delay(200);
      return; // Exit without saving
    }
  }
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
  // Removed the "Disable Alarms" case (previously mode == 5)
}

void check_temp() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();
  current_temp = data.temperature;
  current_humidity = data.humidity;
  
  // Reset status strings
  temp_status = "";
  humidity_status = "";
  
  // Check if there's a warning condition
  bool has_warning = false;
  
  // Check temperature against healthy limits
  if (data.temperature > MAX_HEALTHY_TEMP) {
    temp_status = "TEMP HIGH";
    has_warning = true;
  } 
  else if (data.temperature < MIN_HEALTHY_TEMP) {
    temp_status = "TEMP LOW";
    has_warning = true;
  }
  
  // Check humidity against healthy limits
  if (data.humidity > MAX_HEALTHY_HUMIDITY) {
    humidity_status = "HUMIDITY HIGH";
    has_warning = true;
  } 
  else if (data.humidity < MIN_HEALTHY_HUMIDITY) {
    humidity_status = "HUMIDITY LOW";
    has_warning = true;
  }
  
  // If there's a warning, show it for 3 seconds and activate LED/buzzer
  if (has_warning) {
    display_warning = true;
    warning_end_time = millis() + 3000; // Show for 3 seconds
    
    // Activate LED and buzzer for warnings
    digitalWrite(LED_1, HIGH);
    
    // Play warning tone with buzzer
    for (int i = 0; i < 3; i++) {
      tone(BUZZER, notes[7]); // Use highest note for warning
      delay(100);
      noTone(BUZZER);
      delay(100);
    }
    
    digitalWrite(LED_1, LOW);
  }

}
  void loop() {
  static unsigned long lastSecondTick = 0;
  unsigned long currentMillis = millis();
  
  // Update time exactly every second
  if (currentMillis - lastSecondTick >= 1000) {
    lastSecondTick = currentMillis;
    update_time();
    check_alarms();
  }
  
  // Check for menu button press
  if (digitalRead(PB_OK) == LOW) {
    delay(200);
    go_to_menu();
  }
  
  // Periodically check temperature/humidity
  if (currentMillis - lastTempCheck >= TEMP_CHECK_INTERVAL) {
    lastTempCheck = currentMillis;
    check_temp();
  }
  
  // Display time or warnings based on current state
  if (display_warning && millis() < warning_end_time) {
    display_temp_warning();
  } else {
    display_warning = false;
    print_time_now();
  }


}
