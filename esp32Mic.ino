#include <ESP_I2S.h>
#include <LittleFS.h>

#define SAMPLE_RATE 16000
#define RECORD_TIME_SEC 10
#define PAUSE_TIME_SEC 10

String currentFilePath = "";

// Для ядра 3.x нужно явно создать объект интерфейса I2S
I2SClass I2S; 

void setup() {
    Serial.begin(500000);
    delay(1000);
    Serial.printf("[%lu ms] [ENTER] setup | params: none\n", millis());

    // Отключаем встроенную SD-карту (GPIO 21 - SD Card CS), 
    // чтобы она не конфликтовала с PDM-шиной микрофона
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);
    delay(100); 

    if (!LittleFS.begin(true)) {
        Serial.printf("[%lu ms] [EXIT ERROR] setup | error: LittleFS Mount Failed\n", millis());
        return;
    }
    Serial.printf("[%lu ms] [EXIT] setup | LittleFS Initialized Successfully\n", millis());

    // Настройка PDM-пинов для платы XIAO ESP32S3 Sense (CLK = 42, DATA = 41)
    I2S.setPinsPdmRx(42, 41);
    
    // Инициализация I2S-шины с использованием нового синтаксиса ESP32 3.x
    if (!I2S.begin(I2S_MODE_PDM_RX, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO)) {
        Serial.printf("[%lu ms] [EXIT ERROR] setup | error: I2S init failed!\n", millis());
        return;
    }
    
    Serial.printf("[%lu ms] [EXIT] setup | Mic initialized and started successfully\n", millis());
}

void removeOldAudioSession() {

    if (!LittleFS.begin(true)) {
        Serial.printf("[%lu ms] [EXIT ERROR] setup | error: LittleFS Mount Failed\n", millis());
        return;
    }
    
    // ВРЕМЕННАЯ СТРОКА: Принудительное форматирование всей файловой системы
    LittleFS.format(); 
    Serial.println("LittleFS Format Complete!");

    File root = LittleFS.open("/");
    if (root) {
        File file = root.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                String fileName = String(file.name());
                if (fileName.endsWith(".pcm")) {
                    // Гарантируем наличие слеша перед именем файла для корректного удаления
                    String filePath = fileName.startsWith("/") ? fileName : "/" + fileName;
                    LittleFS.remove(filePath);
                    Serial.printf("[%lu ms] [CLEANUP] Deleted old file: %s\n", millis(), filePath.c_str());
                }
            }
            file = root.openNextFile();
        }
    }    
}

void recordAudioSession1() {
    unsigned long startTimestamp = millis();
    currentFilePath = "/mic_" + String(startTimestamp) + ".pcm";
    
    Serial.printf("[%lu ms] [ENTER] recordAudioSession | starting 10s recording session | target: %s\n", startTimestamp, currentFilePath.c_str());

    File file = LittleFS.open(currentFilePath, "w");
    if (!file) {
        Serial.printf("[%lu ms] [EXIT ERROR] recordAudioSession | error: failed to open file for writing\n", millis());
        return;
    }

    uint8_t buffer[512];
    size_t totalBytesWritten = 0;
    unsigned long startMillis = millis();

    while (millis() - startMillis < (RECORD_TIME_SEC * 1000)) {
        // Чтение данных через новую библиотеку ESP_I2S
        size_t bytesRead = I2S.readBytes((char*)buffer, sizeof(buffer));
        if (bytesRead > 0) {
            file.write(buffer, bytesRead);
            totalBytesWritten += bytesRead;
//            Serial.printf("I2S.readBytes:%lu\n", totalBytesWritten);
        }
    }


    file.close();
    
    Serial.printf("[%lu ms] [EXIT] recordAudioSession | file: %s | location: Internal Flash | size: %u bytes\n", 
                  millis(), currentFilePath.c_str(), (unsigned int)totalBytesWritten);
}

void recordAudioSession() {
    unsigned long startTimestamp = millis();
    currentFilePath = "/mic_" + String(startTimestamp) + ".pcm";
    
    Serial.printf("[%lu ms] [ENTER] recordAudioSession | starting 10s recording session | target: %s\n", startTimestamp, currentFilePath.c_str());

    File file = LittleFS.open(currentFilePath, "w");
    if (!file) {
        Serial.printf("[%lu ms] [EXIT ERROR] recordAudioSession | error: failed to open file for writing\n", millis());
        return;
    }

    // Множитель громкости. 
    // Начните с 8. Если все еще тихо - поставьте 12 или 16. Если звук хрипит - снизьте до 4-5.
    const int VOLUME_GAIN = 12; 

    uint8_t buffer[512];
    size_t totalBytesWritten = 0;
    unsigned long startMillis = millis();

    while (millis() - startMillis < (RECORD_TIME_SEC * 1000)) {
        size_t bytesRead = I2S.readBytes((char*)buffer, sizeof(buffer));
        
        if (bytesRead > 0) {
            // --- УСИЛЕНИЕ ЗВУКА ---
            // Буфер состоит из байтов, но наши сэмплы - это 16-битные числа (2 байта).
            // Поэтому мы читаем буфер как массив int16_t
            int16_t* samples = (int16_t*)buffer;
            size_t numSamples = bytesRead / 2; 
            
            for (size_t i = 0; i < numSamples; i++) {
                // Умножаем звук на множитель (используем int32_t для временного хранения)
                int32_t amplified = (int32_t)samples[i] * VOLUME_GAIN;
                
                // Клиппинг: жестко обрезаем пики, выходящие за пределы 16 бит, 
                // чтобы звук не превращался в цифровой шум
                if (amplified > 32767) amplified = 32767;
                if (amplified < -32768) amplified = -32768;
                
                // Возвращаем усиленный сэмпл обратно в буфер
                samples[i] = (int16_t)amplified;
            }
            // -----------------------

            file.write(buffer, bytesRead);
            totalBytesWritten += bytesRead;
        }
    }

    file.close();
    
    Serial.printf("[%lu ms] [EXIT] recordAudioSession | file: %s | location: Internal Flash | size: %u bytes\n", 
                  millis(), currentFilePath.c_str(), (unsigned int)totalBytesWritten);
}

void sendFileToPC() {
    unsigned long startTimestamp = millis();
    Serial.printf("[%lu ms] [ENTER] sendFileToPC | sending %s\n", startTimestamp, currentFilePath.c_str());

    if (!LittleFS.exists(currentFilePath)) {
        Serial.printf("[%lu ms] [EXIT ERROR] sendFileToPC | error: file not found\n", millis());
        return;
    }
    
    File file = LittleFS.open(currentFilePath, "r");
    if (!file) {
        Serial.printf("[%lu ms] [EXIT ERROR] sendFileToPC | error: failed to open file for reading\n", millis());
        return;
    }
    
    Serial.printf("START_FILE_TRANSFER:%s\n", currentFilePath.c_str());
    
    while (file.available()) {
        uint8_t buf[256];
        size_t len = file.read(buf, sizeof(buf));
        Serial.write(buf, len);
    }
    file.close();
    
    Serial.printf("[%lu ms] [EXIT] sendFileToPC | transfer completed successfully\n", millis());
    
    // Удаляем файл после успешной передачи
    LittleFS.remove(currentFilePath);
}

void loop() {
    unsigned long loopStart = millis();
    Serial.printf("[%lu ms] [ENTER] loop | cycle started\n", loopStart);

    removeOldAudioSession();
    recordAudioSession();
    sendFileToPC();

    unsigned long pauseStart = millis();
    Serial.printf("[%lu ms] [ENTER] pause | starting 10s pause\n", pauseStart);
    delay(PAUSE_TIME_SEC * 1000);
    Serial.printf("[%lu ms] [EXIT] pause | pause completed\n", millis());
}