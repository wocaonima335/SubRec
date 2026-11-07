pragma Singleton
import QtQuick

// Module U4 (ZCode). Page state of the main window; pages never reach into AppWindow.
// FROZEN API:
//   page: "record" | "library" | "settings"            (read-only for everyone but AppWindow)
//   settingsPage: "general" | "output" | "devices" | "subtitle" | "hotkeys" | "advanced" | "about"
//   open(target): "record" | "library" | "settings" | "settings/<settingsPage>"
//     (also accepts ShellView.pageRequested values). Unknown targets are ignored.
//   signal opened(page, settingsPage) after every change
QtObject {
    id: nav
    property string page: "record"
    property string settingsPage: "general"
    signal opened(string page, string settingsPage)

    function open(target: string) {
        const parts = target.split("/")
        const pages = ["record", "library", "settings"]
        const subPages = ["general", "output", "devices", "subtitle", "hotkeys", "advanced", "about"]
        if (pages.indexOf(parts[0]) < 0)
            return
        if (parts.length > 1) {
            if (parts[0] !== "settings" || subPages.indexOf(parts[1]) < 0)
                return
            nav.settingsPage = parts[1]
        }
        nav.page = parts[0]
        nav.opened(nav.page, nav.settingsPage)
    }
}
