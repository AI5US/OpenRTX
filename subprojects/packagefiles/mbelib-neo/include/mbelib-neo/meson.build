# SPDX-FileCopyrightText: Copyright 2020-2026 OpenRTX Contributors
#
# SPDX-License-Identifier: GPL-3.0-or-later

version_conf = configuration_data()
version_conf.set('PROJECT_VERSION', meson.project_version())

configure_file(input         : 'version.h.in',
               output        : 'version.h',
               configuration : version_conf)
