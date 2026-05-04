import serial
import time

COM_PORT = 'COM8' # <-- CHANGE THIS TO YOUR ESP32 COM PORT
BAUD_RATE = 115200
EXPECTED_INTERVAL = 11.0 # 10s sleep + 1s delay

def draw_bar(drift_ms, max_drift=150):
    """Draws an ASCII bar showing drift direction and magnitude."""
    bar_width = 20
    # Normalize drift to bar width
    normalized = int((abs(drift_ms) / max_drift) * (bar_width // 2))
    normalized = min(max_drift, max(0, normalized))
    
    if drift_ms < 0:
        left_pad = (bar_width // 2) - normalized
        return f"[{' ' * left_pad}{'<' * normalized}|{' ' * (bar_width // 2)}]"
    else:
        return f"[{' ' * (bar_width // 2)}|{'>' * normalized}{' ' * ((bar_width // 2) - normalized)}]"

try:
    ser = serial.Serial(COM_PORT, BAUD_RATE, timeout=1)
    print(f"Listening on {COM_PORT}...")
    print(f"{'BOOT':<6} | {'TIME (s)':<10} | {'DRIFT (ms)':<12} | {'TOTAL DRIFT (ms)':<18} | GRAPH")
    print("-" * 80)
    
    last_time = None
    total_drift = 0
    
    while True:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        
        if line.startswith("PING:"):
            current_time = time.time()
            boot_count = line.split(":")[1]
            
            if last_time is not None:
                delta = current_time - last_time
                drift_sec = delta - EXPECTED_INTERVAL
                drift_ms = drift_sec * 1000
                total_drift += drift_ms
                
                bar = draw_bar(drift_ms)
                print(f"{boot_count:<6} | {delta:<10.3f} | {drift_ms:<12.1f} | {total_drift:<18.1f} | {bar}")
            else:
                print(f"{boot_count:<6} | {'---':<10} | {'---':<12} | {'---':<18} | Baseline set")
                
            last_time = current_time
            
except serial.SerialException as e:
    print(f"Error opening serial port: {e}")
except KeyboardInterrupt:
    print("\nStopped.")
finally:
    if 'ser' in locals() and ser.is_open:
        ser.close()