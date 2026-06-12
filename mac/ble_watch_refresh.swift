import CoreBluetooth
import Foundation

let deviceNames = ["CCSwitch", "CCSwitch Display"]
let primaryDeviceName = "CCSwitch"
let serviceUUID = CBUUID(string: "7B3D0001-7C2F-4F81-9F2F-2D5A91C3CC01")
let statusUUID = CBUUID(string: "7B3D0002-7C2F-4F81-9F2F-2D5A91C3CC01")
let requestUUID = CBUUID(string: "7B3D0003-7C2F-4F81-9F2F-2D5A91C3CC01")

final class BLERefreshBridge: NSObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    private var central: CBCentralManager!
    private var targetPeripheral: CBPeripheral?
    private var statusCharacteristic: CBCharacteristic?
    private var requestCharacteristic: CBCharacteristic?
    private var periodicTimer: Timer?
    private var didPushOnCurrentConnection = false
    private var isRefreshing = false
    private var queuedRefreshReason: String?

    private let statusScriptPath: String
    private let periodicInterval: TimeInterval
    private let verbose: Bool

    init(statusScriptPath: String, periodicInterval: TimeInterval, verbose: Bool) {
        self.statusScriptPath = statusScriptPath
        self.periodicInterval = periodicInterval
        self.verbose = verbose
        super.init()
        self.central = CBCentralManager(delegate: self, queue: DispatchQueue.main)
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        guard central.state == .poweredOn else {
            if central.state != .unknown && central.state != .resetting {
                log("Bluetooth is not powered on: \(central.state.rawValue)")
            }
            return
        }
        scan()
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
            log("Discovered name=\(name) localName=\(advName) rssi=\(RSSI) services=[\(services)]")
        }

        if isTarget {
            log("Found \(primaryDeviceName); connecting...")
            targetPeripheral = peripheral
            peripheral.delegate = self
            central.stopScan()
            central.connect(peripheral)
        }
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        log("Connected; discovering status service...")
        statusCharacteristic = nil
        requestCharacteristic = nil
        didPushOnCurrentConnection = false
        peripheral.discoverServices([serviceUUID])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        log("Failed to connect: \(error?.localizedDescription ?? "unknown error")")
        scheduleReconnect()
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        if let error {
            log("Disconnected: \(error.localizedDescription)")
        } else {
            log("Disconnected")
        }
        periodicTimer?.invalidate()
        periodicTimer = nil
        statusCharacteristic = nil
        requestCharacteristic = nil
        targetPeripheral = nil
        scheduleReconnect()
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        if let error {
            log("Failed to discover services: \(error.localizedDescription)")
            scheduleReconnect()
            return
        }

        guard let service = peripheral.services?.first(where: { $0.uuid == serviceUUID }) else {
            log("Target service not found")
            scheduleReconnect()
            return
        }

        log("Status service found; discovering characteristics...")
        peripheral.discoverCharacteristics([statusUUID, requestUUID], for: service)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        if let error {
            log("Failed to discover characteristics: \(error.localizedDescription)")
            scheduleReconnect()
            return
        }

        for characteristic in service.characteristics ?? [] {
            if characteristic.uuid == statusUUID {
                statusCharacteristic = characteristic
                log("Status writer ready")
            } else if characteristic.uuid == requestUUID {
                requestCharacteristic = characteristic
                if characteristic.properties.contains(.notify) {
                    peripheral.setNotifyValue(true, for: characteristic)
                } else {
                    log("Request characteristic does not support notify")
                }
            }
        }

        guard statusCharacteristic != nil else {
            log("Status characteristic not found")
            scheduleReconnect()
            return
        }

        if requestCharacteristic == nil {
            log("Request characteristic not found; button refresh needs updated ESP32 firmware")
        }

        startPeriodicRefresh()
        refreshAndPush(reason: "connect")
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if characteristic.uuid != requestUUID {
            return
        }

        if let error {
            log("Request notify subscribe failed: \(error.localizedDescription)")
            return
        }

        log(characteristic.isNotifying ? "Listening for display refresh button" : "Request notify stopped")
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        if characteristic.uuid != requestUUID {
            return
        }

        if let error {
            log("Request notify failed: \(error.localizedDescription)")
            return
        }

        guard let data = characteristic.value, let text = String(data: data, encoding: .utf8) else {
            log("Request notify received non-UTF8 data")
            return
        }

        let request = text.trimmingCharacters(in: .whitespacesAndNewlines)
        log("Display request: \(request)")
        if request.contains("refresh_usage") {
            refreshAndPush(reason: "button")
        }
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        if let error {
            log("Status write failed: \(error.localizedDescription)")
            return
        }

        log("Status pushed")
    }

    private func scan() {
        periodicTimer?.invalidate()
        periodicTimer = nil
        statusCharacteristic = nil
        requestCharacteristic = nil
        targetPeripheral = nil
        log("Scanning for \(primaryDeviceName)...")
        central.scanForPeripherals(withServices: nil, options: [
            CBCentralManagerScanOptionAllowDuplicatesKey: false
        ])
    }

    private func scheduleReconnect() {
        periodicTimer?.invalidate()
        periodicTimer = nil
        DispatchQueue.main.asyncAfter(deadline: .now() + 2.0) {
            if self.central.state == .poweredOn {
                self.scan()
            }
        }
    }

    private func startPeriodicRefresh() {
        periodicTimer?.invalidate()
        periodicTimer = nil
        guard periodicInterval > 0 else {
            return
        }

        periodicTimer = Timer.scheduledTimer(withTimeInterval: periodicInterval, repeats: true) { [weak self] _ in
            self?.refreshAndPush(reason: "timer")
        }
    }

    private func refreshAndPush(reason: String) {
        if isRefreshing {
            queuedRefreshReason = reason
            log("Refresh already running; queued \(reason)")
            return
        }

        isRefreshing = true
        log("Refreshing usage via \(reason)...")

        DispatchQueue.global(qos: .utility).async {
            let payload = self.fetchStatusPayload()
            DispatchQueue.main.async {
                self.isRefreshing = false
                self.writeStatusPayload(payload)
                if let queuedReason = self.queuedRefreshReason {
                    self.queuedRefreshReason = nil
                    self.refreshAndPush(reason: queuedReason)
                }
            }
        }
    }

    private func fetchStatusPayload() -> String {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/env")
        process.arguments = ["python3", statusScriptPath]

        let stdout = Pipe()
        let stderr = Pipe()
        process.standardOutput = stdout
        process.standardError = stderr

        do {
            try process.run()
            process.waitUntilExit()
        } catch {
            return makeErrorPayload("run failed: \(error.localizedDescription)")
        }

        let output = String(data: stdout.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        let errorOutput = String(data: stderr.fileHandleForReading.readDataToEndOfFile(), encoding: .utf8) ?? ""
        let payload = output.trimmingCharacters(in: .whitespacesAndNewlines)

        guard process.terminationStatus == 0 else {
            let message = errorOutput.trimmingCharacters(in: .whitespacesAndNewlines)
            return makeErrorPayload(message.isEmpty ? "status script exited \(process.terminationStatus)" : message)
        }

        guard payload.hasPrefix("{"), payload.hasSuffix("}") else {
            return makeErrorPayload(payload.isEmpty ? "empty status payload" : "invalid status payload")
        }

        return payload
    }

    private func writeStatusPayload(_ payload: String) {
        guard let peripheral = targetPeripheral, let characteristic = statusCharacteristic else {
            log("No connected display; status not pushed")
            return
        }

        let writeType: CBCharacteristicWriteType = characteristic.properties.contains(.write) ? .withResponse : .withoutResponse
        log("Writing \(payload.utf8.count) bytes...")
        peripheral.writeValue(Data(payload.utf8), for: characteristic, type: writeType)
        if writeType == .withoutResponse {
            log("Status pushed")
        }
    }

    private func makeErrorPayload(_ message: String) -> String {
        let nowDate = Date()
        let now = Int(nowDate.timeIntervalSince1970 * 1000)
        let refreshedAtText = makeRefreshTimeText(nowDate)
        return """
        {"providerName":"Refresh","status":"error","usageOk":false,"usageError":"\(jsonEscape(message))","updatedAt":\(now),"ageSeconds":0,"refreshedAt":\(now),"refreshedAtText":"\(refreshedAtText)"}
        """
    }

    private func makeRefreshTimeText(_ date: Date) -> String {
        let formatter = DateFormatter()
        formatter.dateFormat = "yyyy-MM-dd HH:mm:ss"
        return formatter.string(from: date)
    }

    private func jsonEscape(_ value: String) -> String {
        var escaped = ""
        for character in value {
            switch character {
            case "\\":
                escaped += "\\\\"
            case "\"":
                escaped += "\\\""
            case "\n":
                escaped += "\\n"
            case "\r":
                escaped += "\\r"
            case "\t":
                escaped += "\\t"
            default:
                escaped.append(character)
            }
        }
        return escaped
    }

    private func log(_ message: String) {
        let formatter = ISO8601DateFormatter()
        print("[\(formatter.string(from: Date()))] \(message)")
        fflush(stdout)
    }
}

let args = CommandLine.arguments
let defaultStatusScriptPath = FileManager.default.currentDirectoryPath + "/cc_switch_provider_status.py"
let statusScriptPath = args.count >= 2 ? args[1] : defaultStatusScriptPath
let intervalValue = ProcessInfo.processInfo.environment["CC_SWITCH_BLE_INTERVAL"] ?? "120"
let periodicInterval = Double(intervalValue) ?? 120
let verbose = ProcessInfo.processInfo.environment["BLE_PUSH_VERBOSE"] == "1"

let bridge = BLERefreshBridge(statusScriptPath: statusScriptPath, periodicInterval: periodicInterval, verbose: verbose)
RunLoop.main.run()
