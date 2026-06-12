import CoreBluetooth
import Foundation

let deviceNames = ["CCSwitch", "CCSwitch Display"]
let primaryDeviceName = "CCSwitch"
let serviceUUID = CBUUID(string: "7B3D0001-7C2F-4F81-9F2F-2D5A91C3CC01")
let statusUUID = CBUUID(string: "7B3D0002-7C2F-4F81-9F2F-2D5A91C3CC01")

final class BLEStatusPusher: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var targetPeripheral: CBPeripheral?
    private var statusCharacteristic: CBCharacteristic?
    private let payload: Data
    private let timeoutSeconds: TimeInterval
    private let verbose: Bool
    private var didFinish = false

    init(payload: String, timeoutSeconds: TimeInterval, verbose: Bool) {
        self.payload = Data(payload.utf8)
        self.timeoutSeconds = timeoutSeconds
        self.verbose = verbose
        super.init()
        self.central = CBCentralManager(delegate: self, queue: DispatchQueue.main)
        DispatchQueue.main.asyncAfter(deadline: .now() + timeoutSeconds) {
            self.finish(code: 3, message: "BLE push timed out after \(Int(self.timeoutSeconds))s")
        }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central.state == .poweredOn else {
            if central.state != .unknown && central.state != .resetting {
                finish(code: 2, message: "Bluetooth is not powered on: \(central.state.rawValue)")
            }
            return
        }
        print("Bluetooth is powered on; scanning for \(primaryDeviceName)...")
        central.scanForPeripherals(withServices: nil, options: [
            CBCentralManagerScanOptionAllowDuplicatesKey: false
        ])
    }

    func centralManager(
        _ central: CBCentralManager,
        didDiscover peripheral: CBPeripheral,
        advertisementData: [String: Any],
        rssi RSSI: NSNumber
    ) {
        let localName = advertisementData[CBAdvertisementDataLocalNameKey] as? String
        let advertisedServices = advertisementData[CBAdvertisementDataServiceUUIDsKey] as? [CBUUID] ?? []
        let isTarget = deviceNames.contains(peripheral.name ?? "") ||
            deviceNames.contains(localName ?? "") ||
            advertisedServices.contains(serviceUUID)

        if verbose {
            let name = peripheral.name ?? "-"
            let advName = localName ?? "-"
            let services = advertisedServices.map { $0.uuidString }.joined(separator: ",")
            print("Discovered name=\(name) localName=\(advName) rssi=\(RSSI) services=[\(services)]")
        }

        if isTarget {
            print("Found \(primaryDeviceName); connecting...")
            targetPeripheral = peripheral
            peripheral.delegate = self
            central.stopScan()
            central.connect(peripheral)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        print("Connected; discovering status service...")
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        finish(code: 4, message: "Failed to connect: \(error?.localizedDescription ?? "unknown error")")
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error = error {
            finish(code: 5, message: "Failed to discover services: \(error.localizedDescription)")
            return
        }
        guard let services = peripheral.services else {
            finish(code: 5, message: "No services found")
            return
        }
        for service in services where service.uuid == serviceUUID {
            print("Status service found; discovering characteristic...")
            peripheral.discoverCharacteristics([statusUUID], for: service)
            return
        }
        finish(code: 5, message: "Target service not found")
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error = error {
            finish(code: 6, message: "Failed to discover characteristic: \(error.localizedDescription)")
            return
        }
        guard let characteristics = service.characteristics else {
            finish(code: 6, message: "No characteristics found")
            return
        }
        for characteristic in characteristics where characteristic.uuid == statusUUID {
            statusCharacteristic = characteristic
            let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.write) ? .withResponse : .withoutResponse
            print("Writing \(payload.count) bytes...")
            peripheral.writeValue(payload, for: characteristic, type: writeType)
            if writeType == .withoutResponse {
                finish(code: 0, message: "Status pushed")
            }
            return
        }
        finish(code: 6, message: "Target characteristic not found")
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error = error {
            finish(code: 7, message: "Write failed: \(error.localizedDescription)")
            return
        }
        finish(code: 0, message: "Status pushed")
    }

    private func finish(code: Int32, message: String) {
        guard !didFinish else { return }
        didFinish = true
        if let peripheral = targetPeripheral {
            central.cancelPeripheralConnection(peripheral)
        }
        print(message)
        exit(code)
    }
}

let args = CommandLine.arguments
guard args.count >= 2 else {
    fputs("Usage: ble_push_status '<json-payload>' [timeoutSeconds]\n", stderr)
    exit(64)
}

let payload = args[1]
let timeout = args.count >= 3 ? Double(args[2]) ?? 15 : 15
let verbose = ProcessInfo.processInfo.environment["BLE_PUSH_VERBOSE"] == "1"
_ = BLEStatusPusher(payload: payload, timeoutSeconds: timeout, verbose: verbose)
RunLoop.main.run()
