# Keep package acceptance tied to the tests registered by the current desktop build.
function Assert-PackageRegressionCoverage([string[]] $TestNames) {
    foreach ($taskRequired in @('dependency_tools', 'core', 'archive', 'extraction', 'compression', 'application',
        'platform_packaging_metadata', 'qt_shell', 'qt_desktop_flow', 'qt_drag', 'qt_compression')) {
        if ($TestNames -notcontains $taskRequired) {
            throw ('Regression coverage is missing: ' + $taskRequired)
        }
    }
}
