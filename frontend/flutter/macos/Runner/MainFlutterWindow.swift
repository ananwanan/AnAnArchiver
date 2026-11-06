import Cocoa
import FlutterMacOS

class MainFlutterWindow: NSWindow {
  private var windowChannel: FlutterMethodChannel?
  func openFiles(_ files: [String]) {
    windowChannel?.invokeMethod("openFiles", arguments: Array(files.prefix(2)))
    makeKeyAndOrderFront(nil)
    NSApp.activate(ignoringOtherApps: true)
  }
  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = self.frame
    self.contentViewController = flutterViewController
    self.setFrame(windowFrame, display: true)

    RegisterGeneratedPlugins(registry: flutterViewController)
    windowChannel = FlutterMethodChannel(name: "litearc/window",
      binaryMessenger: flutterViewController.engine.binaryMessenger)
    windowChannel?.setMethodCallHandler { [weak self] call, result in
      guard let self = self else { result(FlutterError(code: "window_closed", message: "Window closed", details: nil)); return }
      if call.method == "ready" {
        result((NSApp.delegate as? AppDelegate)?.takePendingFiles() ?? [])
        return
      }
      if call.method == "state" {
        result(["width": max(640, min(7680, Int(self.frame.width))),
          "height": max(480, min(4320, Int(self.frame.height))), "maximized": self.isZoomed])
        return
      }
      if call.method == "restore", let values = call.arguments as? [String: Any] {
        let visible = self.screen?.visibleFrame ?? NSScreen.main?.visibleFrame ?? self.frame
        let width = min(visible.width, CGFloat(max(640, min(7680, values["width"] as? Int ?? 1280))))
        let height = min(visible.height, CGFloat(max(480, min(4320, values["height"] as? Int ?? 720))))
        self.setFrame(NSRect(x: visible.midX - width / 2, y: visible.midY - height / 2, width: width, height: height), display: true)
        if values["maximized"] as? Bool == true && !self.isZoomed { self.zoom(nil) }
        result(nil)
        return
      }
      guard call.method == "title", let title = call.arguments as? String else {
        result(FlutterMethodNotImplemented)
        return
      }
      self.title = title
      result(nil)
    }

    super.awakeFromNib()
  }
}
