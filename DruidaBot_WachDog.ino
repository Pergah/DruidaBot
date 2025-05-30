#define RELAY_PIN 2

unsigned long lastReceivedTime = 0;
const unsigned long interval = 120000; // 20 segundos
int missedSignals = 0;

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Relé apagado
  Serial.begin(115200);
  lastReceivedTime = millis();
}

void loop() {
  // Verifica si llegó algo por Serial
  Serial.println("test");
  if (Serial.available()) {
    String incoming = Serial.readStringUntil('\n');
    if (incoming.indexOf("PING") != -1) {
      missedSignals = 0; // Señal recibida correctamente
      lastReceivedTime = millis();
    }
  }

  // Verifica si pasó un intervalo sin recibir señal
  if (millis() - lastReceivedTime >= interval) {
    missedSignals++;
    lastReceivedTime = millis(); // Reset para esperar el siguiente intento
  }

  // Si se han perdido 3 señales consecutivas (60 segundos)
  if (missedSignals >= 3) {
    digitalWrite(RELAY_PIN, HIGH); // Activa el relé
    delay(2000); // 2 segundos
    digitalWrite(RELAY_PIN, LOW);  // Apaga el relé
    missedSignals = 0; // Reinicia el contador
  }
  delay(1000);
}
