// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>

#include <map>
#include <optional>

namespace tbh::io {

// Entry name -> uncompressed bytes. Ordered, so archives are written deterministically.
using ZipEntries = std::map<QString, QByteArray>;

std::optional<ZipEntries> readZip(const QByteArray &archive, QString *error);
std::optional<QByteArray> writeZip(const ZipEntries &entries, QString *error);

} // namespace tbh::io
