import Cocoa
import FlutterMacOS

@main
class AppDelegate: FlutterAppDelegate {
  private var pendingFiles: [String] = []
  var desktopReady = false

  override func application(_ sender: NSApplication, openFiles filenames: [String]) {
    if desktopReady, let window = sender.windows.compactMap({ $0 as? MainFlutterWindow }).first {
      window.openFiles(filenames)
    } else {
      pendingFiles = Array(filenames.prefix(2)) // one pending event; multiple files rejected by controller
    }
    sender.reply(toOpenOrPrint: .success)
  }

  func takePendingFiles() -> [String] {
    desktopReady = true
    let files = pendingFiles
    pendingFiles = []
    return files
  }
  override func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    return true
  }

  override func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
    return true
  }
}
