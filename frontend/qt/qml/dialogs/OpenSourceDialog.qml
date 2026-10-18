pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: root
    objectName: "openSourceDialog"
    required property var hostWindow
    property var buildInfo: null
    title: qsTr("Open-source components and licenses")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(740, hostWindow.width - 48)
    height: Math.min(620, hostWindow.height - 48)
    standardButtons: Dialog.Close

    function metadataFor(name) {
        return buildInfo && buildInfo.dependencyInfo[name] ? buildInfo.dependencyInfo[name] : null
    }
    function licenseUrlFor(component) {
        const source = metadataFor(component.licenseDependency || component.dependencyName)
        return source && source.revision.length > 0
            ? component.licenseUrl.replace("/HEAD/", "/" + source.revision + "/") : component.licenseUrl
    }
    function selectionText(component) {
        const source = metadataFor(component.dependencyName)
        if (!source)
            return buildInfo ? qsTr("Not included in this build") : qsTr("Build information unavailable")
        if (source.selection === "developer-kit")
            return qsTr("Selected developer kit")
        if (source.selection === "system")
            return qsTr("Selected system library")
        return component.dependencyName === "vulkan-headers" ? qsTr("Build-time headers") : qsTr("Bundled source")
    }

    // Library licenses, rather than licenses of unrelated upstream CLI tools.
    // Keep this catalog aligned with cmake/Dependencies.cmake and third_party.
    readonly property var components: [
        {
            dependencyName: "qt",
            licenseDependency: "qt/qtbase",
            name: "Qt 6",
            purpose: qsTr("Desktop interface, QML, SVG and shader tools"),
            license: "LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only",
            projectUrl: "https://www.qt.io/",
            licenseUrl: "https://github.com/qt/qtbase/tree/HEAD/LICENSES"
        },
        {
            dependencyName: "libarchive",
            name: "libarchive",
            purpose: qsTr("Archive reading and writing"),
            license: qsTr("BSD-2-Clause and additional component licenses"),
            projectUrl: "https://github.com/libarchive/libarchive",
            licenseUrl: "https://github.com/libarchive/libarchive/blob/HEAD/COPYING"
        },
        {
            dependencyName: "zlib",
            name: "zlib",
            purpose: qsTr("DEFLATE compression"),
            license: "Zlib",
            projectUrl: "https://zlib.net/",
            licenseUrl: "https://github.com/madler/zlib/blob/HEAD/LICENSE"
        },
        {
            dependencyName: "bzip2",
            name: "bzip2 / libbzip2",
            purpose: qsTr("BZIP2 compression"),
            license: "bzip2-1.0.6",
            projectUrl: "https://sourceware.org/bzip2/",
            licenseUrl: "https://github.com/libarchive/bzip2/blob/HEAD/COPYING"
        },
        {
            dependencyName: "xz",
            name: "XZ Utils / liblzma",
            purpose: qsTr("XZ and LZMA compression"),
            license: "0BSD",
            projectUrl: "https://tukaani.org/xz/",
            licenseUrl: "https://github.com/tukaani-project/xz/blob/HEAD/COPYING"
        },
        {
            dependencyName: "zstd",
            name: "Zstandard",
            purpose: qsTr("Zstandard compression"),
            license: "BSD-3-Clause OR GPL-2.0-only",
            projectUrl: "https://facebook.github.io/zstd/",
            licenseUrl: "https://github.com/facebook/zstd/blob/HEAD/LICENSE"
        },
        {
            dependencyName: "lz4",
            name: "LZ4",
            purpose: qsTr("LZ4 compression"),
            license: "BSD-2-Clause",
            projectUrl: "https://lz4.org/",
            licenseUrl: "https://github.com/lz4/lz4/blob/HEAD/lib/LICENSE"
        },
        {
            dependencyName: "expat",
            name: "Expat",
            purpose: qsTr("XML parsing for archive metadata"),
            license: "MIT",
            projectUrl: "https://libexpat.github.io/",
            licenseUrl: "https://github.com/libexpat/libexpat/blob/HEAD/COPYING"
        },
        {
            dependencyName: "mbedtls",
            name: "Mbed TLS",
            purpose: qsTr("Archive cryptography (depending on build)"),
            license: "Apache-2.0 OR GPL-2.0-or-later",
            projectUrl: "https://github.com/Mbed-TLS/mbedtls",
            licenseUrl: "https://github.com/Mbed-TLS/mbedtls/blob/HEAD/LICENSE"
        },
        {
            dependencyName: "mbedtls/tf-psa-crypto",
            name: "TF-PSA-Crypto",
            purpose: qsTr("Cryptography provider for Mbed TLS"),
            license: "Apache-2.0 OR GPL-2.0-or-later",
            projectUrl: "https://github.com/Mbed-TLS/TF-PSA-Crypto",
            licenseUrl: "https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/HEAD/LICENSE"
        },
        {
            dependencyName: "vulkan-headers",
            name: "Vulkan-Headers",
            purpose: qsTr("Graphics API headers (build time)"),
            license: "Apache-2.0 OR MIT",
            projectUrl: "https://github.com/KhronosGroup/Vulkan-Headers",
            licenseUrl: "https://github.com/KhronosGroup/Vulkan-Headers/blob/HEAD/LICENSE.md"
        }
    ]

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            objectName: "licenseApplicationVersion"
            Layout.fillWidth: true
            text: qsTr("%1 — Version %2").arg(qsTr("LiteArc")).arg(Qt.application.version)
            font.weight: Font.DemiBold
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        Label {
            objectName: "licenseApplicationRevision"
            Layout.fillWidth: true
            text: qsTr("Application Git revision: %1").arg(root.buildInfo ? root.buildInfo.applicationRevision : qsTr("Unknown"))
                + (root.buildInfo && root.buildInfo.applicationDirty ? qsTr(" (modified working tree)") : "")
            textFormat: Text.PlainText
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("LiteArc uses the following open-source libraries. Thank you to their authors and contributors.")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
        }
        ListView {
            id: libraries
            objectName: "openSourceComponents"
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: root.components
            clip: true
            reuseItems: true
            spacing: 8
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Frame {
                id: library
                required property var modelData
                readonly property var buildMetadata: root.metadataFor(modelData.dependencyName)
                width: libraries.width - 16
                padding: 12
                contentItem: ColumnLayout {
                    spacing: 4
                    Label {
                        Layout.fillWidth: true
                        text: library.modelData.name
                        textFormat: Text.PlainText
                        font.weight: Font.DemiBold
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                    Label {
                        Layout.fillWidth: true
                        text: library.modelData.purpose
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "componentSelection-" + library.modelData.dependencyName
                        Layout.fillWidth: true
                        text: root.selectionText(library.modelData)
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        objectName: "componentVersion-" + library.modelData.dependencyName
                        Layout.fillWidth: true
                        visible: library.buildMetadata !== null
                        text: library.buildMetadata ? qsTr("Version: %1").arg(library.buildMetadata.version) : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                    Label {
                        objectName: "componentRevision-" + library.modelData.dependencyName
                        Layout.fillWidth: true
                        visible: library.buildMetadata !== null
                        text: library.buildMetadata ? qsTr("Git revision: %1").arg(library.buildMetadata.revision || qsTr("Not recorded")) : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                    Label {
                        objectName: "qtRuntimeVersion"
                        Layout.fillWidth: true
                        visible: library.modelData.dependencyName === "qt" && root.buildInfo !== null
                        text: root.buildInfo ? qsTr("Runtime version: %1").arg(root.buildInfo.qtRuntimeVersion) : ""
                        textFormat: Text.PlainText
                        wrapMode: Text.WordWrap
                    }
                    Repeater {
                        model: library.modelData.dependencyName === "qt"
                            ? ["qt/qtbase", "qt/qtdeclarative", "qt/qtsvg", "qt/qtshadertools"] : []
                        Label {
                            required property string modelData
                            readonly property var source: root.metadataFor(modelData)
                            Layout.fillWidth: true
                            visible: source !== null
                            text: source ? qsTr("%1 Git revision: %2").arg(modelData).arg(source.revision) : ""
                            textFormat: Text.PlainText
                            wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("License: %1").arg(library.modelData.license)
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                    }
                    RowLayout {
                        spacing: 8
                        Button {
                            text: qsTr("Project website")
                            flat: true
                            font.underline: true
                            Accessible.name: qsTr("Open %1 project website").arg(library.modelData.name)
                            Accessible.description: library.modelData.projectUrl
                            onClicked: Qt.openUrlExternally(library.modelData.projectUrl)
                        }
                        Button {
                            objectName: "componentLicense-" + library.modelData.dependencyName
                            text: qsTr("License text")
                            flat: true
                            font.underline: true
                            Accessible.name: qsTr("Read %1 license").arg(library.modelData.name)
                            Accessible.description: root.licenseUrlFor(library.modelData)
                            onClicked: Qt.openUrlExternally(root.licenseUrlFor(library.modelData))
                        }
                    }
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Versions and Git revisions come from this build's verified dependency records. Optional components not selected for this build are marked above. Qt's embedded components have separate notices; links open upstream license texts in your browser.")
            textFormat: Text.PlainText
            wrapMode: Text.WordWrap
            font.pixelSize: 12
        }
        Button {
            text: qsTr("Third-party components in Qt")
            flat: true
            font.underline: true
            onClicked: Qt.openUrlExternally("https://doc.qt.io/qt-6/licenses-used-in-qt.html")
        }
    }
}
