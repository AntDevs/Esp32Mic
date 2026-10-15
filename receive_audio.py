import serial
import wave

# === НАСТРОЙКИ ===
PORT = '/dev/ttyACM0'  # Укажите ваш порт
BAUD_RATE = 500000
SAMPLE_RATE = 16000    # Должно совпадать с SAMPLE_RATE в ESP32
# =================

print(f"Слушаю порт {PORT} на скорости {BAUD_RATE}...")

try:
    ser = serial.Serial(PORT, BAUD_RATE, timeout=0.1)
except Exception as e:
    print(f"Ошибка открытия порта: {e}")
    exit()

is_recording = False
f = None
buffer = b""

while True:
    try:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            buffer += chunk

        if not is_recording:
            if len(buffer) > 4096:
                buffer = buffer[-4096:]

            if b'START_FILE_TRANSFER:' in buffer:
                start_idx = buffer.find(b'START_FILE_TRANSFER:') + len(b'START_FILE_TRANSFER:')
                end_idx = buffer.find(b'\n', start_idx)
                
                if end_idx != -1:
                    filename = buffer[start_idx:end_idx].decode('utf-8', errors='ignore').strip()
                    filename = filename.lstrip('/') 
                    
                    # Меняем расширение файла на .wav
                    if filename.endswith('.pcm'):
                        filename = filename[:-4] + '.wav'
                    elif not filename.endswith('.wav'):
                        filename += '.wav'
                        
                    print(f"\n[+] Начало приема: {filename}")
                    
                    # Открываем файл через модуль wave для автоматической записи заголовка
                    f = wave.open(filename, 'wb')
                    f.setnchannels(1)           # 1 канал (Моно)
                    f.setsampwidth(2)           # 16 бит = 2 байта
                    f.setframerate(SAMPLE_RATE) # 16000 Гц
                    
                    is_recording = True
                    buffer = buffer[end_idx+1:]
        else:
            if b'[EXIT] sendFileToPC' in buffer:
                exit_idx = buffer.find(b'[EXIT] sendFileToPC')
                newline_idx = buffer.rfind(b'\n', 0, exit_idx)
                
                # Записываем фреймы (аудиоданные)
                if newline_idx != -1:
                    f.writeframes(buffer[:newline_idx])
                else:
                    f.writeframes(buffer[:exit_idx])
                    
                f.close()
                print(f"[+] Файл успешно сохранен в формате WAV!")
                is_recording = False
                buffer = buffer[exit_idx:]
            else:
                if len(buffer) > 50:
                    f.writeframes(buffer[:-50])
                    buffer = buffer[-50:]
                    
    except KeyboardInterrupt:
        print("\nОстановка.")
        if f:
            # Для wave объекта метод закрытия также корректно допишет размер файла в заголовок
            f.close()
        break