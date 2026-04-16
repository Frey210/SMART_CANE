/**
 * ============================================================================
 * PROYEK: TONGKAT TUNA NETRA DENGAN 4 SENSOR ULTRASONIK + BUZZER
 * ============================================================================
 * 
 * DESKRIPSI:
 *   Tongkat pintar untuk membantu penyandang tuna netra mendeteksi halangan
 *   di sekitar. Menggunakan 4 sensor HC-SR04 yang dipasang di:
 *   - Depan (arah jalan)
 *   - Kiri
 *   - Kanan
 *   - Atas (untuk deteksi palang rendah/ranting)
 * 
 *   Buzzer akan berbunyi dengan interval yang semakin rapat saat halangan
 *   semakin dekat. Jarak peringatan dimulai dari 150 cm.
 * 
 *   Suara semakin "mendesak" = objek semakin dekat.
 * 
 * HARDWARE:
 *   - ESP32 DevKit V1
 *   - 4x HC-SR04 (sensor ultrasonik)
 *   - 1x Buzzer (aktif atau pasif)
 *   - 4x Resistor 10kΩ + 4x Resistor 20kΩ (pembagi tegangan untuk Echo)
 * 
 * WIRING (PIN AMAN UNTUK ESP32):
 *   Sensor Depan  : TRIG=GPIO13, ECHO=GPIO12
 *   Sensor Kiri   : TRIG=GPIO16, ECHO=GPIO27
 *   Sensor Kanan  : TRIG=GPIO26, ECHO=GPIO25
 *   Sensor Atas   : TRIG=GPIO21, ECHO=GPIO32
 *   Buzzer        : GPIO15 (aktif LOW? tergantung modul, ubah HIGH/LOW)
 * 
 * CATATAN PENTING:
 *   - Semua pin ECHO WAJIB melalui pembagi tegangan (5V -> 3.3V)
 *   - Hindari pin: GPIO1,3 (serial), GPIO6-11 (flash), GPIO0,2,5,15 (boot)
 *   - GPIO15 sudah aman untuk buzzer karena internal pull-down
 * 
 * DIBUAT OLEH: (Nama Anda)
 * TANGGAL:     April 2026
 * ============================================================================
 */

#include <Arduino.h>

// ========================= KONFIGURASI PIN ==================================
// Sensor Ultrasonik (4 buah)
// Struktur penamaan: TRIG_<POSISI>, ECHO_<POSISI>
// POSISI: FRONT (depan), LEFT (kiri), RIGHT (kanan), TOP (atas)

// Sensor 1 - DEPAN (arah jalan)
#define TRIG_FRONT   13
#define ECHO_FRONT   12

// Sensor 2 - KIRI
#define TRIG_LEFT    16
#define ECHO_LEFT    27

// Sensor 3 - KANAN
#define TRIG_RIGHT   26
#define ECHO_RIGHT   25

// Sensor 4 - ATAS (deteksi palang rendah / ranting)
#define TRIG_TOP     21
#define ECHO_TOP     32

// Buzzer
#define BUZZER_PIN   15

// ========================= KONSTANTA SISTEM =================================
const float SOUND_SPEED_CM_US = 0.0343;   // Kecepatan suara: 343 m/s = 0.0343 cm/µs
const float MAX_DISTANCE_CM = 200.0;      // Batas maksimal deteksi (cm)
const float WARNING_DISTANCE_CM = 150.0;  // Jarak mulai peringatan (cm)

// Parameter buzzer (untuk mode digital on/off)
const int BEEP_DURATION_MS = 50;          // Lama bunyi setiap kali beep (ms)
int beepIntervalMs = 0;                   // Jeda antar beep (diupdate berdasarkan jarak)
                                           // 0 = buzzer mati

// ========================= STRUKTUR DATA ====================================
/**
 * Struktur untuk menyimpan data satu sensor ultrasonik
 */
struct UltrasonicSensor {
  uint8_t trigPin;      // Pin trigger (output)
  uint8_t echoPin;      // Pin echo (input, sudah melalui pembagi tegangan)
  float distanceCm;     // Jarak terukur (cm)
  const char* location; // Nama posisi (untuk debugging)
};

// Inisialisasi array sensor (memudahkan iterasi)
UltrasonicSensor sensors[] = {
  {TRIG_FRONT, ECHO_FRONT, 0, "DEPAN"},
  {TRIG_LEFT,  ECHO_LEFT,  0, "KIRI"},
  {TRIG_RIGHT, ECHO_RIGHT, 0, "KANAN"},
  {TRIG_TOP,   ECHO_TOP,   0, "ATAS"}
};

const int SENSOR_COUNT = sizeof(sensors) / sizeof(sensors[0]);

// Variabel untuk non-blocking buzzer
unsigned long lastBeepTime = 0;
bool isBuzzerActive = false;      // Status buzzer (menyala/tidak)

// ========================= FUNGSI SENSOR ====================================

/**
 * Membaca jarak dari satu sensor ultrasonik (mode blocking singkat)
 * 
 * @param sensor Referensi ke struktur UltrasonicSensor
 * @return float Jarak dalam cm, atau MAX_DISTANCE_CM jika timeout
 * 
 * Proses:
 *   1. Kirim pulse trigger 10µs
 *   2. Tunggu pulse echo (dengan timeout)
 *   3. Hitung jarak = (durasi * kecepatan suara) / 2
 */
float readSingleDistance(UltrasonicSensor &sensor) {
  // 1. Kirim trigger pulse (LOW -> HIGH -> LOW)
  digitalWrite(sensor.trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(sensor.trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(sensor.trigPin, LOW);
  
  // 2. Baca durasi pulse echo (timeout 25ms = ~425cm maksimal)
  //    pulseIn() akan menunggu sampai echo HIGH lalu mengukur panjangnya
  long durationUs = pulseIn(sensor.echoPin, HIGH, 25000);
  
  // 3. Jika timeout (tidak ada echo), kembalikan nilai maksimal
  if (durationUs == 0) {
    return MAX_DISTANCE_CM;
  }
  
  // 4. Hitung jarak: (waktu * kecepatan suara) / 2 (karena bolak-balik)
  float distance = durationUs * SOUND_SPEED_CM_US / 2.0;
  
  // 5. Batasi nilai maksimal
  if (distance > MAX_DISTANCE_CM) {
    return MAX_DISTANCE_CM;
  }
  
  return distance;
}

/**
 * Memperbarui jarak untuk semua sensor
 * 
 * Membaca keempat sensor secara berurutan. Setiap pembacaan memakan waktu
 * sekitar 10-20ms tergantung jarak objek.
 */
void updateAllDistances() {
  for (int i = 0; i < SENSOR_COUNT; i++) {
    sensors[i].distanceCm = readSingleDistance(sensors[i]);
  }
}

/**
 * Mencari jarak minimum dari semua sensor
 * 
 * @return float Jarak terdekat (cm) dari keempat sensor
 * 
 * Fungsi ini digunakan untuk menentukan prioritas peringatan:
 *   Jika ada halangan di mana pun, buzzer akan merespons halangan TERDEKAT.
 */
float getMinimumDistance() {
  float minDist = MAX_DISTANCE_CM;
  for (int i = 0; i < SENSOR_COUNT; i++) {
    if (sensors[i].distanceCm < minDist) {
      minDist = sensors[i].distanceCm;
    }
  }
  return minDist;
}

/**
 * Menentukan sensor dengan jarak terdekat (untuk debugging)
 * 
 * @return const char* Nama posisi sensor terdekat
 */
const char* getClosestSensorLocation() {
  float minDist = MAX_DISTANCE_CM;
  int closestIndex = 0;
  for (int i = 0; i < SENSOR_COUNT; i++) {
    if (sensors[i].distanceCm < minDist) {
      minDist = sensors[i].distanceCm;
      closestIndex = i;
    }
  }
  return sensors[closestIndex].location;
}

// ========================= FUNGSI BUZZER ====================================

/**
 * Menghitung interval beep berdasarkan jarak terdekat
 * 
 * LOGIKA (mirip sensor parkir mobil):
 *   - Jarak 0 cm   -> interval 30 ms (bunyi hampir terus-menerus)
 *   - Jarak 150 cm -> interval 500 ms (bunyi lambat)
 *   - Jarak >150cm -> interval 0 (buzzer mati)
 * 
 * @param distanceCm Jarak terdekat (cm)
 */
void updateBeepInterval(float distanceCm) {
  // Jika di luar batas peringatan, matikan buzzer
  if (distanceCm > WARNING_DISTANCE_CM) {
    beepIntervalMs = 0;
    return;
  }
  
  // Mapping linier: jarak 0..150cm -> interval 30..500ms
  // Semakin kecil jarak, semakin kecil interval (bunyi lebih cepat)
  beepIntervalMs = map(distanceCm, 0, WARNING_DISTANCE_CM, 30, 500);
  
  // Pastikan interval dalam batas yang wajar
  beepIntervalMs = constrain(beepIntervalMs, 30, 500);
}

/**
 * Mengontrol buzzer secara non-blocking (mode digital on/off)
 * 
 * Cara kerja:
 *   - Jika beepIntervalMs == 0, buzzer mati total
 *   - Jika tidak, buzzer akan menyala selama BEEP_DURATION_MS
 *   - Kemudian mati selama (beepIntervalMs - BEEP_DURATION_MS)
 *   - Siklus berulang secara otomatis tanpa delay()
 * 
 * Fungsi ini harus dipanggil di loop() sesering mungkin.
 */
void handleBuzzer() {
  // Dapatkan jarak terdekat dan update interval
  float minDistance = getMinimumDistance();
  updateBeepInterval(minDistance);
  
  // Jika tidak ada peringatan, pastikan buzzer mati
  if (beepIntervalMs == 0) {
    digitalWrite(BUZZER_PIN, LOW);
    isBuzzerActive = false;
    return;
  }
  
  unsigned long currentTime = millis();
  
  if (!isBuzzerActive) {
    // BUZZER SEDANG MATI: nyalakan jika sudah waktunya
    if (currentTime - lastBeepTime >= beepIntervalMs) {
      digitalWrite(BUZZER_PIN, HIGH);
      isBuzzerActive = true;
      lastBeepTime = currentTime;
    }
  } else {
    // BUZZER SEDANG MENYALA: matikan setelah durasi bunyi tercapai
    if (currentTime - lastBeepTime >= BEEP_DURATION_MS) {
      digitalWrite(BUZZER_PIN, LOW);
      isBuzzerActive = false;
      lastBeepTime = currentTime;
    }
  }
}

// ========================= DEBUGGING & SERIAL ===============================

/**
 * Mencetak informasi semua sensor ke Serial Monitor
 * 
 * Format output:
 *   DEPAN: 45cm  KIRI: 120cm  KANAN: 32cm  ATAS: 78cm  | MIN: 32cm (KANAN)
 * 
 * @param forcePrint Jika true, tetap cetak meskipun belum waktunya (jarang dipakai)
 */
void printDebugInfo(bool forcePrint = false) {
  static unsigned long lastPrintTime = 0;
  const unsigned long PRINT_INTERVAL_MS = 300;  // Cetak setiap 300ms
  
  if (!forcePrint && (millis() - lastPrintTime < PRINT_INTERVAL_MS)) {
    return;
  }
  
  Serial.print("[INFO] ");
  for (int i = 0; i < SENSOR_COUNT; i++) {
    Serial.print(sensors[i].location);
    Serial.print(": ");
    Serial.print(sensors[i].distanceCm);
    Serial.print("cm  ");
  }
  
  Serial.print("| MIN: ");
  Serial.print(getMinimumDistance());
  Serial.print("cm (");
  Serial.print(getClosestSensorLocation());
  Serial.println(")");
  
  // Tambahan informasi interval buzzer jika aktif
  if (beepIntervalMs > 0) {
    Serial.print("      Buzzer interval: ");
    Serial.print(beepIntervalMs);
    Serial.println("ms (semakin kecil = semakin dekat)");
  }
  
  lastPrintTime = millis();
}

// ========================= SETUP & LOOP =====================================

void setup() {
  // Inisialisasi Serial Monitor untuk debugging
  Serial.begin(115200);
  delay(100);  // Tunggu serial siap
  
  Serial.println("========================================");
  Serial.println("TONGKAT TUNA NETRA v1.0");
  Serial.println("4 Sensor Ultrasonik + Buzzer");
  Serial.println("========================================");
  Serial.println();
  
  // Inisialisasi pin untuk semua sensor
  for (int i = 0; i < SENSOR_COUNT; i++) {
    pinMode(sensors[i].trigPin, OUTPUT);
    pinMode(sensors[i].echoPin, INPUT);
    digitalWrite(sensors[i].trigPin, LOW);  // Pastikan trigger LOW saat awal
    Serial.print("✓ Sensor ");
    Serial.print(sensors[i].location);
    Serial.println(" siap");
  }
  
  // Inisialisasi buzzer
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  Serial.println("✓ Buzzer siap");
  
  Serial.println();
  Serial.println("SISTEM SIAP! Dekatkan halangan ke sensor...");
  Serial.println("========================================");
  Serial.println();
  
  delay(500);
  
  // Tes buzzer sebentar (opsional, untuk konfirmasi hardware)
  digitalWrite(BUZZER_PIN, HIGH);
  delay(100);
  digitalWrite(BUZZER_PIN, LOW);
}

void loop() {
  // 1. Baca semua sensor (blocking singkat ~10-80ms total)
  updateAllDistances();
  
  // 2. Debugging (cetak ke Serial Monitor secara periodik)
  printDebugInfo();
  
  // 3. Kontrol buzzer berdasarkan jarak terdekat
  handleBuzzer();
  
  // 4. Delay kecil untuk stabilisasi dan memberi waktu CPU
  //    Total loop sekitar 20-100ms tergantung jarak objek.
  delay(15);
}