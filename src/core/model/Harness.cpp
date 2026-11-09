// SPDX-License-Identifier: GPL-3.0-or-later
#include "model/Harness.h"

#include <QUuid>

namespace tbh {

QString newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

} // namespace tbh
