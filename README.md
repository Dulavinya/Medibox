# 💊 Medibox – Smart Medicine Reminder (ESP32 + OLED + IoT)

**Medibox** is a compact medicine reminder system powered by an ESP32. It features alarm scheduling, snooze functionality, environmental monitoring, and an intuitive menu-driven interface via an OLED display. Built for reliability, ease of use, and future scalability.

---

## ✨ Features

- ⏰ Schedule up to **5 customizable medicine alarms**  
- 🔁 **Snooze** support for delayed reminders  
- 🌡️ Real-time **temperature & humidity monitoring** (DHT22)  
- 📆 **Automatic time sync** via WiFi using NTP  
- 🖥️ **OLED display** with interactive, menu-based UI  
- 🔘 Simple **3-button navigation system**  
- 🔊 **Audible buzzer alerts**  
- 🔧 Modular and extendable for additional features  

---

## 🧩 Hardware Components

- **ESP32 Dev Board**  
- **OLED Display** (128x64, I2C)  
- **DHT22 Sensor** – Temperature & Humidity  
- **3 Push Buttons** – Navigation & Snooze  
- **Piezo Buzzer** – Alarm output  
- **WiFi Access** – For NTP time sync  

---

## 🖥️ Menu Interface

An intuitive, button-controlled menu allows users to:

- 🕒 **View Current Time**  
- ⏰ **Set Alarms**  
- ❌ **Delete Alarms**  
- 📋 **View Alarm List**  
- 😴 **Snooze Active Alarm**  
- 🌡️ **Monitor Temperature & Humidity**

---

## 🔔 Alarm System

- The buzzer activates when an alarm is triggered.  
- Pressing the snooze button delays the alert by **5 minutes**.  
- Multiple alarms can be created, reviewed, and managed via the menu.

---

## 🌐 Connectivity

- Automatically syncs time via **NTP over WiFi**.  
- Optional offline support via RTC module in future iterations.  
- Foundation laid for **cloud integration** and **remote control**.

---

## 🚀 Roadmap & Future Enhancements

- 💾 Store alarms in **EEPROM** for persistence after power loss  
- 📱 Add **mobile notifications** (e.g., Blynk, Telegram)  
- 📊 Track **medication intake history**  
- 🌐 Build a **web dashboard** for remote configuration  
- 🖲️ Upgrade to **touchscreen** or **rotary encoder** interface  

---


