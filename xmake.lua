-- SPDX-License-Identifier: GPL-3.0-or-later

set_project("viewbinding")

-- add requirements
add_requires("pacman::glib2", {alias = "glib2"})

target("viewbinding", function (target)
    add_rules("module.binary")
    add_files("main.c")
    add_packages("glib2")
end)