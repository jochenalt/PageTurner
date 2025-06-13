import serial.tools.list_ports

def list_all_serial_ports():
    ports = list(serial.tools.list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return

    print("Available serial ports:")
    for port in ports:
        print(f"Port: {port.device}, Description: {port.description}")

list_all_serial_ports()
