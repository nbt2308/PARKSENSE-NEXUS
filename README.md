# ParkSense Nexus – IoT Smart Parking System

**Sense the Space. Simplify the Parking.**

ParkSense Nexus is an IoT-based smart parking system designed to monitor parking slot availability, automate vehicle access, and provide remote parking status updates through a web dashboard and Telegram notifications.

The system uses an ESP32 microcontroller and ultrasonic sensors to detect vehicle presence, control an automatic barrier, and transmit parking data to ThingSpeak for remote monitoring.

## Features

- **Real-time Parking Monitoring** – Monitor the occupancy status of three parking slots.
- **Automatic Barrier Control** – Open or close the entrance barrier using a servo motor based on vehicle detection and parking availability.
- **Ultrasonic Distance Measurement** – Measure distances to detect vehicles in parking slots and at the entrance.
- **OLED Status Display** – Display parking availability and system status locally.
- **ThingSpeak Integration** – Upload parking data for remote monitoring and visualization.
- **Web Dashboard** – View parking occupancy, available spaces, and barrier status through a web interface.
- **Telegram Notifications** – Receive alerts about parking availability and sensor/system events.
- **Wi-Fi Connectivity** – Connect the ESP32 to a wireless network to send monitoring data.

## System Architecture

```text
Ultrasonic Sensors
        |
        v
      ESP32
        |
        +------> Servo Motor (Entrance Barrier)
        |
        +------> OLED Display
        |
        +------> ThingSpeak
        |            |
        |            v
        |       Web Dashboard
        |
        +------> Telegram Bot
                   |
                   v
              Notifications
```

## Hardware Components

| Component | Purpose |
|---|---|
| ESP32 | Main controller and Wi-Fi communication |
| HC-SR04 Ultrasonic Sensors | Detect vehicles and measure distance |
| Servo Motor | Control the entrance barrier |
| LEDs | Indicate parking slot status |
| SSD1306 OLED Display | Display parking information |
| Breadboard and jumper wires | Circuit assembly |

*The system is designed around three parking slots and one entrance detection point.*

## Software and Technologies

- **Embedded:** Arduino framework for ESP32, C++
- **Sensors and Actuators:** HC-SR04, servo motor, LEDs
- **Display:** Adafruit SSD1306, Adafruit GFX
- **Cloud Monitoring:** ThingSpeak
- **Notifications:** Telegram Bot API
- **Frontend:** HTML, CSS, JavaScript
- **Deployment:** Vercel (planned or optional)
- **Simulation:** Wokwi

## How It Works

1. Ultrasonic sensors measure the distance to nearby objects.
2. The ESP32 determines whether each parking slot is occupied.
3. The system calculates the number of available parking spaces.
4. LEDs and the OLED display provide local status information.
5. When a vehicle approaches the entrance, the ESP32 checks parking availability and controls the servo-operated barrier.
6. Parking information is sent to ThingSpeak over Wi-Fi.
7. The web dashboard retrieves the cloud data and displays the current parking status.
8. Telegram notifications provide remote alerts when configured.

## ThingSpeak Data Fields

The dashboard uses the following field mapping:

| Field | Description |
|---|---|
| Field 1 | Parking Slot 1 occupancy |
| Field 2 | Parking Slot 2 occupancy |
| Field 3 | Parking Slot 3 occupancy |
| Field 4 | Number of available slots |
| Field 5 | Number of occupied slots |
| Field 6 | Parking occupancy percentage |
| Field 7 | Entrance barrier status |
| Field 8 | Entrance sensor distance |

For occupancy fields, `1` represents occupied and `0` represents empty. Barrier status and distance values are reported by the ESP32.

## Getting Started

### 1. Clone the Repository

```bash
git clone https://github.com/nbt2308/PARKSENSE-NEXUS.git
cd PARKSENSE-NEXUS
```

### 2. Configure the ESP32

1. Open the ESP32 project in Arduino IDE or Wokwi.
2. Install the required libraries:
   - ESP32Servo
   - Adafruit GFX Library
   - Adafruit SSD1306
3. Create a local `secrets.h` file based on `secrets.example.h`.
4. Configure your Wi-Fi credentials, ThingSpeak Write API Key, and Telegram bot settings.
5. Verify the sensor and actuator pin assignments.
6. Build and upload the sketch, or run the simulation in Wokwi.

**Security:** Never commit `secrets.h`, real API keys, or Telegram bot tokens to a public repository.

### 3. Configure ThingSpeak

1. Create a ThingSpeak channel.
2. Enable the eight fields listed above.
3. Configure the ESP32 with the channel's Write API Key.
4. Verify that data is being received before opening the dashboard.

### 4. Run the Web Dashboard

The dashboard is a static web application and can be hosted on a static hosting service such as Vercel.

1. Open `index.html`.
2. Configure the ThingSpeak Channel ID in the dashboard configuration.
3. If the channel is public, the dashboard can read its data without a Read API Key.
4. If the channel is private, use a backend endpoint to protect the Read API Key instead of embedding it in public frontend code.
5. Deploy the dashboard to Vercel or another static hosting provider.

## Security Notes

- Keep API keys and Telegram bot tokens outside tracked source files.
- Add `secrets.h` and local environment files to `.gitignore`.
- Use placeholder values in `secrets.example.h`.
- Do not expose ThingSpeak Write API Keys in frontend code.
- A public ThingSpeak channel exposes its channel data to anyone who can access it.
- If a credential has been exposed, rotate it before deploying the project publicly.

## Project Structure

The exact structure may vary depending on the repository organization.

```text
ParkSense-Nexus/
│
├── sketch.ino
├── secrets.example.h
├── dashboard/
│   └── index.html
├── diagram.json
├── libraries.txt
├── .gitignore
└── README.md
```

Adjust the directory names to match the actual repository. Do not commit the local `secrets.h` file.

## Future Improvements

- Add historical parking statistics and occupancy charts.
- Improve sensor failure detection and system diagnostics.
- Add a mobile-friendly dashboard layout.
- Support multiple parking areas.
- Add user authentication and role-based access control.
- Store historical parking events in a dedicated database.
- Introduce a backend API for secure data access and management.

## Project Status

This project is being developed as an IoT smart parking system. Features and implementation details may change during development.

## License

This project is licensed under the MIT License. See the LICENSE file for details.

---

**ParkSense Nexus**  
*An IoT-based approach to smarter parking management.*
