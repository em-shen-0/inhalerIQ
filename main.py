import sys
from PyQt5.QtWidgets import (
    QApplication, QWidget, QLabel, QPushButton, QVBoxLayout, QHBoxLayout, QStackedWidget, QFrame, QSizePolicy
)
from PyQt5.QtGui import QFont
from PyQt5.QtCore import Qt
import asyncio
from bleak import BleakScanner, BleakClient
from PyQt5.QtCore import QThread, pyqtSignal

uuid_inhaler_string_characteristic = '3b0d6406-ad62-49a0-aec3-ea8a17cc25fe'

## main window
class MainWindow(QStackedWidget):
    def __init__(self):
        super().__init__()
        self.setStyleSheet("""
            QWidget {
                background-color: #f0f4f8;
                font-family: Arial;
            }
            QPushButton {
                background-color: #4CAF50;
                color: white;
                padding: 10px;
                font-size: 14px;
                border-radius: 8px;
            }
            QPushButton:hover {
                background-color: #45a049;
            }
            QLabel {
                color: #333;
            }
        """)

        # Initialize the BLE worker
        self.ble_worker = BLEWorker()
        self.session_control_char = None  # set this once connected

        # self.training_page = TrainingPage(self.ble_worker)
        self.training_page = TrainingPage(self.ble_worker, start_session_callback= self.start_new_session)
        self.home_page = HomePage(self, self.ble_worker)

        self.addWidget(self.home_page)  # index 0
        self.addWidget(self.training_page)  # index 1

        self.setCurrentIndex(0)

    async def start_new_session(self):
        if self.ble_worker.client and self.session_control_char:
            try:
                await self.ble_worker.client.write_gatt_char(self.session_control_char, b"START")
                print("Sent: START")
            except Exception as e:
                print(f"Failed to send START: {e}")

## home page
class HomePage(QWidget):
    def __init__(self, stack, ble_worker):
        super().__init__()
        self.stack = stack
        self.ble_worker = ble_worker
        self.setup_ui()
    def setup_ui(self):
        # Header
        header = QLabel("Welcome to Inhaler Training")
        header.setFont(QFont("Arial", 16, QFont.Bold))
        header.setAlignment(Qt.AlignCenter)

        # Status / message
        status = QLabel("Tap below to begin your training session.")
        status.setFont(QFont("Arial", 11))
        status.setAlignment(Qt.AlignCenter)

        # Buttons
        start_button = QPushButton("Start Training")
        results_button = QPushButton("View Past Results")
        settings_button = QPushButton("Settings")

        start_button.clicked.connect(self.start_training)
        results_button.clicked.connect(self.view_results)
        settings_button.clicked.connect(self.open_settings)

        # Layout
        layout = QVBoxLayout()
        layout.setContentsMargins(40, 30, 40, 30)
        layout.setSpacing(20)
        layout.addStretch()
        layout.addWidget(header)
        layout.addWidget(status)
        layout.addStretch()
        layout.addWidget(start_button)
        layout.addWidget(results_button)
        layout.addWidget(settings_button)
        layout.addStretch()

        self.setLayout(layout)

    def start_training(self):
        if not self.ble_worker.isRunning():
            self.ble_worker._running = True
            self.ble_worker.start()
        self.stack.setCurrentIndex(1)  # Switch to TrainingPage
    def view_results(self):
        print("Showing results...")

    def open_settings(self):
        print("Opening settings...")

## training page
class TrainingPage(QWidget):
    def __init__(self, ble_worker, start_session_callback=None):
        super().__init__()
        self.start_session_callback = start_session_callback
        self.ble_worker = ble_worker
        self.setup_ui()
        # Connect BLE signal to display method
        self.ble_worker.data_received.connect(self.update_display)
        self.ble_worker.connected.connect(self.show_connected_message)

    def show_connected_message(self):
        self.data_display.setText("Connected to Inhaler! Begin shaking inhaler")
    def setup_ui(self):
        layout = QVBoxLayout()
        layout.setContentsMargins(40, 30, 40, 30)
        layout.setSpacing(20)
        header = QLabel("Inhaler Training in Progress")
        header.setFont(QFont("Arial", 16, QFont.Bold))
        header.setAlignment(Qt.AlignCenter)

        info = QLabel("Follow the steps shown here. Data will be captured live.")
        info.setAlignment(Qt.AlignCenter)

        self.label = QLabel("Live Data From Inhaler")
        self.label.setAlignment(Qt.AlignCenter)
        self.label.setFont(QFont("Arial", 16))

        self.data_display = QLabel("Waiting for sensor data...")
        self.data_display.setAlignment(Qt.AlignCenter)
        self.data_display.setFont(QFont("Courier", 14))
        self.data_display.setWordWrap(True)
        self.data_display.setSizePolicy(QSizePolicy.Expanding, QSizePolicy.Preferred)

        self.restart_button = QPushButton("Start New Session")
        self.restart_button.clicked.connect(self.start_session_callback)

        back_button = QPushButton("Back to Home")
        back_button.clicked.connect(self.go_back)

        layout.addStretch()
        layout.addWidget(header)
        layout.addWidget(info)
        layout.addWidget(self.data_display)
        layout.addWidget(self.restart_button)
        layout.addWidget(back_button)
        layout.addStretch()

        self.setLayout(layout)

    def update_display(self, data):
        self.data_display.setText(f"{data}")
        print(f"update_data_display received: {data}")  # <-- debug print

    def handle_start_button(self):
        if self.start_session_callback:
            try:
                asyncio.get_running_loop().create_task(self.start_session_callback())
            except RuntimeError:
                # No running event loop; fallback to run
                asyncio.run(self.start_session_callback())

    def go_back(self):
        self.parent().setCurrentIndex(0)
        self.ble_worker.stop()

## BLE connection
class BLEWorker(QThread):
    data_received = pyqtSignal(str)  # Signal to pass data to the UI
    connected = pyqtSignal()

    def __init__(self):
        super().__init__()
        self._running = True
        self.myDevice = None

    async def discover_device(self):
        devices = await BleakScanner.discover(5.0, return_adv=True)
        for d in devices:
            if (devices[d][1].local_name == 'Nano33IoT'):
                print("Found device:", devices[d][1].local_name)
                self.myDevice = devices[d][0]
                print(f"Device address: {self.myDevice.address}")
                return self.myDevice
        print("Device not found.")
        return None

    def notification_handler(self, sender, data):
        decoded_data = data.decode('utf-8')
        print(f"Notification from {sender}: {decoded_data}")
        self.data_received.emit(decoded_data)
    async def connect_and_read(self):
        if self.myDevice:
            async with BleakClient(self.myDevice) as client:
                await client.connect()
                print("Connected!")
                self.connected.emit()  # <-- Emit signal on successful connection

                await client.start_notify(uuid_inhaler_string_characteristic, self.notification_handler)
                print("Started notifications.")

                try:
                    while self._running:
                        await asyncio.sleep(0.1)  # Keep the loop alive to receive notifications
                except Exception as e:
                    print(f"Error during notification handling: {e}")
                finally:
                    await client.stop_notify(uuid_inhaler_string_characteristic)
                    print("Stopped notifications.")

    def run(self):
        asyncio.run(self.run_ble_loop())

    async def run_ble_loop(self):
        self.myDevice = await self.discover_device()
        if self.myDevice:
            await self.connect_and_read()

    def stop(self):
        self._running = False

#######################################################################
# main
if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = MainWindow()
    window.setWindowTitle("Inhaler Training Interface")
    window.resize(600, 400)
    window.show()
    sys.exit(app.exec_())