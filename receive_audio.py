import serial

# === НАСТРОЙКИ ===
PORT = '/dev/ttyACM0'  # Укажите ваш порт (ACM0 или USB0)
BAUD_RATE = 500000
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
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting)
            buffer += chunk

            if not is_recording:
                # Ждем маркер начала передачи
                if b'START_FILE_TRANSFER:' in buffer:
                    start_idx = buffer.find(b'START_FILE_TRANSFER:') + len(b'START_FILE_TRANSFER:')
                    end_idx = buffer.find(b'\n', start_idx)
                    
                    if end_idx != -1:
                        # Достаем имя файла (например, mic_12345.pcm)
                        filename = buffer[start_idx:end_idx].decode('utf-8', errors='ignore').strip()
                        filename = filename.lstrip('/') 
                        
                        print(f"\n[+] Начало приема: {filename}")
                        f = open(filename, 'wb')
                        is_recording = True
                        
                        # Записываем то, что пришло после переноса строки
                        f.write(buffer[end_idx+1:])
                        buffer = b""
            else:
                # Ищем маркер завершения передачи в логах ESP32
                if b'[EXIT] sendFileToPC' in buffer:
                    # Находим начало последней строки лога, чтобы отрезать таймстемп
                    exit_idx = buffer.find(b'[EXIT] sendFileToPC')
                    newline_idx = buffer.rfind(b'\n', 0, exit_idx)
                    
                    if newline_idx != -1:
                        f.write(buffer[:newline_idx])
                    else:
                        f.write(buffer[:exit_idx])
                        
                    f.close()
                    print(f"[+] Файл успешно сохранен!")
                    is_recording = False
                    buffer = buffer[exit_idx:] # Оставляем логи в буфере
                else:
                    # Записываем данные в файл, оставляя 50 байт в буфере 
                    # на случай, если маркер [EXIT] разбился между чанками
                    if len(buffer) > 50:
                        f.write(buffer[:-50])
                        buffer = buffer[-50:]
                        
    except KeyboardInterrupt:
        print("\nОстановка.")
        if f and not f.closed:
            f.close()
        break