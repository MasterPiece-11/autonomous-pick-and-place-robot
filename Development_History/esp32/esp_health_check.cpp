#include <Arduino.h>
#include <WiFi.h>

void setup() {
  Serial.begin(115200);
  
  // Set WiFi to station mode and disconnect from an AP if it was previously connected
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.println("\n--- Testing Wi-Fi Hardware Block ---");
}

void loop() {
  Serial.println("Scanning for networks...");

  // WiFi.scanNetworks() will return the number of networks found
  int n = WiFi.scanNetworks();
  Serial.println("Scan complete!");
  
  if (n == 0) {
    Serial.println("Result: No networks found. (If you are near a router, the antenna or radio might be damaged.)");
  } else if (n > 0) {
    Serial.print("Result: SUCCESS! Found ");
    Serial.print(n);
    Serial.println(" networks. The Wi-Fi/Bluetooth radio is 100% healthy!");
    
    // Print the names of the first few networks it sees
    for (int i = 0; i < min(n, 3); ++i) {
      Serial.print(i + 1);
      Serial.print(": ");
      Serial.print(WiFi.SSID(i));
      Serial.print(" (");
      Serial.print(WiFi.RSSI(i));
      Serial.println(" dBm)");
    }
  } else {
    Serial.println("Result: CRITICAL ERROR! Wi-Fi hardware failed to initialize.");
  }
  
  Serial.println("-----------------------------------\n");
  delay(5000); // Wait 5 seconds before scanning again
}