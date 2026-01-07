-- SPDX-License-Identifier: GPL-3.0-or-later

set_project("viewbinding")

-- add requirements
add_requires("glib-2.0", {alias = "glib2", system = true})

target("viewbinding", function (target)
    add_rules("module.binary")
    add_files("main.c")
    add_packages("glib2")
end)