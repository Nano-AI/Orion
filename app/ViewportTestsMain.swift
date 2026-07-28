import Foundation

// Named so swiftc treats it as the entry point; top-level code is only
// permitted in main.swift, so this file is renamed at build time.
exit(Int32(ViewportTests.run()))
