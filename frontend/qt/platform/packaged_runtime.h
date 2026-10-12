#pragma once

namespace ananarchiver::qt {
// Configure only a self-contained Windows deployment, before QGuiApplication
// loads QPA. Developer builds keep the explicitly selected build kit.
bool configurePackagedRuntime();
}
